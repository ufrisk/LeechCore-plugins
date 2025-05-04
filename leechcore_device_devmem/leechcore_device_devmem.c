/*
* Author: Brendan Heinonen
*/
#include "leechcore.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <limits.h>

#include <leechcore_device.h>
#include <unistd.h>


static VOID DeviceDevmem_ReadContigious(PLC_READ_CONTIGIOUS_CONTEXT ctxRC) {
    int bytes_read;
    int fd = (intptr_t)ctxRC->ctxLC->hDevice;

    lseek(fd, ctxRC->paBase, SEEK_SET);
    if ((bytes_read = read(fd, ctxRC->pb, ctxRC->cb)) < 0) {
        lcprintfvvv(ctxRC->ctxLC, "Failed to read physical memory at 0x%llx (error %d)\n",
                    ctxRC->paBase, bytes_read);
    }
    ctxRC->cbRead = (DWORD)bytes_read;
}

static BOOL DeviceDevmem_WriteContigious(_In_ PLC_CONTEXT ctxLC,
                                           _In_ QWORD qwAddr, _In_ DWORD cb,
                                           _In_reads_(cb) PBYTE pb) {
    int bytes_written;
    int fd = (intptr_t)ctxLC->hDevice;
    lseek(fd, qwAddr, SEEK_SET);
    if ((bytes_written = write(fd, pb, cb)) < 0) {
        lcprintfvvv(ctxLC, "Failed to write physical memory at 0x%llx (error %d)\n",
                    qwAddr, bytes_written);
        return false;
    }
    return true;
}

VOID DeviceDevmem_Close(_Inout_ PLC_CONTEXT ctxLC)
{
    close((intptr_t)ctxLC->hDevice);
}

_Success_(return) EXPORTED_FUNCTION
BOOL LcPluginCreate(_Inout_ PLC_CONTEXT ctxLC, _Out_opt_ PPLC_CONFIG_ERRORINFO ppLcCreateErrorInfo)
{
    int ret = 0;
    PLC_DEVICE_PARAMETER_ENTRY pPathParameter = NULL;
    CHAR szPath[MAX_PATH];
    int fd;

    lcprintf(ctxLC, "DEVICE: devmem: Initializing\n");

    /* Sanity checks */
    if(ppLcCreateErrorInfo) { *ppLcCreateErrorInfo = NULL; }
    if(ctxLC->version != LC_CONTEXT_VERSION) { return false; }

    /* Parse path parameter, or default to /dev/mem */
    if((pPathParameter = LcDeviceParameterGet(ctxLC, "path"))) {
        strncpy(szPath, pPathParameter->szValue, sizeof(szPath));
    } else {
        strncpy(szPath, "/dev/mem", sizeof(szPath));
    }

    /* Open the device */
    fd = open(szPath, O_RDWR | O_SYNC);
    if(fd < 0) {
        lcprintf(ctxLC, "DEVICE: devmem: Failed to open device %s (error %d)\n", szPath, errno);
        return false;
    }

    /* Assign info and handles for LeechCore */
    ctxLC->hDevice = (HANDLE)(intptr_t)fd;
    ctxLC->fMultiThread = false;
    ctxLC->Config.fVolatile = true;
    ctxLC->pfnClose = DeviceDevmem_Close;
    ctxLC->pfnReadContigious = DeviceDevmem_ReadContigious;
    ctxLC->pfnWriteContigious = DeviceDevmem_WriteContigious;
    return true;
}
