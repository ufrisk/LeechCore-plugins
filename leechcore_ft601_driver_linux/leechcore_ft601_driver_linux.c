#include "leechcore_ft601_driver_linux.h"
#include "fpga_libusb.h"
#include "ft601_posix.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define FT_OK                       0
#define FT_INVALID_PARAMETER        6
#define FT_NOT_SUPPORTED            17
#define FT_TIMEOUT                  19
#define FT_OTHER_ERROR              32
#define FT_DEVICE_NOT_CONNECTED     30
#define FT_OPEN_BY_INDEX            0x10

#define FT_PIPE_OUT                 0x02
#define FT_PIPE_IN                  0x82
#define FT_DEFAULT_TIMEOUT_MS       1000
#define FT_KERNEL_MAX_WRITE         0x800U

struct ft_handle {
    uint32_t is_libusb;
    void *handle;
    int kernel_fd;
    uint32_t rx_timeout_ms;
    uint32_t tx_timeout_ms;
    int kernel_read_pending;
};

static ssize_t ft601_posix_read(
    int fd,
    void *buffer,
    size_t length
)
{
    return read(fd, buffer, length);
}

static ssize_t ft601_posix_write(
    int fd,
    const void *buffer,
    size_t length
)
{
    return write(fd, buffer, length);
}

static int ft601_posix_close(int fd)
{
    return close(fd);
}

static uint64_t ft601_posix_now_ms(void)
{
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC, &now)) {
        return 0;
    }
    return
        ((uint64_t)now.tv_sec * 1000) +
        ((uint64_t)now.tv_nsec / 1000000);
}

static void ft601_posix_sleep_ms(uint32_t milliseconds)
{
    struct timespec remaining = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (long)(milliseconds % 1000) * 1000000
    };
    while(nanosleep(&remaining, &remaining) && errno == EINTR) {
    }
}

static const FT601_POSIX_OPS g_posix_ops = {
    .read_fn = ft601_posix_read,
    .write_fn = ft601_posix_write,
    .close_fn = ft601_posix_close,
    .now_ms_fn = ft601_posix_now_ms,
    .sleep_ms_fn = ft601_posix_sleep_ms
};

static uint64_t ft601_deadline(uint32_t timeout_ms)
{
    uint64_t now = g_posix_ops.now_ms_fn();
    if(UINT64_MAX - now < timeout_ms) {
        return UINT64_MAX;
    }
    return now + timeout_ms;
}

static uint32_t ft601_remaining_ms(uint64_t deadline)
{
    uint64_t now = g_posix_ops.now_ms_fn();
    uint64_t remaining;
    if(now >= deadline) {
        return 0;
    }
    remaining = deadline - now;
    return remaining > UINT32_MAX ?
        UINT32_MAX :
        (uint32_t)remaining;
}

static uint32_t ft601_status_from_errno(int error)
{
    if(error == ETIMEDOUT) {
        return FT_TIMEOUT;
    }
    if(error == ENODEV || error == ENXIO || error == EBADF ||
       error == EPIPE || error == ESHUTDOWN) {
        return FT_DEVICE_NOT_CONNECTED;
    }
    return FT_OTHER_ERROR;
}

__attribute__((visibility("default")))
uint32_t FT_Create(
    void *pvArg,
    uint32_t dwFlags,
    struct ft_handle **pftHandle
)
{
    int i;
    int rc;
    int device_index = 0;
    struct ft_handle *fth;
    if(!pftHandle) {
        return FT_INVALID_PARAMETER;
    }
    *pftHandle = NULL;
    fth = calloc(1, sizeof(struct ft_handle));
    if(!fth) {
        return FT_OTHER_ERROR;
    }
    fth->kernel_fd = -1;
    fth->rx_timeout_ms = FT_DEFAULT_TIMEOUT_MS;
    fth->tx_timeout_ms = FT_DEFAULT_TIMEOUT_MS;

    {
        char device[12] = {
            '/', 'd', 'e', 'v', '/', 'f', 't', '6', '0', 'x', '0', 0
        };
        for(i = 0; i < 4; i++) {
            device[10] = '0' + i;
            rc = open(
                device,
                O_RDWR | O_CLOEXEC | O_NONBLOCK);
            if(rc >= 0) {
                fth->kernel_fd = rc;
                *pftHandle = fth;
                return FT_OK;
            }
        }
    }

    if(dwFlags == FT_OPEN_BY_INDEX) {
        device_index = (int)(size_t)pvArg;
    }
    fth->handle = fpga_open(device_index);
    if(fth->handle) {
        fth->is_libusb = 1;
        *pftHandle = fth;
        return FT_OK;
    }

    free(fth);
    return FT_OTHER_ERROR;
}

