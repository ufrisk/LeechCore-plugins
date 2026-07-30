// fpga_libusb.c :
//     Code to directly communicate with the FT601 without using a kernel driver. Works with :
//     - Xilinx SP605 dev board flashed with PCILeech bitstream and FTDI UMFT601X-B addon-board.
//     - Xilinx AC701 dev board flashed with PCILeech bitstream and FTDI UMFT601X-B addon-board.
//     - PCIeScreamer board flashed with PCILeech bitstream.
//
// Contribution by Jérémie Boutoille from Synacktiv - www.synacktiv.com
// Based in part on PCIeScreamer kernel driver from LambdaConcept.
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libusb.h>
#include <sys/time.h>
#include <time.h>
#include "fpga_libusb.h"

// ----------------------------------------------------------------------------
// FPGA driver defines:
// ----------------------------------------------------------------------------

#define vprintfv(format, ...)       { printf(format, ##__VA_ARGS__); }

typedef enum tdFPGA_ASYNC_STATE {
    FPGA_ASYNC_UNINITIALIZED = 0,
    FPGA_ASYNC_IDLE,
    FPGA_ASYNC_SUBMITTED,
    FPGA_ASYNC_COMPLETED
} FPGA_ASYNC_STATE;

struct fpga_context {
    libusb_context *usb_ctx;
    libusb_device_handle *device_handle;
    int communication_interface_claimed;
    int data_interface_claimed;
    struct {
        FPGA_ASYNC_STATE state;
        struct libusb_transfer *transfer;
        void *cookie;
        uint32_t result_status;
        uint32_t transferred;
        int completed;
    } async;
};



// ----------------------------------------------------------------------------
// FPGA driver synchronous functionality:
// ----------------------------------------------------------------------------

int ftdi_GetChipConfiguration(struct fpga_context *ctx, struct FT_60XCONFIGURATION *config) {
    return libusb_control_transfer(ctx->device_handle,
        LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN,
        0xCF, // proprietary stuffz
        1, // value
        0, //index
        (void *)config,
        sizeof(struct FT_60XCONFIGURATION),
        1000
    );
}

int ftdi_SetChipConfiguration(struct fpga_context *ctx, struct FT_60XCONFIGURATION *config) {
    return libusb_control_transfer(ctx->device_handle,
        LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT,
        0xCF, // proprietary stuffz
        0, // value
        0, //index
        (void *)config,
        sizeof(struct FT_60XCONFIGURATION),
        1000
    );
}

static uint32_t fpga_libusb_status(int err)
{
    if(err == LIBUSB_ERROR_TIMEOUT) {
        return FPGA_FT_TIMEOUT;
    }
    if(err == LIBUSB_ERROR_NO_DEVICE) {
        return FPGA_FT_DEVICE_NOT_CONNECTED;
    }
    return FPGA_FT_OTHER_ERROR;
}

static uint64_t fpga_monotonic_ms(void)
{
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC, &now)) {
        return 0;
    }
    return
        ((uint64_t)now.tv_sec * 1000) +
        ((uint64_t)now.tv_nsec / 1000000);
}

uint32_t ftdi_SendCmdRead(
    struct fpga_context *ctx,
    int size,
    uint32_t timeout_ms
)
{
    int err;
    int transferred = 0;
    struct ft60x_ctrlreq ctrlreq;

    memset(&ctrlreq, 0, sizeof(ctrlreq));
    ctrlreq.idx++;
    ctrlreq.pipe = FTDI_ENDPOINT_IN;
    ctrlreq.cmd = 1; // read cmd.
    ctrlreq.len = size;

    err = libusb_bulk_transfer(
        ctx->device_handle,
        FTDI_ENDPOINT_SESSION_OUT,
        (void *)&ctrlreq,
        sizeof(struct ft60x_ctrlreq),
        &transferred,
        timeout_ms);
    if(err) {
        return fpga_libusb_status(err);
    }
    return transferred == (int)sizeof(ctrlreq) ?
        FPGA_FT_OK :
        FPGA_FT_OTHER_ERROR;
}

