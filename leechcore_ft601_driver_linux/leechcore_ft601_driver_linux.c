#include "leechcore_ft601_driver_linux.h"
#include "fpga_libusb.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define FT601_HANDLE_LIBUSB         (void*)-2
#define min(a, b)                   (((a) < (b)) ? (a) : (b))

__attribute__((visibility("default")))
uint32_t FT_Create(void *pvArg, uint32_t dwFlags, void **pftHandle)
{
    int i, rc;
    // first try kernel driver
    {
        // NB! underlying driver will create a device object at /dev/ft60x[0-3]
        //     when loaded. Iterate through possible combinations at load time.
        char szDevice[12] = { '/', 'd', 'e', 'v', '/', 'f', 't', '6', '0', 'x', '0', 0 };
        for(i = 0; i < 4; i++) {
            szDevice[10] = '0' + i;
            rc = open(szDevice, O_RDWR | O_CLOEXEC);
            if(rc > 0) {
                *pftHandle = (void*)(uint64_t)rc;
                return 0;
            }
        }
    }
    // try libusb built-in driver
    {
        rc = fpga_open();
        if(rc != -1) {
            *pftHandle = FT601_HANDLE_LIBUSB;
            return 0;
        }
    }
    return 0x20;
}

__attribute__((visibility("default")))
uint32_t FT_Close(void *ftHandle)
{
    if(ftHandle == FT601_HANDLE_LIBUSB) {
        fpga_close();
    } else {
        close((int)(uint64_t)ftHandle);
    }
    return 0;
}

__attribute__((visibility("default")))
uint32_t FT_GetChipConfiguration(void *ftHandle, void *pvConfiguration)
{
    if(ftHandle == FT601_HANDLE_LIBUSB) {
        return (fpga_get_chip_configuration(pvConfiguration) == -1) ? 0x20 : 0;
    } else {
        return ioctl((int)(uint64_t)ftHandle, 0, pvConfiguration) ? 0x20 : 0;
    }
}

__attribute__((visibility("default")))
uint32_t FT_SetChipConfiguration(void *ftHandle, void *pvConfiguration)
{
    if(ftHandle == FT601_HANDLE_LIBUSB) {
        return ioctl((int)(uint64_t)ftHandle, 1, pvConfiguration) ? 0x20 : 0;
    } else {
        return (fpga_set_chip_configuration(pvConfiguration) == -1) ? 0x20 : 0;
    }
}

__attribute__((visibility("default")))
uint32_t FT_SetSuspendTimeout(void *ftHandle, uint32_t Timeout)
{
    // dummy function, only here for compatibility in Linux case
    return 0;
}

__attribute__((visibility("default")))
uint32_t FT_AbortPipe(void *ftHandle, uint8_t ucPipeID)
{
    // dummy function, only here for compatibility in Linux case
    return 0;
}

__attribute__((visibility("default")))
uint32_t FT_WritePipe(void *ftHandle, uint8_t ucPipeID, uint8_t *pucBuffer, uint32_t ulBufferLength, uint32_t *pulBytesTransferred, void *pOverlapped)
{
    int result, cbTxTotal = 0;
    if(ftHandle == FT601_HANDLE_LIBUSB) {
        return (fpga_write(pucBuffer, ulBufferLength, pulBytesTransferred) == -1) ? 0x20 : 0;
    } else {
        // NB! underlying ft60x driver cannot handle more than 0x800 bytes per write,
        //     split larger writes into smaller writes if required.
        while(cbTxTotal < ulBufferLength) {
            result = write((int)(uint64_t)ftHandle, pucBuffer + cbTxTotal, min(0x800, ulBufferLength - cbTxTotal));
            if(!result) { return 0x20; } // no bytes transmitted -> error
            cbTxTotal += result;
        }
        *pulBytesTransferred = cbTxTotal;
        return 0;
    }
}

__attribute__((visibility("default")))
uint32_t FT_WritePipeEx(void *ftHandle, uint8_t ucPipeID, uint8_t *pucBuffer, uint32_t ulBufferLength, uint32_t *pulBytesTransferred, void *pOverlapped)
{
    return FT_WritePipe(ftHandle, ucPipeID, pucBuffer, ulBufferLength, pulBytesTransferred, pOverlapped);
}

uint32_t FT_ReadPipe2_KernelDriver(void *ftHandle, uint8_t ucPipeID, uint8_t *pucBuffer, uint32_t ulBufferLength, uint32_t *pulBytesTransferred, void *pOverlapped)
{
    int result;
    *pulBytesTransferred = 0;
    // NB! underlying driver have a max tranfer size in one go, multiple reads may be
    //     required to retrieve all data - hence the loop.
    do {
        result = read((int)(uint64_t)ftHandle, pucBuffer + *pulBytesTransferred, ulBufferLength - *pulBytesTransferred);
        if(result > 0) {
            *pulBytesTransferred += result;
        }
    } while((result > 0) && (0 == (result % 0x1000)) && (ulBufferLength > * pulBytesTransferred));
    return (result > 0) ? 0 : 0x20;
}

__attribute__((visibility("default")))
uint32_t FT_ReadPipe(void *ftHandle, uint8_t ucPipeID, uint8_t *pucBuffer, uint32_t ulBufferLength, uint32_t *pulBytesTransferred, void *pOverlapped)
{
    uint32_t i, result, cbRx, cbRxTotal = 0;
    if(ftHandle == FT601_HANDLE_LIBUSB) {
        return (fpga_read(pucBuffer, ulBufferLength, pulBytesTransferred) == -1) ? 0x20 : 0;
    } else {
        // NB! underlying driver won't return all data on the USB core queue in first
        //     read so we have to read two times.
        for(i = 0; i < 2; i++) {
            result = FT_ReadPipe2_KernelDriver(ftHandle, ucPipeID, pucBuffer + cbRxTotal, ulBufferLength - cbRxTotal, &cbRx, pOverlapped);
            cbRxTotal += cbRx;
        }
        *pulBytesTransferred = cbRxTotal;
        return result;
    }
}

__attribute__((visibility("default")))
uint32_t FT_ReadPipeEx(void *ftHandle, uint8_t ucPipeID, uint8_t *pucBuffer, uint32_t ulBufferLength, uint32_t *pulBytesTransferred, void *pOverlapped)
{
    return FT_ReadPipe(ftHandle, ucPipeID, pucBuffer, ulBufferLength, pulBytesTransferred, pOverlapped);
}
