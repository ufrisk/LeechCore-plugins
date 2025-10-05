
#ifndef __LEECHCORE_FT601_DRIVER_MACOS_H__
#define __LEECHCORE_FT601_DRIVER_MACOS_H__
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdint.h>
#include <unistd.h>

struct ft_handle;

__attribute__((visibility("default")))
uint32_t FT_Create(
    void *pvArg, 
    uint32_t dwFlags,
    struct ft_handle **pftHandle
);

__attribute__((visibility("default")))
uint32_t FT_Close(
    struct ft_handle *ftHandle
);

__attribute__((visibility("default")))
uint32_t FT_GetChipConfiguration(
    struct ft_handle *ftHandle,
    void *pvConfiguration
);

__attribute__((visibility("default")))
uint32_t FT_SetChipConfiguration(
    struct ft_handle *ftHandle,
    void *pvConfiguration
);

__attribute__((visibility("default")))
uint32_t FT_SetSuspendTimeout(
    struct ft_handle *ftHandle,
    uint32_t Timeout
);

__attribute__((visibility("default")))
uint32_t FT_AbortPipe(
    struct ft_handle *ftHandle,
    uint8_t ucPipeID
);

__attribute__((visibility("default")))
uint32_t FT_WritePipe(
    struct ft_handle *ftHandle,
    uint8_t ucPipeID,
    uint8_t *pucBuffer,
    uint32_t ulBufferLength,
    uint32_t *pulBytesTransferred,
    void *pOverlapped
);

__attribute__((visibility("default")))
uint32_t FT_WritePipeEx(
    struct ft_handle *ftHandle,
    uint8_t ucPipeID,
    uint8_t *pucBuffer,
    uint32_t ulBufferLength,
    uint32_t *pulBytesTransferred,
    void *pOverlapped
);

__attribute__((visibility("default")))
uint32_t FT_ReadPipe(
    struct ft_handle *ftHandle,
    uint8_t ucPipeID,
    uint8_t *pucBuffer,
    uint32_t ulBufferLength,
    uint32_t *pulBytesTransferred,
    void *pOverlapped
);

__attribute__((visibility("default")))
uint32_t FT_ReadPipeEx(
    struct ft_handle *ftHandle,
    uint8_t ucPipeID,
    uint8_t *pucBuffer,
    uint32_t ulBufferLength,
    uint32_t *pulBytesTransferred,
    void *pOverlapped
);

__attribute__((visibility("default")))
uint32_t FT_InitializeOverlapped(
    struct ft_handle *ftHandle,
    void *pOverlapped
);

__attribute__((visibility("default")))
uint32_t FT_ReleaseOverlapped(
    struct ft_handle *ftHandle,
    void *pOverlapped
);

__attribute__((visibility("default")))
uint32_t FT_GetOverlappedResult(
    struct ft_handle *ftHandle,
    void *pOverlapped,
    uint32_t *pulBytesTransferred,
    uint32_t bWait
);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* __LEECHCORE_FT601_DRIVER_MACOS_H__ */