int fpga_get_chip_configuration(struct fpga_context *ctx, void *config)
{
    int rc = 0;
    int err;

    err = ftdi_GetChipConfiguration(ctx, config);
    if(err != sizeof(struct FT_60XCONFIGURATION)) {
        vprintfv("[-] cannot get chip config: %s\n", libusb_strerror(err));
        rc = -1;
    }

    return rc;
}

int fpga_set_chip_configuration(struct fpga_context *ctx, void *config)
{
    return ftdi_SetChipConfiguration(ctx, config) ==
        sizeof(struct FT_60XCONFIGURATION) ? 0 : -1;
}

struct fpga_context* fpga_open(int device_index)
{
    ssize_t device_count;
    struct fpga_context *ctx;
    libusb_device **device_list = NULL;
    libusb_device *device;
    struct libusb_device_descriptor desc;
    int err;
    int i;
    int found;
    struct FT_60XCONFIGURATION chip_configuration;
    unsigned char string[255] = { 0 };
    char description[255] = { 0 };

    ctx = malloc(sizeof(struct fpga_context));
    if(!ctx) { goto fail; }
    memset(ctx, 0, sizeof(struct fpga_context));

    err = libusb_init(&ctx->usb_ctx);
    if(err) {
        vprintfv("[-] libusb_init failed: %s\n", libusb_strerror(err));
        goto fail;
    }

    device_count = libusb_get_device_list(ctx->usb_ctx, &device_list);
    if(device_count < 0) {
        vprintfv("[-] Cannot get device list: %s\n", libusb_strerror(device_count));
        goto fail;
    }

    found = 0;
    for(i = 0; i < device_count; i++) {
        device = device_list[i];

        err = libusb_get_device_descriptor(device, &desc);
        if(err) {
            vprintfv("[-] Cannot get device descriptor: %s\n", libusb_strerror(err));
            goto fail;
        }

        if(desc.idVendor == FTDI_VENDOR_ID && desc.idProduct == FTDI_FT60X_PRODUCT_ID) {
            if(device_index) {
                device_index--;
            } else {
                vprintfv("[+] using FTDI device: %04x:%04x (bus %d, device %d)\n",
                    desc.idVendor,
                    desc.idProduct,
                    libusb_get_bus_number(device),
                    libusb_get_device_address(device));
                found = 1;
                break;
            }
        }
    }

    if(!found) {
        goto fail;
    }

    err = libusb_open(device, &ctx->device_handle);
    if(err) {
        vprintfv("[-] Cannot open device: %s\n", libusb_strerror(err));
        goto fail;
    }
    libusb_free_device_list(device_list, 1);
    device_list = NULL;

    err = libusb_get_string_descriptor_ascii(ctx->device_handle, desc.iManufacturer, string, sizeof(string));
    if(err) {
        snprintf(description, sizeof(description), "%s", string);
    } else {
        snprintf(description, sizeof(description), "%04X - ", desc.idVendor);
    }

    err = libusb_get_string_descriptor_ascii(ctx->device_handle, desc.iProduct, string, sizeof(string));
    if(err) {
        snprintf(description + strlen(description), sizeof(description) - strlen(description), "%s", string);
    } else {
        snprintf(description + strlen(description), sizeof(description) - strlen(description), "%04X", desc.idProduct);
    }

    err = libusb_get_string_descriptor_ascii(ctx->device_handle, desc.iSerialNumber, string, sizeof(string));
    if(err) {
        snprintf(description + strlen(description), sizeof(description) - strlen(description), "%s", string);
    }

    vprintfv("[+] %s\n", description);

    err = ftdi_GetChipConfiguration(ctx, &chip_configuration);
    if(err != sizeof(chip_configuration)) {
        vprintfv("[-] Cannot get chip configuration: %s\n", libusb_strerror(err));
        goto fail;
    }


    err = libusb_kernel_driver_active(ctx->device_handle, FTDI_COMMUNICATION_INTERFACE);
    if(err < 0) {
        vprintfv("[-] Cannot get kernel driver status for FTDI_COMMUNICATION_INTERFACE: %s\n", libusb_strerror(err));
        goto fail;
    }
    if(err) {
        vprintfv("[-] driver is active on FTDI_COMMUNICATION_INTERFACE = %d\n", err);
        goto fail;
    }

