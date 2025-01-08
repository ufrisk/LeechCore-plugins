// leechcore_ft601_driver_macos.c : implementation of the FT601 wrapper library for the FPGA D3XX driver.
//
// (c) Ulf Frisk, 2025
// Author: Ulf Frisk, pcileech@frizk.net
//

#include "leechcore_ft601_driver_macos.h"
#include "fpga_ft601.h"
#include <fcntl.h>
#include <stdlib.h>

#define FT_OK                       0
#define FT_NOT_SUPPORTED            17
#define FT_IO_PENDING               24
#define FT_OTHER_ERROR              32
#define FT_OPEN_BY_INDEX            0x10

struct ft_handle {
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
    // try ft601 driver
    {
        fth->handle = fpga_open(pvArg, dwFlags);
        if(fth->handle) {
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
    if(fth) {
        fpga_close(fth->handle);
        free(fth);
    }
    return 0;
}

__attribute__((visibility("default")))
uint32_t FT_GetChipConfiguration(struct ft_handle *fth, void *pvConfiguration)
{
    return fpga_get_chip_configuration(fth->handle, pvConfiguration);
}

__attribute__((visibility("default")))
uint32_t FT_SetChipConfiguration(struct ft_handle *fth, void *pvConfiguration)
{
    return fpga_set_chip_configuration(fth->handle, pvConfiguration);
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
    return fpga_write(fth->handle, pucBuffer, ulBufferLength, pulBytesTransferred);
}

__attribute__((visibility("default")))
uint32_t FT_WritePipeEx(struct ft_handle *fth, uint8_t ucPipeID, uint8_t *pucBuffer, uint32_t ulBufferLength, uint32_t *pulBytesTransferred, void *pOverlapped)
{
    return FT_WritePipe(fth, ucPipeID, pucBuffer, ulBufferLength, pulBytesTransferred, pOverlapped);
}

__attribute__((visibility("default")))
uint32_t FT_ReadPipe(struct ft_handle *fth, uint8_t ucPipeID, uint8_t *pucBuffer, uint32_t ulBufferLength, uint32_t *pulBytesTransferred, void *pOverlapped)
{
    if(pOverlapped) {
        return (fpga_async_read(fth->handle, pucBuffer, ulBufferLength) == -1) ? FT_OTHER_ERROR : FT_IO_PENDING;
    } else {
        return fpga_read(fth->handle, pucBuffer, ulBufferLength, pulBytesTransferred);
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
    return fpga_async_init(fth->handle) ? FT_OTHER_ERROR : FT_OK;
}

__attribute__((visibility("default")))
uint32_t FT_ReleaseOverlapped(struct ft_handle *fth, void *pOverlapped)
{
    return fpga_async_close(fth->handle) ? FT_OTHER_ERROR : FT_OK;
}

__attribute__((visibility("default")))
uint32_t FT_GetOverlappedResult(struct ft_handle *fth, void *pOverlapped, uint32_t *pulBytesTransferred, uint32_t bWait)
{
    return fpga_async_result(fth->handle, pulBytesTransferred) ? FT_OTHER_ERROR : FT_OK;
}
