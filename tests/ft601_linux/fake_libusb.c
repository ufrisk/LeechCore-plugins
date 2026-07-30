#include "fake_libusb.h"

#include <libusb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct libusb_context {
    int unused;
};

struct libusb_device {
    int unused;
};

struct libusb_device_handle {
    int unused;
};

typedef struct tdFAKE_LIBUSB_STATE {
    int sync_status;
    int sync_transferred;
    unsigned int sync_delay_ms;
    unsigned int last_sync_timeout;
    FAKE_ASYNC_OUTCOME async_outcome;
    int async_transferred;
    unsigned int cancel_count;
    unsigned int set_config_count;
    unsigned int free_device_list_count;
    unsigned int release_interface_count;
    unsigned int close_count;
    unsigned int exit_count;
    struct libusb_transfer *submitted_transfer;
} FAKE_LIBUSB_STATE;

static struct libusb_context g_context;
static struct libusb_device g_device;
static struct libusb_device_handle g_handle;
static FAKE_LIBUSB_STATE g_state;

void fake_libusb_reset(void)
{
    memset(&g_state, 0, sizeof(g_state));
    g_state.sync_transferred = -1;
}

void fake_libusb_set_sync_read(
    int status,
    int transferred,
    unsigned int delay_ms
)
{
    g_state.sync_status = status;
    g_state.sync_transferred = transferred;
    g_state.sync_delay_ms = delay_ms;
}

void fake_libusb_set_async_outcome(
    FAKE_ASYNC_OUTCOME outcome,
    int transferred
)
{
    g_state.async_outcome = outcome;
    g_state.async_transferred = transferred;
}

unsigned int fake_libusb_last_sync_timeout(void)
{
    return g_state.last_sync_timeout;
}

unsigned int fake_libusb_cancel_count(void)
{
    return g_state.cancel_count;
}

unsigned int fake_libusb_set_config_count(void)
{
    return g_state.set_config_count;
}

unsigned int fake_libusb_free_device_list_count(void)
{
    return g_state.free_device_list_count;
}

unsigned int fake_libusb_release_interface_count(void)
{
    return g_state.release_interface_count;
}

unsigned int fake_libusb_close_count(void)
{
    return g_state.close_count;
}

unsigned int fake_libusb_exit_count(void)
{
    return g_state.exit_count;
}

int LIBUSB_CALL libusb_init(libusb_context **ctx)
{
    *ctx = &g_context;
    return LIBUSB_SUCCESS;
}

void LIBUSB_CALL libusb_exit(libusb_context *ctx)
{
    (void)ctx;
    g_state.exit_count++;
}

ssize_t LIBUSB_CALL libusb_get_device_list(
    libusb_context *ctx,
    libusb_device ***list
)
{
    (void)ctx;
    *list = calloc(2, sizeof(libusb_device *));
    if(!*list) {
        return LIBUSB_ERROR_NO_MEM;
    }
    (*list)[0] = &g_device;
    return 1;
}

void LIBUSB_CALL libusb_free_device_list(
    libusb_device **list,
    int unref_devices
)
{
    (void)unref_devices;
    free(list);
    g_state.free_device_list_count++;
}

int LIBUSB_CALL libusb_get_device_descriptor(
    libusb_device *dev,
    struct libusb_device_descriptor *desc
)
{
    (void)dev;
    memset(desc, 0, sizeof(*desc));
    desc->idVendor = 0x0403;
    desc->idProduct = 0x601f;
    desc->iManufacturer = 1;
    desc->iProduct = 2;
    desc->iSerialNumber = 3;
    return LIBUSB_SUCCESS;
}

uint8_t LIBUSB_CALL libusb_get_bus_number(libusb_device *dev)
{
    (void)dev;
    return 1;
}

uint8_t LIBUSB_CALL libusb_get_device_address(libusb_device *dev)
{
    (void)dev;
    return 2;
}

int LIBUSB_CALL libusb_open(
    libusb_device *dev,
    libusb_device_handle **dev_handle
)
{
    (void)dev;
    *dev_handle = &g_handle;
    return LIBUSB_SUCCESS;
}

void LIBUSB_CALL libusb_close(libusb_device_handle *dev_handle)
{
    (void)dev_handle;
    g_state.close_count++;
}

