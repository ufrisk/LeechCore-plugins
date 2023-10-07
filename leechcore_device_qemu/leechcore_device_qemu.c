#include <fcntl.h>           /* For O_* constants */
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>        /* For mode constants */
#include <sys/un.h>

#include <leechcore_device.h>

typedef struct tdDEVICE_CONTEXT_QEMU {
    PBYTE pb;       // base address of memory mapped region
    SIZE_T cb;      // size of memory mapped region
} DEVICE_CONTEXT_QEMU, *PDEVICE_CONTEXT_QEMU;

#define QMP_BUFFER_SIZE 0x00100000      // 1MB

//-----------------------------------------------------------------------------
// GENERAL FUNCTIONALITY BELOW:
//-----------------------------------------------------------------------------

VOID DeviceQEMU_ReadScatter(_In_ PLC_CONTEXT ctxLC, _In_ DWORD cpMEMs, _Inout_ PPMEM_SCATTER ppMEMs)
{
    PDEVICE_CONTEXT_QEMU ctx = (PDEVICE_CONTEXT_QEMU)ctxLC->hDevice;
    PMEM_SCATTER pMEM;
    DWORD i;
    for(i = 0; i < cpMEMs; i++) {
        pMEM = ppMEMs[i];
        if(pMEM->f || MEM_SCATTER_ADDR_ISINVALID(pMEM)) { continue; }
        if(pMEM->qwA + pMEM->cb > ctx->cb) { continue; } 
        memcpy(pMEM->pb, ctx->pb + pMEM->qwA, pMEM->cb);
        pMEM->f = true;
    }
}

VOID DeviceQEMU_WriteScatter(_In_ PLC_CONTEXT ctxLC, _In_ DWORD cpMEMs, _Inout_ PPMEM_SCATTER ppMEMs)
{
    PDEVICE_CONTEXT_QEMU ctx = (PDEVICE_CONTEXT_QEMU)ctxLC->hDevice;
    PMEM_SCATTER pMEM;
    DWORD i;
    for(i = 0; i < cpMEMs; i++) {
        pMEM = ppMEMs[i];
        if(pMEM->f || MEM_SCATTER_ADDR_ISINVALID(pMEM)) { continue; }
        if(pMEM->qwA + pMEM->cb > ctx->cb) { continue; } 
        memcpy(ctx->pb + pMEM->qwA, pMEM->pb, pMEM->cb);
        pMEM->f = true;
    }
}

VOID DeviceQEMU_Close(_Inout_ PLC_CONTEXT ctxLC)
{
    PDEVICE_CONTEXT_QEMU ctx = (PDEVICE_CONTEXT_QEMU)ctxLC->hDevice;
    if(ctx) {
        ctxLC->hDevice = 0;
        if(ctx->pb) {
            munmap(ctx->pb, ctx->cb);
        }
        free(ctx);
    }
}

//-----------------------------------------------------------------------------
// QMP PARSE FUNCTIONALITY BELOW:
//-----------------------------------------------------------------------------

BOOL DeviceQEMU_QmpMemoryMap_Parse(_In_ PLC_CONTEXT ctxLC, _In_ LPSTR sz)
{
    QWORD paCurrent = 0, paBase = 0, paTop = 0, paRemap = 0;
    char *sze;

    while(true) {
        sz = strstr(sz, "\\r\\n  ");
        if(!sz) { break; }
        sz += 6;

        sze = strstr(sz, "\\r\\n  ");
        if(!sze) { break; }
        sze[0] = 0;

        if(strncmp(sz, "000000", 6)) { break; }

        if(!strncmp(sz + 13, "000-000000", 10) && strstr(sz, "ram)") && strstr(sz, " KVM")) {
            paBase = strtoull(sz, NULL, 16);
            paTop = strtoull(sz + 17, NULL, 16);
            if((paCurrent != 0) || (paBase != 0)) {
                paRemap = strtoull(strstr(sz, " KVM") - 16, NULL, 16);
            }

            if(paBase < paCurrent) { break; }
            if(paTop < paBase) { break; }
            if(paTop < paRemap) { break; }

            if(!paCurrent || paRemap) {
                LcMemMap_AddRange(ctxLC, paBase, paTop + 1 - paBase, paRemap);
            }

            paCurrent = paTop;
        }

        sze[0] = '\\';
    }

    return paCurrent > 0x01000000;
}

BOOL DeviceQEMU_QmpMemoryMap(_In_ PLC_CONTEXT ctxLC, _In_ LPSTR szPathQmp)
{
    bool f_result = false;
    int sock;
    struct sockaddr_un addr;
    char *buf = NULL, *sz;
    char *sz_command;

    // Initialize and connect to QMP socket:
    buf = malloc(QMP_BUFFER_SIZE);
    if(!buf) { goto fail; }
    memset(buf, 0, QMP_BUFFER_SIZE);

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) { goto fail; }
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, szPathQmp, sizeof(addr.sun_path) - 1);

    if(connect(sock, (struct sockaddr*) &addr, sizeof(struct sockaddr_un)) == -1) {
        goto fail;
    }

    // Initiate QMP capabilities negotiation and list memory regions:
    sz_command = "{\"execute\": \"qmp_capabilities\"}\n";
    write(sock, (void*)sz_command, strlen(sz_command));
    sz_command = "{\"execute\": \"human-monitor-command\", \"arguments\": { \"command-line\": \"info mtree -f\"} }\n";
    write(sock, (void*)sz_command, strlen(sz_command));
    sleep(1);
    if(read(sock, buf, QMP_BUFFER_SIZE - 0x1000) == -1) {
        goto fail;
    }

    // Parse retrieved memory regions:
    sz = strstr(buf, "Root memory region: system");
    if(!sz) { goto fail; }

    f_result = DeviceQEMU_QmpMemoryMap_Parse(ctxLC, sz);

