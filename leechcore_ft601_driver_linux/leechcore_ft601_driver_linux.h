// The LeechCore FT601 driver for Linux:
//
// The LeechCore FT601 driver allows programs that include this library to
// communicate with a FT601 device over USB3. The use of this driver is not
// limited to LeechCore, other programs may also include this driver if only
// a subset of the FT601 API is required.
//
// The driver first tries to use the LambdaConcept kernel driver .ko for FT601
// if it should be found to be loaded into the kernel _and_ that this library
// have read/write access to the driver device at /dev/ft60x[0-3]
// For more information about the LambdaConcept kernel driver for FT601 please
// see: https://github.com/lambdaconcept/ft60x_driver
//
// If the LambdaConcept kernel driver is not found on the system this library
// will use a built-in driver that use libusb in the backend.
// The libusb driver (fpga_libusb) is:
//    Contributed by J�r�mie Boutoille from Synacktiv - www.synacktiv.com
//    Based in part on PCIeScreamer kernel driver from LambdaConcept.
//
// This driver, both kernel driver and built-in libusb backed driver have been
// tested on various FT601 FPGA boards suppodted by the PCILeech/LeechCore
// projects. These boards include:
//     - Xilinx SP605 dev board flashed with PCILeech bitstream and FTDI UMFT601X-B addon-board.
//     - Xilinx AC701 dev board flashed with PCILeech bitstream and FTDI UMFT601X-B addon-board.
//     - PCIeScreamer board flashed with PCILeech bitstream.
//
//

#ifndef __LEECHCORE_FT601_DRIVER_LINUX_H__
#define __LEECHCORE_FT601_DRIVER_LINUX_H__
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <libusb.h>

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
#endif /* __LEECHCORE_FT601_DRIVER_LINUX_H__ */