    err = libusb_kernel_driver_active(ctx->device_handle, FTDI_DATA_INTERFACE);
    if(err < 0) {
        vprintfv("[-] Cannot get kernel driver status for FTDI_DATA_INTERFACE: %s\n", libusb_strerror(err));
        goto fail;
    }
    if(err) {
        vprintfv("[-] driver is active on FTDI_DATA_INTERFACE = %d\n", err);
        goto fail;
    }

    err = libusb_claim_interface(ctx->device_handle, FTDI_COMMUNICATION_INTERFACE);
    if(err != 0) {
        vprintfv("[-] Cannot claim interface FTDI_COMMUNICATION_INTERFACE: %s\n", libusb_strerror(err));
        goto fail;
    }
    ctx->communication_interface_claimed = 1;

    err = libusb_claim_interface(ctx->device_handle, FTDI_DATA_INTERFACE);
    if(err != 0) {
        vprintfv("[-] Cannot claim interface FTDI_DATA_INTERFACE: %s\n", libusb_strerror(err));
        goto fail;
    }
    ctx->data_interface_claimed = 1;
    return ctx;
fail:
    if(device_list) {
        libusb_free_device_list(device_list, 1);
    }
    fpga_close(ctx);
    return NULL;
}

int fpga_close(struct fpga_context *ctx)
{
    if(ctx) {
        if(fpga_async_shutdown(ctx)) {
            return -1;
        }
        if(ctx->device_handle) {
            if(ctx->data_interface_claimed) {
                libusb_release_interface(
                    ctx->device_handle,
                    FTDI_DATA_INTERFACE);
            }
            if(ctx->communication_interface_claimed) {
                libusb_release_interface(
                    ctx->device_handle,
                    FTDI_COMMUNICATION_INTERFACE);
            }
            libusb_close(ctx->device_handle);
        }
        if(ctx->usb_ctx) {
            libusb_exit(ctx->usb_ctx);
        }
        free(ctx);
    }
    return 0;
}

uint32_t fpga_read(
    struct fpga_context *ctx,
    void *data,
    int size,
    int *transferred,
    uint32_t timeout_ms
)
{
    uint64_t elapsed_ms;
    uint64_t start_ms;
    uint32_t data_timeout_ms;
    uint32_t status;
    int err;

    if(!timeout_ms) {
        return FPGA_FT_INVALID_PARAMETER;
    }
    if(ctx->async.state == FPGA_ASYNC_SUBMITTED ||
       ctx->async.state == FPGA_ASYNC_COMPLETED) {
        vprintfv("[-] previous async read is not yet completed. complete by reading results before initiating new read!\n");
        return FPGA_FT_IO_PENDING;
    }
    start_ms = fpga_monotonic_ms();
    status = ftdi_SendCmdRead(ctx, size, timeout_ms);
    if(status != FPGA_FT_OK) {
        return status;
    }
    elapsed_ms = fpga_monotonic_ms();
    elapsed_ms =
        start_ms && elapsed_ms >= start_ms ?
            elapsed_ms - start_ms :
            0;
    if(elapsed_ms >= timeout_ms) {
        return FPGA_FT_TIMEOUT;
    }
    data_timeout_ms = timeout_ms - (uint32_t)elapsed_ms;

    *transferred = 0;
    err = libusb_bulk_transfer(
        ctx->device_handle,
        FTDI_ENDPOINT_IN,
        data,
        size,
        transferred,
        data_timeout_ms);
    if(err < 0) {
        vprintfv("[-] bulk transfer error: %s", libusb_strerror(err));
        return fpga_libusb_status(err);
    }

    return FPGA_FT_OK;
}

uint32_t fpga_write(
    struct fpga_context *ctx,
    void *data,
    int size,
    int *transferred,
    uint32_t timeout_ms
)
{
    int err;

    *transferred = 0;
    err = libusb_bulk_transfer(
        ctx->device_handle,
        FTDI_ENDPOINT_OUT,
        data,
        size,
        transferred,
        timeout_ms);

    if(err < 0) {
        vprintfv("[-] bulk transfer error: %s", libusb_strerror(err));
        return fpga_libusb_status(err);
    }

    if(*transferred != size) {
        vprintfv("[-] only %d bytes transferred\n", *transferred);
        return FPGA_FT_OTHER_ERROR;
    }

    return FPGA_FT_OK;
}



