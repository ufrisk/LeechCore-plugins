/*
 * Author: Rick Wertenbroek
 */
#include <stdbool.h>
#include <limits.h>

#include <leechcore_device.h>

/* Almost empty context */
typedef struct tdDEVICE_CONTEXT_SKELETON {
    SIZE_T cb; /* Size of memory region (starts at 0) */
} DEVICE_CONTEXT_SKELETON, *PDEVICE_CONTEXT_SKELETON;

VOID DeviceSKELETON_ReadScatter(_In_ PLC_CONTEXT ctxLC, _In_ DWORD cpMEMs, _Inout_ PPMEM_SCATTER ppMEMs)
{
    PDEVICE_CONTEXT_SKELETON ctx = (PDEVICE_CONTEXT_SKELETON)ctxLC->hDevice;
    PMEM_SCATTER pMEM;
    DWORD i;

    for(i = 0; i < cpMEMs; i++) {
        pMEM = ppMEMs[i];
        lcprintf(ctxLC, "SKELETON: Address of memory to read : %#016llx, #bytes : %u, data in pb : %s\n", pMEM->qwA, pMEM->cb, (pMEM->f ? "yes" : "no"));
        if(pMEM->f || MEM_SCATTER_ADDR_ISINVALID(pMEM)) {
            lcprintf(ctxLC, "SKELETON: ERROR: pMEM->f is set or invalid address\n");
            continue;
        }
        if(pMEM->qwA + pMEM->cb > ctx->cb) {
            lcprintf(ctxLC, "SKELETON: ERROR: OOB access\n");
            continue;
        }
        /* Do the actual transfer here e.g., :
        memcpy(pMEM->pb, <source memory base> + pMEM->qwA, pMEM->cb);
        */
        pMEM->f = true;
    }
}

VOID DeviceSKELETON_WriteScatter(_In_ PLC_CONTEXT ctxLC, _In_ DWORD cpMEMs, _Inout_ PPMEM_SCATTER ppMEMs)
{
    PDEVICE_CONTEXT_SKELETON ctx = (PDEVICE_CONTEXT_SKELETON)ctxLC->hDevice;
    PMEM_SCATTER pMEM;
    DWORD i;
    for(i = 0; i < cpMEMs; i++) {
        pMEM = ppMEMs[i];
        lcprintf(ctxLC, "SKELETON: Address of memory to write : %#016llx, #bytes : %u, data in pb : %s\n", pMEM->qwA, pMEM->cb, (pMEM->f ? "yes" : "no"));
        if(pMEM->f || MEM_SCATTER_ADDR_ISINVALID(pMEM)) {
            lcprintf(ctxLC, "SKELETON: ERROR: pMEM->f is set or invalid address\n");
            continue;
        }
        if(pMEM->qwA + pMEM->cb > ctx->cb) {
            lcprintf(ctxLC, "SKELETON: ERROR: OOB access\n");
            continue;
        }
        /* Do the actual transfer here e.g., :
        memcpy(<destination memory base> + pMEM->qwA, pMEM->pb, pMEM->cb);
        */
        pMEM->f = true;
    }
}

VOID DeviceSKELETON_Close(_Inout_ PLC_CONTEXT ctxLC)
{
    PDEVICE_CONTEXT_SKELETON ctx = (PDEVICE_CONTEXT_SKELETON)ctxLC->hDevice;
    if(ctx) {
        ctxLC->hDevice = 0;
        free(ctx);
    }
}

_Success_(return) EXPORTED_FUNCTION
BOOL LcPluginCreate(_Inout_ PLC_CONTEXT ctxLC, _Out_opt_ PPLC_CONFIG_ERRORINFO ppLcCreateErrorInfo)
{
    int ret = 0;
    PDEVICE_CONTEXT_SKELETON ctx = NULL;
    PLC_DEVICE_PARAMETER_ENTRY pDevParameter = NULL;
    PLC_DEVICE_PARAMETER_ENTRY pSizeParameter = NULL;

    lcprintf(ctxLC, "DEVICE: SKELETON: Initializing\n");

    /* Sanity checks */
    if(ppLcCreateErrorInfo) { *ppLcCreateErrorInfo = NULL; }
    if(ctxLC->version != LC_CONTEXT_VERSION) { return false; }

    /* Allocate the context */
    ctx = (PDEVICE_CONTEXT_SKELETON)malloc(sizeof(DEVICE_CONTEXT_SKELETON));
    if(!ctx) { return false; }

    /* Parse parameters */
    pDevParameter = LcDeviceParameterGet(ctxLC, "dev");

    if(!pDevParameter) {
        lcprintf(ctxLC, "SKELETON: ERROR: Required parameter \"dev\" not given.\n");
        lcprintf(ctxLC, "   Example: skeleton://dev=<value>\n");
        goto fail;
    }

    lcprintf(ctxLC, "Dev parameter is %s\n", pDevParameter->szValue);

    pSizeParameter = LcDeviceParameterGet(ctxLC, "size");

    if (pSizeParameter) {
#ifdef _WIN32
        ret = sscanf_s(pSizeParameter->szValue, "%zu", &ctx->cb);
#else /* _WIN32 */
        ret = sscanf(pSizeParameter->szValue, "%zu", &ctx->cb);
#endif /* _WIN32 */
        if (ret <= 0) {
            lcprintf(ctxLC, "SKELETON: ERROR: Failed to read \"size\" parameter\n");
            goto fail;
        }
    } else {
        /* Default to 4GB */
        ctx->cb = UINT_MAX;
    }

    /* Assign info and handles for LeechCore */
    ctxLC->hDevice = (HANDLE)ctx;
    ctxLC->fMultiThread = false;
    ctxLC->Config.fVolatile = true;
    ctxLC->pfnClose = DeviceSKELETON_Close;
    ctxLC->pfnReadScatter = DeviceSKELETON_ReadScatter;
    ctxLC->pfnWriteScatter = DeviceSKELETON_WriteScatter;
    return true;
fail:
    /* Sad ... */
    ctxLC->hDevice = (HANDLE)ctx;
    DeviceSKELETON_Close(ctxLC);
    return false;
}