__attribute__((visibility("default")))
uint32_t FT_Close(struct ft_handle *fth)
{
    uint32_t status = FT_OK;
    if(!fth) {
        return FT_INVALID_PARAMETER;
    }
    if(fth->is_libusb) {
        if(fpga_close(fth->handle)) {
            return FT_OTHER_ERROR;
        }
    } else if(fth->kernel_fd >= 0) {
        if(g_posix_ops.close_fn(fth->kernel_fd)) {
            status = FT_OTHER_ERROR;
        }
        fth->kernel_fd = -1;
    }
    free(fth);
    return status;
}

__attribute__((visibility("default")))
uint32_t FT_GetChipConfiguration(
    struct ft_handle *fth,
    void *pvConfiguration
)
{
    if(!fth || !pvConfiguration) {
        return FT_INVALID_PARAMETER;
    }
    if(fth->is_libusb) {
        return fpga_get_chip_configuration(
            fth->handle,
            pvConfiguration) ? FT_OTHER_ERROR : FT_OK;
    }
    if(fth->kernel_fd < 0) {
        return FT_DEVICE_NOT_CONNECTED;
    }
    return ioctl(
        fth->kernel_fd,
        0,
        pvConfiguration) ? FT_OTHER_ERROR : FT_OK;
}

__attribute__((visibility("default")))
uint32_t FT_SetChipConfiguration(
    struct ft_handle *fth,
    void *pvConfiguration
)
{
    if(!fth || !pvConfiguration) {
        return FT_INVALID_PARAMETER;
    }
    if(fth->is_libusb) {
        return fpga_set_chip_configuration(
            fth->handle,
            pvConfiguration) ? FT_OTHER_ERROR : FT_OK;
    }
    if(fth->kernel_fd < 0) {
        return FT_DEVICE_NOT_CONNECTED;
    }
    return ioctl(
        fth->kernel_fd,
        1,
        pvConfiguration) ? FT_OTHER_ERROR : FT_OK;
}

__attribute__((visibility("default")))
uint32_t FT_SetSuspendTimeout(
    struct ft_handle *fth,
    uint32_t Timeout
)
{
    (void)Timeout;
    return fth ? FT_OK : FT_INVALID_PARAMETER;
}

__attribute__((visibility("default")))
uint32_t FT_SetPipeTimeout(
    struct ft_handle *fth,
    uint8_t ucPipeID,
    uint32_t ulTimeoutInMs
)
{
    if(!fth || !ulTimeoutInMs) {
        return FT_INVALID_PARAMETER;
    }
    if(ucPipeID == FT_PIPE_IN) {
        fth->rx_timeout_ms = ulTimeoutInMs;
        return FT_OK;
    }
    if(ucPipeID == FT_PIPE_OUT) {
        fth->tx_timeout_ms = ulTimeoutInMs;
        return FT_OK;
    }
    return FT_INVALID_PARAMETER;
}