// ----------------------------------------------------------------------------
// "ASYNC" functionality below:
// ----------------------------------------------------------------------------

static uint32_t fpga_async_transfer_status(
    enum libusb_transfer_status status
)
{
    switch(status) {
        case LIBUSB_TRANSFER_COMPLETED:
        case LIBUSB_TRANSFER_CANCELLED:
            return FPGA_FT_OK;
        case LIBUSB_TRANSFER_TIMED_OUT:
            return FPGA_FT_TIMEOUT;
        case LIBUSB_TRANSFER_NO_DEVICE:
            return FPGA_FT_DEVICE_NOT_CONNECTED;
        default:
            return FPGA_FT_OTHER_ERROR;
    }
}

static void LIBUSB_CALL fpga_async_callback(
    struct libusb_transfer *transfer
)
{
    struct fpga_context *ctx = transfer->user_data;
    ctx->async.result_status =
        fpga_async_transfer_status(transfer->status);
    ctx->async.transferred =
        transfer->status == LIBUSB_TRANSFER_COMPLETED ?
            (uint32_t)transfer->actual_length :
            0;
    ctx->async.state = FPGA_ASYNC_COMPLETED;
    ctx->async.completed = 1;
}

uint32_t fpga_async_init(struct fpga_context *ctx, void *cookie)
{
    if(!ctx || !cookie) {
        return FPGA_FT_INVALID_PARAMETER;
    }
    if(ctx->async.state != FPGA_ASYNC_UNINITIALIZED) {
        vprintfv("[-] only one async overlapped supported. close previous one before open new!\n");
        return FPGA_FT_OTHER_ERROR;
    }
    ctx->async.transfer = libusb_alloc_transfer(0);
    if(!ctx->async.transfer) {
        return FPGA_FT_OTHER_ERROR;
    }
    ctx->async.cookie = cookie;
    ctx->async.state = FPGA_ASYNC_IDLE;
    return FPGA_FT_OK;
}

uint32_t fpga_async_close(struct fpga_context *ctx, void *cookie)
{
    if(!ctx || !cookie || ctx->async.cookie != cookie) {
        return FPGA_FT_INVALID_PARAMETER;
    }
    if(ctx->async.state == FPGA_ASYNC_SUBMITTED) {
        return FPGA_FT_IO_PENDING;
    }
    libusb_free_transfer(ctx->async.transfer);
    memset(&ctx->async, 0, sizeof(ctx->async));
    return FPGA_FT_OK;
}

uint32_t fpga_async_read(
    struct fpga_context *ctx,
    void *cookie,
    void *data,
    int size,
    uint32_t command_timeout_ms
)
{
    uint32_t command_status;
    int err;
    if(!ctx || !cookie || ctx->async.cookie != cookie ||
       !data || size <= 0) {
        return FPGA_FT_INVALID_PARAMETER;
    }
    if(ctx->async.state != FPGA_ASYNC_IDLE) {
        vprintfv("[-] previous async read is not yet completed. complete by reading results before initiating new read!\n");
        return FPGA_FT_IO_PENDING;
    }
    command_status = ftdi_SendCmdRead(
        ctx,
        size,
        command_timeout_ms);
    if(command_status != FPGA_FT_OK) {
        return command_status;
    }
    libusb_fill_bulk_transfer(
        ctx->async.transfer,
        ctx->device_handle,
        FTDI_ENDPOINT_IN,
        data,
        size,
        fpga_async_callback,
        ctx,
        command_timeout_ms);
    ctx->async.result_status = FPGA_FT_IO_PENDING;
    ctx->async.transferred = 0;
    ctx->async.completed = 0;
    ctx->async.state = FPGA_ASYNC_SUBMITTED;
    err = libusb_submit_transfer(ctx->async.transfer);
    if(err) {
        ctx->async.state = FPGA_ASYNC_IDLE;
        return err == LIBUSB_ERROR_NO_DEVICE ?
            FPGA_FT_DEVICE_NOT_CONNECTED :
            FPGA_FT_OTHER_ERROR;
    }
    return FPGA_FT_IO_PENDING;
}