fail:
    free(buf);
    close(sock);
    return f_result;
}

//-----------------------------------------------------------------------------
// INITIALIZATION FUNCTIONALITY BELOW:
//-----------------------------------------------------------------------------

_Success_(return) EXPORTED_FUNCTION
BOOL LcPluginCreate(_Inout_ PLC_CONTEXT ctxLC, _Out_opt_ PPLC_CONFIG_ERRORINFO ppLcCreateErrorInfo)
{
    PDEVICE_CONTEXT_QEMU ctx = NULL;
    PLC_DEVICE_PARAMETER_ENTRY pPathMem = NULL;
    PLC_DEVICE_PARAMETER_ENTRY pPathQmp = NULL;
    CHAR szPathMem[MAX_PATH] = { 0 }, szPathQmp[MAX_PATH] = { 0 };
    struct stat st;
    int fd, err;

    lcprintf(ctxLC, "DEVICE: QEMU: Initializing\n");

    // safety checks
    if(ppLcCreateErrorInfo) { *ppLcCreateErrorInfo = NULL; }
    if(ctxLC->version != LC_CONTEXT_VERSION) { return false; }

    // init context & parameters:
    ctx = (PDEVICE_CONTEXT_QEMU)malloc(sizeof(DEVICE_CONTEXT_QEMU));
    if(!ctx) { return false; }

    pPathMem = LcDeviceParameterGet(ctxLC, "shm");
    pPathQmp = LcDeviceParameterGet(ctxLC, "qmp");

    if(!pPathMem || !pPathMem->szValue[0] || (strlen(pPathMem->szValue) > MAX_PATH - 10)) {
        lcprintf(ctxLC, "DEVICE: QEMU: FAIL: Required parameter shm not given.\n");
        lcprintf(ctxLC, "   Example: qemu://shm=qemu-ram\n");
        goto fail;
    } else {
        strcat(szPathMem, "/dev/shm/");
        strcat(szPathMem, pPathMem->szValue);
    }


    if(!pPathQmp || !pPathQmp->szValue[0] || (strlen(pPathQmp->szValue) > MAX_PATH - 10)) {
        lcprintf(ctxLC, "DEVICE: QEMU: WARN: Optional parameter qmp not given.\n");
        lcprintf(ctxLC, "   Example: qemu://shm=qemu-ram,qmp=/tmp/qemu-qmp\n");
    } else {
        if(pPathQmp->szValue[0] != '/') {
            strcat(szPathQmp, "/tmp/");
        }
        strcat(szPathQmp, pPathQmp->szValue);
    }

    // open shared memory file
    err = stat(szPathMem, &st);
    if(err) {
        lcprintf(ctxLC, "DEVICE: QEMU: FAIL: 'stat' failed path='%s', errorcode=%i.\n", szPathMem, err);
        goto fail;
    }
    if(st.st_size % 0x1000) {
        lcprintf(ctxLC, "DEVICE: QEMU: FAIL: Shared memory not a multiple of 4096 bytes (page).\n");
        goto fail;
    }
    ctx->cb = st.st_size;

    fd = shm_open(pPathMem->szValue, O_RDWR | O_SYNC, 0);
    if(fd < 0) {
        lcprintf(ctxLC, "DEVICE: QEMU: FAIL: 'shm_open' failed path='%s', errorcode=%i.\n", szPathMem, fd);
        lcprintf(ctxLC, "  Possible reasons: no read/write access to shared memory file.\n");
        goto fail;
    }

    ctx->pb = mmap(NULL, ctx->cb, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(!ctx->pb) {
        lcprintf(ctxLC, "DEVICE: QEMU: FAIL: 'mmap' failed.\n");
        goto fail;
    }

    close(fd);

    // parse memory ranges using qmp (or heuristics as fallback)
    if(!szPathQmp[0] || !DeviceQEMU_QmpMemoryMap(ctxLC, szPathQmp)) {
        // qmp parsing of memory map failed - try guess fallback memory map:
        lcprintf(ctxLC, "DEVICE: QEMU: WARN: Trying fallback memory map. It's recommended to use QMP or manual memory map.\n");
        LcMemMap_AddRange(ctxLC, 0, ((ctx->cb > 0x80000000) ? 0x80000000 : ctx->cb), 0);
        if(ctx->cb > 0x80000000) {
            LcMemMap_AddRange(ctxLC, 0x100000000, ctx->cb - 0x80000000, 0x80000000);
        }
    }

    // finish:
    ctxLC->hDevice = (HANDLE)ctx;
    ctxLC->fMultiThread = true;
    ctxLC->Config.fVolatile = true;
    ctxLC->pfnClose = DeviceQEMU_Close;
    ctxLC->pfnReadScatter = DeviceQEMU_ReadScatter;
    ctxLC->pfnWriteScatter = DeviceQEMU_WriteScatter;
    return true;
fail:
    ctxLC->hDevice = (HANDLE)ctx;
    DeviceQEMU_Close(ctxLC);
    return false;
}