int LIBUSB_CALL libusb_get_string_descriptor_ascii(
    libusb_device_handle *dev_handle,
    uint8_t desc_index,
    unsigned char *data,
    int length
)
{
    const char *value;
    int value_length;
    (void)dev_handle;
    value = desc_index == 1 ? "FTDI" :
        (desc_index == 2 ? "SuperSpeed-FIFO Bridge" : "000000000001");
    value_length = (int)strlen(value);
    if(value_length >= length) {
        value_length = length - 1;
    }
    memcpy(data, value, (size_t)value_length);
    data[value_length] = 0;
    return value_length;
}

int LIBUSB_CALL libusb_kernel_driver_active(
    libusb_device_handle *dev_handle,
    int interface_number
)
{
    (void)dev_handle;
    (void)interface_number;
    return 0;
}

int LIBUSB_CALL libusb_claim_interface(
    libusb_device_handle *dev_handle,
    int interface_number
)
{
    (void)dev_handle;
    (void)interface_number;
    return LIBUSB_SUCCESS;
}

int LIBUSB_CALL libusb_release_interface(
    libusb_device_handle *dev_handle,
    int interface_number
)
{
    (void)dev_handle;
    (void)interface_number;
    g_state.release_interface_count++;
    return LIBUSB_SUCCESS;
}

int LIBUSB_CALL libusb_control_transfer(
    libusb_device_handle *dev_handle,
    uint8_t request_type,
    uint8_t request,
    uint16_t value,
    uint16_t index,
    unsigned char *data,
    uint16_t length,
    unsigned int timeout
)
{
    (void)dev_handle;
    (void)request;
    (void)value;
    (void)index;
    (void)timeout;
    if(request_type & LIBUSB_ENDPOINT_IN) {
        memset(data, 0, length);
        return length;
    }
    g_state.set_config_count++;
    return length;
}

int LIBUSB_CALL libusb_bulk_transfer(
    libusb_device_handle *dev_handle,
    unsigned char endpoint,
    unsigned char *data,
    int length,
    int *transferred,
    unsigned int timeout
)
{
    int result_length;
    (void)dev_handle;
    (void)data;
    g_state.last_sync_timeout = timeout;
    if(g_state.sync_delay_ms) {
        usleep(g_state.sync_delay_ms * 1000);
    }
    result_length = g_state.sync_transferred < 0 ?
        length :
        g_state.sync_transferred;
    *transferred = result_length;
    if(endpoint == 0x01) {
        return LIBUSB_SUCCESS;
    }
    return g_state.sync_status;
}

const char * LIBUSB_CALL libusb_strerror(int errcode)
{
    (void)errcode;
    return "fake libusb error";
}

struct libusb_transfer * LIBUSB_CALL libusb_alloc_transfer(
    int iso_packets
)
{
    size_t size = sizeof(struct libusb_transfer) +
        ((size_t)iso_packets * sizeof(struct libusb_iso_packet_descriptor));
    return calloc(1, size);
}

void LIBUSB_CALL libusb_free_transfer(struct libusb_transfer *transfer)
{
    free(transfer);
}

int LIBUSB_CALL libusb_submit_transfer(struct libusb_transfer *transfer)
{
    g_state.submitted_transfer = transfer;
    return LIBUSB_SUCCESS;
}

int LIBUSB_CALL libusb_cancel_transfer(struct libusb_transfer *transfer)
{
    if(transfer != g_state.submitted_transfer) {
        return LIBUSB_ERROR_NOT_FOUND;
    }
    g_state.cancel_count++;
    transfer->status = LIBUSB_TRANSFER_CANCELLED;
    g_state.async_outcome = FAKE_ASYNC_COMPLETE;
    g_state.async_transferred = 0;
    return LIBUSB_SUCCESS;
}

int LIBUSB_CALL libusb_handle_events_timeout_completed(
    libusb_context *ctx,
    struct timeval *tv,
    int *completed
)
{
    struct libusb_transfer *transfer = g_state.submitted_transfer;
    (void)ctx;
    (void)tv;
    if(!transfer || g_state.async_outcome == FAKE_ASYNC_PENDING) {
        return LIBUSB_SUCCESS;
    }
    if(transfer->status != LIBUSB_TRANSFER_CANCELLED) {
        transfer->status =
            g_state.async_outcome == FAKE_ASYNC_COMPLETE ?
                LIBUSB_TRANSFER_COMPLETED :
                LIBUSB_TRANSFER_ERROR;
    }
    transfer->actual_length = g_state.async_transferred;
    g_state.submitted_transfer = NULL;
    g_state.async_outcome = FAKE_ASYNC_PENDING;
    transfer->callback(transfer);
    if(completed) {
        *completed = 1;
    }
    return LIBUSB_SUCCESS;
}