uint32_t fpga_async_result(
    struct fpga_context *ctx,
    void *cookie,
    uint32_t *transferred,
    uint32_t is_wait
)
{
    int err;
    struct timeval timeout;
    uint32_t result_status;
    if(!ctx || !cookie || ctx->async.cookie != cookie || !transferred) {
        return FPGA_FT_INVALID_PARAMETER;
    }
    *transferred = 0;
    if(ctx->async.state == FPGA_ASYNC_IDLE) {
        return FPGA_FT_OK;
    }
    while(ctx->async.state == FPGA_ASYNC_SUBMITTED) {
        timeout.tv_sec = is_wait ? 1 : 0;
        timeout.tv_usec = 0;
        err = libusb_handle_events_timeout_completed(
            ctx->usb_ctx,
            &timeout,
            &ctx->async.completed);
        if(err == LIBUSB_ERROR_INTERRUPTED) {
            if(!is_wait) {
                return FPGA_FT_IO_INCOMPLETE;
            }
            continue;
        }
        if(err) {
            return err == LIBUSB_ERROR_NO_DEVICE ?
                FPGA_FT_DEVICE_NOT_CONNECTED :
                FPGA_FT_OTHER_ERROR;
        }
        if(!is_wait) {
            break;
        }
    }
    if(ctx->async.state == FPGA_ASYNC_SUBMITTED) {
        return FPGA_FT_IO_INCOMPLETE;
    }
    if(ctx->async.state != FPGA_ASYNC_COMPLETED) {
        return FPGA_FT_OTHER_ERROR;
    }
    result_status = ctx->async.result_status;
    *transferred = ctx->async.transferred;
    ctx->async.result_status = FPGA_FT_OK;
    ctx->async.transferred = 0;
    ctx->async.completed = 0;
    ctx->async.state = FPGA_ASYNC_IDLE;
    return result_status;
}

uint32_t fpga_async_abort(struct fpga_context *ctx, uint8_t endpoint)
{
    int err;
    if(!ctx) {
        return FPGA_FT_INVALID_PARAMETER;
    }
    if(endpoint == FTDI_ENDPOINT_OUT) {
        return FPGA_FT_OK;
    }
    if(endpoint != FTDI_ENDPOINT_IN) {
        return FPGA_FT_INVALID_PARAMETER;
    }
    if(ctx->async.state != FPGA_ASYNC_SUBMITTED) {
        return FPGA_FT_OK;
    }
    err = libusb_cancel_transfer(ctx->async.transfer);
    if(!err || err == LIBUSB_ERROR_NOT_FOUND) {
        return FPGA_FT_OK;
    }
    return err == LIBUSB_ERROR_NO_DEVICE ?
        FPGA_FT_DEVICE_NOT_CONNECTED :
        FPGA_FT_OTHER_ERROR;
}

int fpga_async_shutdown(struct fpga_context *ctx)
{
    int i;
    struct timeval timeout;
    if(!ctx || ctx->async.state == FPGA_ASYNC_UNINITIALIZED) {
        return 0;
    }
    if(ctx->async.state == FPGA_ASYNC_SUBMITTED) {
        if(fpga_async_abort(ctx, FTDI_ENDPOINT_IN) != FPGA_FT_OK) {
            return -1;
        }
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000;
        for(i = 0;
            i < 100 && ctx->async.state == FPGA_ASYNC_SUBMITTED;
            i++) {
            int err = libusb_handle_events_timeout_completed(
                ctx->usb_ctx,
                &timeout,
                &ctx->async.completed);
            if(err == LIBUSB_ERROR_INTERRUPTED) {
                continue;
            }
            if(err) {
                return -1;
            }
        }
        if(ctx->async.state == FPGA_ASYNC_SUBMITTED) {
            return -1;
        }
    }
    libusb_free_transfer(ctx->async.transfer);
    memset(&ctx->async, 0, sizeof(ctx->async));
    return 0;
}
