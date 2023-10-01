#include "leechcore_ft601_driver_linux.h"
#include "fpga_libusb.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define min(a, b)                   (((a) < (b)) ? (a) : (b))

#define FT_OK                       0
#define FT_NOT_SUPPORTED            17
#define FT_IO_PENDING               24
#define FT_OTHER_ERROR              32
#define FT_OPEN_BY_INDEX            0x10

struct ft_handle {
    uint32_t is_libusb;
    void* handle;
};

__attribute__((visibility("default")))
uint32_t FT_Create(void *pvArg, uint32_t dwFlags, struct ft_handle **pftHandle)
{
    int i, rc, device_index = 0;
    struct ft_handle *fth;
    fth = malloc(sizeof(struct ft_handle));
    if(!fth) {
        return FT_OTHER_ERROR;
    }
    // first try kernel driver
    {
        // NB! underlying driver will create a device object at /dev/ft60x[0-3]
        //     when loaded. Iterate through possible combinations at load time.
        char szDevice[12] = { '/', 'd', 'e', 'v', '/', 'f', 't', '6', '0', 'x', '0', 0 };
        for(i = 0; i < 4; i++) {
            szDevice[10] = '0' + i;
            rc = open(szDevice, O_RDWR | O_CLOEXEC);
            if(rc > 0) {
                fth->is_libusb = 0;
                fth->handle = (void*)(uint64_t)rc;
                *pftHandle = fth;
                return 0;
            }
        }
    }
    // try libusb built-in driver
    {
        if(dwFlags == FT_OPEN_BY_INDEX) {
            device_index = (int)(size_t)pvArg;
        }
        fth->handle = fpga_open(device_index);
        if(fth->handle) {
            fth->is_libusb = 1;
            *pftHandle = fth;
            return 0;
        }
    }
    free(fth);
    return FT_OTHER_ERROR;
}

__attribute__((visibility("default")))
uint32_t FT_Close(struct ft_handle *fth)
{
    if(fth->is_libusb) {
        fpga_close(fth->handle);
    } else {
        close((int)(uint64_t)fth->handle);
    }
    return 0;
}

__attribute__((visibility("default")))
uint32_t FT_GetChipConfiguration(struct ft_handle *fth, void *pvConfiguration)
{
    if(fth->is_libusb) {
        return (fpga_get_chip_configuration(fth->handle, pvConfiguration) == -1) ? 0x20 : 0;
    } else {
        return ioctl((int)(uint64_t)fth->handle, 0, pvConfiguration) ? 0x20 : 0;
    }
}

__attribute__((visibility("default")))
uint32_t FT_SetChipConfiguration(struct ft_handle *fth, void *pvConfiguration)
{
    if(fth->is_libusb) {
        return (fpga_set_chip_configuration(fth->handle, pvConfiguration) == -1) ? 0x20 : 0;
    } else {
        return ioctl((int)(uint64_t)fth->handle, 1, pvConfiguration) ? 0x20 : 0;
    }
}

__attribute__((visibility("default")))
uint32_t FT_SetSuspendTimeout(struct ft_handle *fth, uint32_t Timeout)
{
    // dummy function, only here for compatibility in Linux case
    return 0;
}

__attribute__((visibility("default")))
uint32_t FT_AbortPipe(struct ft_handle *fth, uint8_t ucPipeID)
{
    // dummy function, only here for compatibility in Linux case
    return 0;
}

__attribute__((visibility("default")))
uint32_t FT_WritePipe(struct ft_handle *fth, uint8_t ucPipeID, uint8_t *pucBuffer, uint32_t ulBufferLength, uint32_t *pulBytesTransferred, void *pOverlapped)
{
    int result, cbTxTotal = 0;
    if(fth->is_libusb) {
        return (fpga_write(fth->handle, pucBuffer, ulBufferLength, (int*)pulBytesTransferred) == -1) ? 0x20 : 0;
    } else {
        // NB! underlying ft60x driver cannot handle more than 0x800 bytes per write,
        //     split larger writes into smaller writes if required.
        while(cbTxTotal < ulBufferLength) {
            result = write((int)(uint64_t)fth->handle, pucBuffer + cbTxTotal, min(0x800, ulBufferLength - cbTxTotal));
            if(!result) { return 0x20; } // no bytes transmitted -> error
            cbTxTotal += result;
        }
        *pulBytesTransferred = cbTxTotal;
        return 0;
    }
}

