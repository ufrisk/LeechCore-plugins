#include <dirent.h>
#include <fcntl.h>           /* For O_* constants */
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>        /* For mode constants */
#include <sys/un.h>

#include <leechcore_device.h>

typedef struct tdDEVICE_CONTEXT_QEMU {
    PBYTE pb;                   // base address of memory mapped region
    SIZE_T cb;                  // size of memory mapped region
    BOOL fDelay;                // delay reads with tmnsDelayRead / tmnsDelayLatency ns.
    QWORD tmnsDelayLatency;     // optional delay in ns applied once per read
    QWORD tmnsDelayReadPage;    // optional delay in ns applied per read page
} DEVICE_CONTEXT_QEMU, *PDEVICE_CONTEXT_QEMU;

#define QMP_BUFFER_SIZE 0x00100000      // 1MB
#define HUGEPAGES_PATH "/dev/hugepages/"

//-----------------------------------------------------------------------------
// GENERAL FUNCTIONALITY BELOW:
//-----------------------------------------------------------------------------

/*
* Helper function to manage delays used for FPGA emulation.
* -- ptmStart = the start time.
* -- tmDelay = the delay .
*/
VOID DeviceQEMU_Delay(_In_ struct timespec *ptmStart, _In_ QWORD tmDelay)
{
    struct timespec tmNow;
    while(!clock_gettime(CLOCK_MONOTONIC, &tmNow) && ((QWORD)ptmStart->tv_nsec + tmDelay > (QWORD)tmNow.tv_nsec) && (ptmStart->tv_nsec < tmNow.tv_nsec)) {
        ;
    }
}

