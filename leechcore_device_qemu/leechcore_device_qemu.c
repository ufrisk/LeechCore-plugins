#include <stdbool.h>

#include <leechcore_device.h>
#include "qemu_driver.h"

#define PLUGIN_URL_SCHEME "qemu://"

static VOID DeviceQemu_ReadContigious(PLC_READ_CONTIGIOUS_CONTEXT ctxRC) {

    // read contiguous physical memory
    PLC_CONTEXT ctxLC = ctxRC->ctxLC;
    uint64_t bytes_read = 0;
    void *qemu_ram = ctxLC->hDevice;
    if (!qemu_read_physical(qemu_ram, ctxRC->paBase, ctxRC->pb, ctxRC->cb, &bytes_read)) {
        lcprintfvvv(ctxLC, "Failed to read physical memory at 0x%llx\n", ctxRC->paBase);
    }
    ctxRC->cbRead = (DWORD)bytes_read;
}

static BOOL DeviceQemu_WriteContigious(_In_ PLC_CONTEXT ctxLC, _In_ QWORD qwAddr, _In_ DWORD cb, _In_reads_(cb) PBYTE pb) {

    // write contiguous memory
    void *qemu_ram = ctxLC->hDevice;
    if (!qemu_write_physical(qemu_ram, qwAddr, pb, cb)) {
        lcprintfvvv(ctxLC, "Failed to write %d bytes in physical memory at 0x%llx\n", cb, qwAddr);
        return false;
    }
    return true;
}

static VOID DeviceQemu_Close(_Inout_ PLC_CONTEXT ctxLC) {
    
    // close driver
    void *qemu_ram = ctxLC->hDevice;
    qemu_destroy(qemu_ram);
}

_Success_(return ) EXPORTED_FUNCTION BOOL LcPluginCreate(_Inout_ PLC_CONTEXT ctxLC, _Out_opt_ PPLC_CONFIG_ERRORINFO ppLcCreateErrorInfo) {


    const char * mem_path = "qemu-ram"; // FIXME we should get this from params
    const char * init_error = NULL;

    lcprintf(ctxLC, "QEMU: Initializing\n");

    // safety checks
    if (ppLcCreateErrorInfo) { *ppLcCreateErrorInfo = NULL; }
    
    if(ctxLC->version != LC_CONTEXT_VERSION) { 
	return false; 
    }
    
    void *qemu_ram = qemu_init(mem_path);
    if (!qemu_ram) {
        lcprintf(ctxLC, "QEMU: initialization failed: %s.\n", init_error);
        goto fail;
    }

    // assign context
    ctxLC->hDevice = (HANDLE)qemu_ram;
    
    // setup config
    ctxLC->Config.fVolatile = true;

    // set max physical address
    uint64_t max_addr = 0;
    if (!qemu_get_max_physical_addr(qemu_ram, &max_addr)) {
        lcprintf(ctxLC, "Failed to get max physical address\n");
        goto fail;
    }

    lcprintfvv(ctxLC, "QEMU: max physical address: 0x%lx\n", max_addr);
    ctxLC->Config.paMax = max_addr;

    // set callback functions
    ctxLC->pfnReadContigious = DeviceQemu_ReadContigious;
    ctxLC->pfnWriteContigious = DeviceQemu_WriteContigious;
    ctxLC->pfnClose = DeviceQemu_Close;

    // status
    lcprintf(ctxLC, "QEMU: initialized.\n");
    return true;

fail:
    DeviceQemu_Close(ctxLC);
    return false;
}