__attribute__((visibility("default")))
uint32_t FT_WritePipeEx(struct ft_handle *fth, uint8_t ucPipeID, uint8_t *pucBuffer, uint32_t ulBufferLength, uint32_t *pulBytesTransferred, void *pOverlapped)
{
    return FT_WritePipe(fth, ucPipeID, pucBuffer, ulBufferLength, pulBytesTransferred, pOverlapped);
}

uint32_t FT_ReadPipe2_KernelDriver(struct ft_handle *fth, uint8_t ucPipeID, uint8_t *pucBuffer, uint32_t ulBufferLength, uint32_t *pulBytesTransferred, void *pOverlapped)
{
    int result;
    *pulBytesTransferred = 0;
    // NB! underlying driver have a max tranfer size in one go, multiple reads may be
    //     required to retrieve all data - hence the loop.
    do {
        result = read((int)(uint64_t)fth->handle, pucBuffer + *pulBytesTransferred, ulBufferLength - *pulBytesTransferred);
        if(result > 0) {
            *pulBytesTransferred += result;
        }
    } while((result > 0) && (0 == (result % 0x1000)) && (ulBufferLength > * pulBytesTransferred));
    return (result > 0) ? 0 : 0x20;
}

__attribute__((visibility("default")))
uint32_t FT_ReadPipe(struct ft_handle *fth, uint8_t ucPipeID, uint8_t *pucBuffer, uint32_t ulBufferLength, uint32_t *pulBytesTransferred, void *pOverlapped)
{
    uint32_t i, result, cbRx, cbRxTotal = 0;
    if(fth->is_libusb) {
        if(pOverlapped) {
            return (fpga_async_read(fth->handle, pucBuffer, ulBufferLength) == -1) ? FT_OTHER_ERROR : FT_IO_PENDING;
        } else {
            return (fpga_read(fth->handle, pucBuffer, ulBufferLength, (int*)pulBytesTransferred) == -1) ? FT_OTHER_ERROR : FT_OK;   
        }
    } else {
        // NB! underlying driver won't return all data on the USB core queue in first
        //     read so we have to read two times.
        for(i = 0; i < 2; i++) {
            result = FT_ReadPipe2_KernelDriver(fth, ucPipeID, pucBuffer + cbRxTotal, ulBufferLength - cbRxTotal, &cbRx, pOverlapped);
            cbRxTotal += cbRx;
        }
        *pulBytesTransferred = cbRxTotal;
        return result;
    }
}

__attribute__((visibility("default")))
uint32_t FT_ReadPipeEx(struct ft_handle *fth, uint8_t ucPipeID, uint8_t *pucBuffer, uint32_t ulBufferLength, uint32_t *pulBytesTransferred, void *pOverlapped)
{
    return FT_ReadPipe(fth, ucPipeID, pucBuffer, ulBufferLength, pulBytesTransferred, pOverlapped);
}

__attribute__((visibility("default")))
uint32_t FT_InitializeOverlapped(struct ft_handle *fth, void *pOverlapped)
{
    if(fth->is_libusb) {
        return fpga_async_init(fth->handle) ? FT_OTHER_ERROR : FT_OK;
    } else {
        return FT_NOT_SUPPORTED;    // not supported on kernel driver
    }
}

__attribute__((visibility("default")))
uint32_t FT_ReleaseOverlapped(struct ft_handle *fth, void *pOverlapped)
{
    if(fth->is_libusb) {
        return fpga_async_close(fth->handle) ? FT_OTHER_ERROR : FT_OK;
    } else {
        return FT_NOT_SUPPORTED;    // not supported on kernel driver
    }
}

__attribute__((visibility("default")))
uint32_t FT_GetOverlappedResult(struct ft_handle *fth, void *pOverlapped, uint32_t *pulBytesTransferred, uint32_t bWait)
{
    if(fth->is_libusb) {
        return fpga_async_result(fth->handle, pulBytesTransferred) ? FT_OTHER_ERROR : FT_OK;
    } else {
        return FT_NOT_SUPPORTED;    // not supported on kernel driver
    }
}