VOID DeviceQEMU_ReadScatter(_In_ PLC_CONTEXT ctxLC, _In_ DWORD cpMEMs, _Inout_ PPMEM_SCATTER ppMEMs)
{
    PDEVICE_CONTEXT_QEMU ctx = (PDEVICE_CONTEXT_QEMU)ctxLC->hDevice;
    struct timespec tmStart;
    PMEM_SCATTER pMEM;
    DWORD i;
    if(ctx->fDelay) {
        clock_gettime(CLOCK_MONOTONIC, &tmStart);
    }
    for(i = 0; i < cpMEMs; i++) {
        pMEM = ppMEMs[i];
        if(pMEM->f || MEM_SCATTER_ADDR_ISINVALID(pMEM)) { continue; }
        if(pMEM->qwA + pMEM->cb > ctx->cb) { continue; } 
        memcpy(pMEM->pb, ctx->pb + pMEM->qwA, pMEM->cb);
        pMEM->f = true;
    }
    if(ctx->fDelay) {
        DeviceQEMU_Delay(&tmStart, ctx->tmnsDelayLatency + ctx->tmnsDelayReadPage * cpMEMs);
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
    char *sze, *szr;

    while(true) {
        sz = strstr(sz, "\\r\\n  ");
        if(!sz) { break; }
        sz += 6;

        sze = strstr(sz, "\\r\\n  ");
        if(!sze) { break; }
        sze[0] = 0;

        if(strncmp(sz, "000000", 6)) { break; }

        if(!strncmp(sz + 13, "000-000000", 10) && strstr(sz, "ram)") && (strstr(sz, " KVM") || strstr(sz, " qemu-ram"))) {
            paBase = strtoull(sz, NULL, 16);
            paTop = strtoull(sz + 17, NULL, 16);
            if((paCurrent != 0) || (paBase != 0)) {
                if((szr = strstr(sz, " KVM"))) {
                    if(szr - sz < 16) { break; }
                    paRemap = strtoull(strstr(sz, " KVM") - 16, NULL, 16);
                }
                if((szr = strstr(sz, " qemu-ram"))) {
                    if(strlen(szr) < 11) { break; }
                    paRemap = strtoull(strstr(sz, " qemu-ram") + 11, NULL, 16);
                }
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
    char *buf = NULL, *sz, *szdup = NULL;
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
        lcprintf(ctxLC, "DEVICE: QEMU: WARN: QMP: Unable to connect to socket.\n");
        goto fail;
    }

    // Initiate QMP capabilities negotiation and list memory regions:
    sz_command = "{\"execute\": \"qmp_capabilities\"}\n";
    write(sock, (void*)sz_command, strlen(sz_command));
    sz_command = "{\"execute\": \"human-monitor-command\", \"arguments\": { \"command-line\": \"info mtree -f\"} }\n";
    write(sock, (void*)sz_command, strlen(sz_command));
    sleep(1);
    if(read(sock, buf, QMP_BUFFER_SIZE - 0x1000) == -1) {
        lcprintf(ctxLC, "DEVICE: QEMU: WARN: QMP: Unable to read from socket.\n");
        goto fail;
    }

    // Parse retrieved memory regions:
    sz = strstr(buf, "Root memory region: system");
    if(!sz) {
        lcprintf(ctxLC, "DEVICE: QEMU: WARN: QMP: Unable to parse memory regions #1.\n");
        goto fail;
    }

    szdup = strdup(sz);
    f_result = DeviceQEMU_QmpMemoryMap_Parse(ctxLC, sz);
    if(!f_result) {
        lcprintf(ctxLC, "DEVICE: QEMU: WARN: QMP: Unable to parse memory regions #2.\n");
        lcprintfvv(ctxLC, "\n\n%s\n\n", szdup);
    }
fail:
    free(buf);
    free(szdup);
    close(sock);
    return f_result;
}

//-----------------------------------------------------------------------------
// INITIALIZATION FUNCTIONALITY BELOW:
//-----------------------------------------------------------------------------

_Success_(return)
BOOL LcPluginCreate_Shm(PLC_CONTEXT ctxLC, _In_ PDEVICE_CONTEXT_QEMU ctx, _In_ PLC_DEVICE_PARAMETER_ENTRY pPathShm)
{
    int fd, err;
    struct stat st;
    CHAR szPathMem[MAX_PATH] = { 0 };

    if(!pPathShm || !pPathShm->szValue[0] || (strlen(pPathShm->szValue) > MAX_PATH - 10)) {
        lcprintf(ctxLC, "DEVICE: QEMU: FAIL: Required parameter shm not given.\n");
        lcprintf(ctxLC, "   Example: qemu://shm=qemu-ram\n");
        goto fail;
    } else {
        strcat(szPathMem, "/dev/shm/");
        strcat(szPathMem, pPathShm->szValue);
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

    fd = shm_open(pPathShm->szValue, O_RDWR | O_SYNC, 0);
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
    return true;

fail:
    return false;
}

_Success_(return)
BOOL LcPluginCreate_HugePages(PLC_CONTEXT ctxLC, _In_ PDEVICE_CONTEXT_QEMU ctx, _In_ QWORD qwHugePagePid)
{
    
    DIR *fdDir;
    int fd, err;
    struct stat st;
    struct dirent *dp;
    CHAR szPathMem[MAX_PATH] = { 0 }, szPathQemuFdDir[MAX_PATH] = { 0 };

    snprintf(szPathQemuFdDir, sizeof(szPathQemuFdDir), "/proc/%llu/fd/", qwHugePagePid);

    fdDir = opendir(szPathQemuFdDir);
    if(!fdDir) {
        lcprintf(ctxLC, "DEVICE: QEMU: Failed to open qemu hugepage fd path.\n");
        lcprintf(ctxLC, "DEVICE: QEMU: Check path and permissions for path: %s\n", szPathQemuFdDir);
        goto fail;
    }

    while ((dp = readdir(fdDir)) != NULL)
    {
        CHAR szPathQemuFd[MAX_PATH] = { 0 };
        CHAR szPathQemuFdReal[MAX_PATH] = { 0 };

        if((strcmp(".", dp->d_name) == 0) || (strcmp("..", dp->d_name) == 0)) {
            continue;
        }

        strcat(szPathQemuFd, szPathQemuFdDir);
        strcat(szPathQemuFd, dp->d_name);

        if(readlink(szPathQemuFd, szPathQemuFdReal, sizeof(szPathQemuFdReal)) == -1) {
            continue;
        }

        if(strncmp(HUGEPAGES_PATH, szPathQemuFdReal, sizeof(HUGEPAGES_PATH) -1) == 0) {
            strcpy(szPathMem, szPathQemuFd);
            break;
        }
    }

    closedir(fdDir);

    fd = open(szPathMem, O_RDWR | O_SYNC, 0);
    if(fd < 0) {
        lcprintf(ctxLC, "DEVICE: QEMU: FAIL: 'open' failed path='%s', errorcode=%i.\n", szPathMem, fd);
        lcprintf(ctxLC, "  Possible reasons: no read/write access to hugepage memory file.\n");
        goto fail;
    }

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

    ctx->pb = mmap(NULL, ctx->cb, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(!ctx->pb) {
        lcprintf(ctxLC, "DEVICE: QEMU: FAIL: 'mmap' failed.\n");
        goto fail;
    }

    close(fd);
    return true;

fail:
    return false;
}

_Success_(return) EXPORTED_FUNCTION
BOOL LcPluginCreate(_Inout_ PLC_CONTEXT ctxLC, _Out_opt_ PPLC_CONFIG_ERRORINFO ppLcCreateErrorInfo)
{
    PDEVICE_CONTEXT_QEMU ctx = NULL;
    PLC_DEVICE_PARAMETER_ENTRY pPathShm = NULL;
    PLC_DEVICE_PARAMETER_ENTRY pPathQmp = NULL;
    CHAR szPathQmp[MAX_PATH] = { 0 };
    QWORD qwHugePagePid;

    lcprintf(ctxLC, "DEVICE: QEMU: Initializing\n");

    // safety checks
    if(ppLcCreateErrorInfo) { *ppLcCreateErrorInfo = NULL; }
    if(ctxLC->version != LC_CONTEXT_VERSION) { return false; }

    // init context & parameters:
    ctx = (PDEVICE_CONTEXT_QEMU)malloc(sizeof(DEVICE_CONTEXT_QEMU));
    if(!ctx) { return false; }

    qwHugePagePid = LcDeviceParameterGetNumeric(ctxLC, "hugepage-pid");
    pPathShm = LcDeviceParameterGet(ctxLC, "shm");
    pPathQmp = LcDeviceParameterGet(ctxLC, "qmp");

    ctx->tmnsDelayLatency = LcDeviceParameterGetNumeric(ctxLC, "delay-latency-ns");
    ctx->tmnsDelayReadPage = LcDeviceParameterGetNumeric(ctxLC, "delay-readpage-ns");
    ctx->fDelay = (ctx->tmnsDelayLatency > 0) || (ctx->tmnsDelayReadPage > 0);

    if(!qwHugePagePid && !pPathShm) {
        lcprintf(ctxLC, "DEVICE: QEMU: FAIL: Required parameter shm or hugepages-pid not given.\n");
        lcprintf(ctxLC, "   Example: qemu://hugepage-pid=<pid>\n");
        lcprintf(ctxLC, "   Example: qemu://shm=qemu-ram\n");
        goto fail;
    }

    // create with shared memory SHM or HugePages QEMU PID
    if(pPathShm && !LcPluginCreate_Shm(ctxLC, ctx, pPathShm)) {
        goto fail;
    }
    if(qwHugePagePid && !LcPluginCreate_HugePages(ctxLC, ctx, qwHugePagePid)) {
        goto fail;
    }

    // parse memory ranges using qmp (or heuristics as fallback)
    if(!pPathQmp || !pPathQmp->szValue[0] || (strlen(pPathQmp->szValue) > MAX_PATH - 10)) {
        lcprintf(ctxLC, "DEVICE: QEMU: WARN: Optional parameter qmp not given.\n");
        lcprintf(ctxLC, "   Example: qemu://hugepage-pid=<pid>,qmp=/tmp/qemu-qmp\n");
        lcprintf(ctxLC, "   Example: qemu://shm=qemu-ram,qmp=/tmp/qemu-qmp\n");
    } else {
        if(pPathQmp->szValue[0] != '/') {
            strcat(szPathQmp, "/tmp/");
        }
        strcat(szPathQmp, pPathQmp->szValue);
    }

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