__attribute__((visibility("default")))
uint32_t FT_AbortPipe(
    struct ft_handle *fth,
    uint8_t ucPipeID
)
{
    int close_status;
    if(!fth) {
        return FT_INVALID_PARAMETER;
    }
    if(fth->is_libusb) {
        return fpga_async_abort(fth->handle, ucPipeID);
    }
    if(ucPipeID == FT_PIPE_OUT) {
        return FT_OK;
    }
    if(ucPipeID != FT_PIPE_IN) {
        return FT_INVALID_PARAMETER;
    }
    if(!fth->kernel_read_pending) {
        return FT_OK;
    }
    close_status = fth->kernel_fd >= 0 ?
        g_posix_ops.close_fn(fth->kernel_fd) :
        0;
    fth->kernel_fd = -1;
    fth->kernel_read_pending = 0;
    return close_status ? FT_OTHER_ERROR : FT_OK;
}

static uint32_t ft601_kernel_write(
    struct ft_handle *fth,
    uint8_t *buffer,
    uint32_t length,
    uint32_t *transferred
)
{
    size_t chunk_transferred;
    uint32_t chunk_length;
    uint32_t timeout_ms;
    uint32_t status;
    uint64_t deadline = ft601_deadline(fth->tx_timeout_ms);
    *transferred = 0;
    while(*transferred < length) {
        timeout_ms = ft601_remaining_ms(deadline);
        if(!timeout_ms) {
            return FT_TIMEOUT;
        }
        chunk_length =
            length - *transferred > FT_KERNEL_MAX_WRITE ?
                FT_KERNEL_MAX_WRITE :
                length - *transferred;
        chunk_transferred = 0;
        if(ft601_posix_write_deadline(
                &g_posix_ops,
                fth->kernel_fd,
                buffer + *transferred,
                chunk_length,
                timeout_ms,
                &chunk_transferred)) {
            status = ft601_status_from_errno(errno);
            *transferred += (uint32_t)chunk_transferred;
            return status;
        }
        *transferred += (uint32_t)chunk_transferred;
    }
    return FT_OK;
}

__attribute__((visibility("default")))
uint32_t FT_WritePipe(
    struct ft_handle *fth,
    uint8_t ucPipeID,
    uint8_t *pucBuffer,
    uint32_t ulBufferLength,
    uint32_t *pulBytesTransferred,
    void *pOverlapped
)
{
    int libusb_transferred;
    uint32_t status;
    (void)ucPipeID;
    (void)pOverlapped;
    if(!fth || !pucBuffer || !ulBufferLength ||
       !pulBytesTransferred) {
        return FT_INVALID_PARAMETER;
    }
    *pulBytesTransferred = 0;
    if(fth->is_libusb) {
        libusb_transferred = 0;
        status = fpga_write(
            fth->handle,
            pucBuffer,
            ulBufferLength,
            &libusb_transferred,
            fth->tx_timeout_ms);
        *pulBytesTransferred = (uint32_t)libusb_transferred;
        return status;
    }
    if(fth->kernel_fd < 0) {
        return FT_DEVICE_NOT_CONNECTED;
    }
    return ft601_kernel_write(
        fth,
        pucBuffer,
        ulBufferLength,
        pulBytesTransferred);
}

__attribute__((visibility("default")))
uint32_t FT_WritePipeEx(
    struct ft_handle *fth,
    uint8_t ucPipeID,
    uint8_t *pucBuffer,
    uint32_t ulBufferLength,
    uint32_t *pulBytesTransferred,
    void *pOverlapped
)
{
    return FT_WritePipe(
        fth,
        ucPipeID,
        pucBuffer,
        ulBufferLength,
        pulBytesTransferred,
        pOverlapped);
}

static uint32_t ft601_kernel_read_pass(
    struct ft_handle *fth,
    uint8_t *buffer,
    uint32_t length,
    uint32_t *transferred,
    uint64_t deadline
)
{
    int read_pending;
    size_t read_length;
    uint32_t timeout_ms;
    uint32_t status;
    *transferred = 0;
    do {
        timeout_ms = ft601_remaining_ms(deadline);
        if(!timeout_ms) {
            return FT_TIMEOUT;
        }
        read_length = 0;
        read_pending = 0;
        if(ft601_posix_read_deadline(
                &g_posix_ops,
                fth->kernel_fd,
                buffer + *transferred,
                length - *transferred,
                timeout_ms,
                &read_length,
                &read_pending)) {
            status = ft601_status_from_errno(errno);
            fth->kernel_read_pending = read_pending;
            *transferred += (uint32_t)read_length;
            return status;
        }
        fth->kernel_read_pending = 0;
        if(!read_length) {
            return FT_DEVICE_NOT_CONNECTED;
        }
        *transferred += (uint32_t)read_length;
    } while(
        !(read_length % 0x1000) &&
        length > *transferred);
    return FT_OK;
}

