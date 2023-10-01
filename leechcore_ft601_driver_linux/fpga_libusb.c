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
#include <pthread.h>
#include "fpga_libusb.h"

// ----------------------------------------------------------------------------
// SRWLock functionality:
// ----------------------------------------------------------------------------

#include <stdatomic.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

typedef void                                VOID, *PVOID;
typedef uint32_t                            BOOL, *PBOOL;
typedef uint32_t                            DWORD, *PDWORD, ULONG, *PULONG;
#define TRUE                                1
#define FALSE                               0
#define _Inout_

typedef struct tdSRWLOCK {
    uint32_t xchg;
    int c;
} SRWLOCK, *PSRWLOCK;
VOID AcquireSRWLockExclusive(_Inout_ PSRWLOCK SRWLock);
VOID ReleaseSRWLockExclusive(_Inout_ PSRWLOCK SRWLock);
#define SRWLOCK_INIT            { 0 }

static int futex(uint32_t *uaddr, int futex_op, uint32_t val, const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3)
{
    return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

VOID AcquireSRWLockExclusive(_Inout_ PSRWLOCK SRWLock)
{
    DWORD dwZero;
    __sync_fetch_and_add_4(&SRWLock->c, 1);
    while(TRUE) {
        dwZero = 0;
        if(atomic_compare_exchange_strong(&SRWLock->xchg, &dwZero, 1)) {
            return;
        }
        futex(&SRWLock->xchg, FUTEX_WAIT, 1, NULL, NULL, 0);
    }
}

VOID ReleaseSRWLockExclusive(_Inout_ PSRWLOCK SRWLock)
{
    DWORD dwOne = 1;
    if(atomic_compare_exchange_strong(&SRWLock->xchg, &dwOne, 0)) {
        if(__sync_sub_and_fetch_4(&SRWLock->c, 1)) {
            futex(&SRWLock->xchg, FUTEX_WAKE, 1, NULL, NULL, 0);
        }
    }
}



// ----------------------------------------------------------------------------
// FPGA driver defines:
// ----------------------------------------------------------------------------

#define vprintfv(format, ...)       { printf(format, ##__VA_ARGS__); }

struct fpga_context {
    libusb_context *usb_ctx;
    libusb_device_handle *device_handle;
    // async context:
    struct {
        uint32_t is_valid;
        // thread control:
        pthread_t tid;
        int is_thread_running;
        // thread read lock:
        SRWLOCK lock_thread_read;
        int is_thread_read;
        // result lock (wait for read to complete):
        SRWLOCK lock_result;
        int is_result;
        // data:
        void* data;
        int data_read;
        int data_size;
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

int ftdi_SendCmdRead(struct fpga_context *ctx, int size)
{
    int transferred = 0;
    struct ft60x_ctrlreq ctrlreq;

    memset(&ctrlreq, 0, sizeof(ctrlreq));
    ctrlreq.idx++;
    ctrlreq.pipe = FTDI_ENDPOINT_IN;
    ctrlreq.cmd = 1; // read cmd.
    ctrlreq.len = size;

    return libusb_bulk_transfer(ctx->device_handle, FTDI_ENDPOINT_SESSION_OUT, (void *)&ctrlreq, sizeof(struct ft60x_ctrlreq), &transferred, 1000);
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
    int rc = 0;

    if(ftdi_SetChipConfiguration(ctx, config) != LIBUSB_SUCCESS) {
        rc = -1;
    }

    return rc;
}

struct fpga_context* fpga_open(int device_index)
{
    ssize_t device_count;
    struct fpga_context *ctx;
    libusb_device **device_list;
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

    err = libusb_get_string_descriptor_ascii(ctx->device_handle, desc.iManufacturer, string, sizeof(string));
    if(err) {
        snprintf(description, sizeof(description), "%s", string);
    } else {
        snprintf(description, sizeof(description), "%04X - ", desc.idVendor);
    }

    err = libusb_get_string_descriptor_ascii(ctx->device_handle, desc.iProduct, string, sizeof(string));
    if(err) {
        snprintf(description + strlen(description), sizeof(description), "%s", string);
    } else {
        snprintf(description + strlen(description), sizeof(description), "%04X", desc.idProduct);
    }

    err = libusb_get_string_descriptor_ascii(ctx->device_handle, desc.iSerialNumber, string, sizeof(string));
    if(err) {
        snprintf(description + strlen(description), sizeof(description), "%s", string);
    }

    vprintfv("[+] %s\n", description);

    err = ftdi_GetChipConfiguration(ctx, &chip_configuration);
    if(err != sizeof(chip_configuration)) {
        vprintfv("[-] Cannot get chip configuration: %s\n", libusb_strerror(err));
        goto fail;
    }


    if(chip_configuration.FIFOMode != CONFIGURATION_FIFO_MODE_245 ||
        chip_configuration.ChannelConfig != CONFIGURATION_CHANNEL_CONFIG_1 ||
        chip_configuration.OptionalFeatureSupport != CONFIGURATION_OPTIONAL_FEATURE_DISABLEALL
        ) {
        vprintfv("[!] Bad FTDI configuration... setting chip config to fifo 245 && 1 channel, no feature support\n");

        chip_configuration.FIFOMode = CONFIGURATION_FIFO_MODE_245;
        chip_configuration.ChannelConfig = CONFIGURATION_CHANNEL_CONFIG_1;
        chip_configuration.OptionalFeatureSupport = CONFIGURATION_OPTIONAL_FEATURE_DISABLEALL;

        err = ftdi_SetChipConfiguration(ctx, &chip_configuration);
        if(err != sizeof(chip_configuration)) {
            vprintfv("[-] Cannot set chip configuration: %s\n", libusb_strerror(err));
            goto fail;
        }

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

    err = libusb_claim_interface(ctx->device_handle, FTDI_DATA_INTERFACE);
    if(err != 0) {
        vprintfv("[-] Cannot claim interface FTDI_DATA_INTERFACE: %s\n", libusb_strerror(err));
        goto fail;
    }
    return ctx;
fail:
    fpga_close(ctx);
    return NULL;
}

int fpga_close(struct fpga_context *ctx)
{
    if(ctx) {
        if(ctx->device_handle) {
            libusb_close(ctx->device_handle);
        }
        if(ctx->usb_ctx) {
            libusb_exit(ctx->usb_ctx);
        }
        free(ctx);
    }
    return 0;
}

int fpga_read_internal(struct fpga_context *ctx, void *data, int size, int *transferred)
{
    int err;

    err = ftdi_SendCmdRead(ctx, size);
    if(err) {
        vprintfv("[-] cannot send CmdRead ftdi: %s", libusb_strerror(err));
        return -1;
    }

    *transferred = 0;
    err = libusb_bulk_transfer(ctx->device_handle, FTDI_ENDPOINT_IN, data, size, transferred, 0);
    if(err < 0) {
        vprintfv("[-] bulk transfer error: %s", libusb_strerror(err));
        return -1;
    }

    return 0;
}

int fpga_read(struct fpga_context *ctx, void *data, int size, int *transferred)
{
    if(ctx->async.is_thread_read) {
        vprintfv("[-] previous async read is not yet completed. complete by reading results before initiating new read!\n");
        return -1;
    }
    return fpga_read_internal(ctx, data, size, transferred);
}

int fpga_write(struct fpga_context *ctx, void *data, int size, int *transferred)
{
    int err;

    *transferred = 0;
    err = libusb_bulk_transfer(ctx->device_handle, FTDI_ENDPOINT_OUT, data, size, transferred, 1000);

    if(err < 0) {
        vprintfv("[-] bulk transfer error: %s", libusb_strerror(err));
        return -1;
    }

    if(*transferred != size) {
        vprintfv("[-] only %d bytes transferred\n", *transferred);
        return -1;
    }

    return 0;
}



// ----------------------------------------------------------------------------
// "ASYNC" functionality below:
// ----------------------------------------------------------------------------

void* fpga_async_thread(void* thread_ctx)
{
    struct fpga_context *ctx = thread_ctx;
    AcquireSRWLockExclusive(&ctx->async.lock_thread_read);
    while(ctx->async.is_valid) {
        usleep(5);
        fpga_read_internal(ctx, ctx->async.data, ctx->async.data_size, &ctx->async.data_read);
        ctx->async.is_result = 1;
        ReleaseSRWLockExclusive(&ctx->async.lock_result);
        AcquireSRWLockExclusive(&ctx->async.lock_thread_read);
    };
    ReleaseSRWLockExclusive(&ctx->async.lock_result);
    ctx->async.is_thread_read = 0;
    ctx->async.tid = 0;
    return NULL;
}

int fpga_async_init(struct fpga_context *ctx)
{
    if(ctx->async.is_valid) {
        vprintfv("[-] only one async overlapped supported. close previous one before open new!\n");
        return -1;
    }

    ctx->async.is_result = 1;
    ctx->async.is_valid = 1;
    AcquireSRWLockExclusive(&ctx->async.lock_result);
    AcquireSRWLockExclusive(&ctx->async.lock_thread_read);

    pthread_create(&ctx->async.tid, NULL, fpga_async_thread, ctx);
    if(!ctx->async.tid) {
        vprintfv("[-] failed creating thread.\n");
        memset(&ctx->async, 0, sizeof(ctx->async));
        return -1;
    }

    return 0;
}

int fpga_async_close(struct fpga_context *ctx)
{
    if(ctx->async.is_valid) {
        ctx->async.is_valid = 0;
        ReleaseSRWLockExclusive(&ctx->async.lock_thread_read);
        while(ctx->async.tid) { sched_yield(); }
        memset(&ctx->async, 0, sizeof(ctx->async));
    }
    return 0;
}

int fpga_async_read(struct fpga_context *ctx, void *data, int size)
{
    if(!ctx->async.is_valid) {
        vprintfv("[-] invalid context!\n");
        return -1;
    }
    if(ctx->async.is_thread_read) {
        vprintfv("[-] previous async read is not yet completed. complete by reading results before initiating new read!\n");
        return -1;
    }
    ctx->async.data = data;
    ctx->async.data_size = size;
    ctx->async.data_read = 0;
    ctx->async.is_result = 0;
    ctx->async.is_thread_read = 1;
    ReleaseSRWLockExclusive(&ctx->async.lock_thread_read);
    return 0;
}

int fpga_async_result(struct fpga_context *ctx, uint32_t *transferred)
{
    if(!ctx->async.is_valid) {
        vprintfv("[-] invalid context!\n");
        return -1;
    }
    if(ctx->async.is_thread_read) {
        AcquireSRWLockExclusive(&ctx->async.lock_result);
        *transferred = ctx->async.data_read;
        ctx->async.is_thread_read = 0;
        ctx->async.is_result = 1;
    } else {
        *transferred = 0;
        ctx->async.is_result = 1;
    }
    return 0;
}