__attribute__((visibility("default")))
uint32_t FT_ReadPipe(
    struct ft_handle *fth,
    uint8_t ucPipeID,
    uint8_t *pucBuffer,
    uint32_t ulBufferLength,
    uint32_t *pulBytesTransferred,
    void *pOverlapped
)
{
    int libusb_transferred;
    uint32_t i;
    uint32_t result = FT_OK;
    uint32_t pass_length;
    uint32_t total = 0;
    uint64_t deadline;
    (void)ucPipeID;
    if(!fth || !pucBuffer || !ulBufferLength ||
       !pulBytesTransferred) {
        return FT_INVALID_PARAMETER;
    }
    *pulBytesTransferred = 0;
    if(fth->is_libusb) {
        if(pOverlapped) {
            return fpga_async_read(
                fth->handle,
                pOverlapped,
                pucBuffer,
                ulBufferLength,
                fth->rx_timeout_ms);
        }
        libusb_transferred = 0;
        result = fpga_read(
            fth->handle,
            pucBuffer,
            ulBufferLength,
            &libusb_transferred,
            fth->rx_timeout_ms);
        *pulBytesTransferred = (uint32_t)libusb_transferred;
        return result;
    }
    if(fth->kernel_fd < 0) {
        return FT_DEVICE_NOT_CONNECTED;
    }
    deadline = ft601_deadline(fth->rx_timeout_ms);
    for(i = 0; i < 2 && total < ulBufferLength; i++) {
        result = ft601_kernel_read_pass(
            fth,
            pucBuffer + total,
            ulBufferLength - total,
            &pass_length,
            deadline);
        total += pass_length;
        if(result != FT_OK) {
            break;
        }
    }
    *pulBytesTransferred = total;
    return result;
}

__attribute__((visibility("default")))
uint32_t FT_ReadPipeEx(
    struct ft_handle *fth,
    uint8_t ucPipeID,
    uint8_t *pucBuffer,
    uint32_t ulBufferLength,
    uint32_t *pulBytesTransferred,
    void *pOverlapped
)
{
    return FT_ReadPipe(
        fth,
        ucPipeID,
        pucBuffer,
        ulBufferLength,
        pulBytesTransferred,
        pOverlapped);
}

__attribute__((visibility("default")))
uint32_t FT_InitializeOverlapped(
    struct ft_handle *fth,
    void *pOverlapped
)
{
    if(!fth || !pOverlapped) {
        return FT_INVALID_PARAMETER;
    }
    if(fth->is_libusb) {
        return fpga_async_init(fth->handle, pOverlapped);
    }
    return FT_NOT_SUPPORTED;
}

__attribute__((visibility("default")))
uint32_t FT_ReleaseOverlapped(
    struct ft_handle *fth,
    void *pOverlapped
)
{
    if(!fth || !pOverlapped) {
        return FT_INVALID_PARAMETER;
    }
    if(fth->is_libusb) {
        return fpga_async_close(fth->handle, pOverlapped);
    }
    return FT_NOT_SUPPORTED;
}

__attribute__((visibility("default")))
uint32_t FT_GetOverlappedResult(
    struct ft_handle *fth,
    void *pOverlapped,
    uint32_t *pulBytesTransferred,
    uint32_t bWait
)
{
    if(!fth || !pOverlapped || !pulBytesTransferred) {
        return FT_INVALID_PARAMETER;
    }
    if(fth->is_libusb) {
        return fpga_async_result(
            fth->handle,
            pOverlapped,
            pulBytesTransferred,
            bWait);
    }
    return FT_NOT_SUPPORTED;
}
