#include <dirent.h>
#include <errno.h>
#include <poll.h>
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
#define QMP_TIMEOUT_MS 5000
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

_Success_(return)
BOOL DeviceQEMU_QmpMemoryMap_Parse(_Inout_ PLC_CONTEXT ctxLC, _Inout_ LPSTR sz)
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
                if(!LcMemMap_AddRange(ctxLC, paBase, paTop + 1 - paBase, paRemap)) { return false; }
            }

            paCurrent = paTop;
        }

        sze[0] = '\\';
    }

    return paCurrent > 0x01000000;
}

_Success_(return != 0)
static QWORD DeviceQEMU_QmpTimeMs(void)
{
    struct timespec tm;
    if(clock_gettime(CLOCK_MONOTONIC, &tm)) { return 0; }
    return (QWORD)tm.tv_sec * 1000 + tm.tv_nsec / 1000000;
}

_Success_(return)
static BOOL DeviceQEMU_QmpWait(_In_ int sock, _In_ BOOL fWrite, _In_ QWORD tmDeadline)
{
    struct pollfd pfd = { 0 };
    QWORD tmNow;
    int r;
    pfd.fd = sock;
    pfd.events = fWrite ? POLLOUT : POLLIN;
    while((tmNow = DeviceQEMU_QmpTimeMs()) && (tmNow < tmDeadline)) {
        r = poll(&pfd, 1, (int)(tmDeadline - tmNow));
        if(r > 0) { return (pfd.revents & pfd.events) != 0; }
        if((r == 0) || (errno != EINTR)) { return false; }
    }
    return false;
}

_Success_(return)
static BOOL DeviceQEMU_QmpSend(_In_ int sock, _In_ LPCSTR sz)
{
    QWORD tmDeadline = DeviceQEMU_QmpTimeMs() + QMP_TIMEOUT_MS;
    SIZE_T cb = strlen(sz);
    ssize_t cbWrite;
    while(cb) {
        if(!DeviceQEMU_QmpWait(sock, true, tmDeadline)) { return false; }
        cbWrite = send(sock, sz, cb, MSG_NOSIGNAL);
        if(cbWrite < 0) {
            if((errno == EINTR) || (errno == EAGAIN) || (errno == EWOULDBLOCK)) { continue; }
            return false;
        }
        if(!cbWrite) { return false; }
        sz += cbWrite;
        cb -= cbWrite;
    }
    return true;
}

// Locate a top-level JSON field, skipping quoted strings and nested objects.
_Success_(return != NULL)
static LPCSTR DeviceQEMU_QmpField(_In_ LPCSTR sz, _In_ LPCSTR szKey)
{
    LPCSTR szStart;
    SIZE_T cchKey = strlen(szKey);
    int cDepth = 0;
    while(*sz) {
        if(*sz == '"') {
            szStart = ++sz;
            while(*sz && (*sz != '"')) {
                if((*sz == '\\') && sz[1]) { sz++; }
                sz++;
            }
            if(!*sz) { return NULL; }
            if((cDepth == 1) && ((SIZE_T)(sz - szStart) == cchKey) && !memcmp(szStart, szKey, cchKey)) {
                sz++;
                while((*sz == ' ') || (*sz == '\t') || (*sz == '\r')) { sz++; }
                if(*sz == ':') {
                    sz++;
                    while((*sz == ' ') || (*sz == '\t') || (*sz == '\r')) { sz++; }
                    return sz;
                }
                continue;
            }
        } else if((*sz == '{') || (*sz == '[')) {
            cDepth++;
        } else if((*sz == '}') || (*sz == ']')) {
            cDepth--;
        }
        sz++;
    }
    return NULL;
}

// QMP messages are newline-delimited JSON. Keep unconsumed bytes for the next reply.
_Success_(return)
static BOOL DeviceQEMU_QmpReceive(_In_ int sock, _Inout_bytecount_(QMP_BUFFER_SIZE) LPSTR sz, _Inout_ SIZE_T *pcb, _In_ DWORD dwId)
{
    QWORD tmDeadline = DeviceQEMU_QmpTimeMs() + QMP_TIMEOUT_MS;
    LPSTR sze;
    LPCSTR szId;
    SIZE_T cbLine;
    ssize_t cbRead;
    while(true) {
        sz[*pcb] = 0;
        while((sze = memchr(sz, '\n', *pcb))) {
            cbLine = sze - sz + 1;
            *sze = 0;
            if(!dwId && DeviceQEMU_QmpField(sz, "QMP")) { return true; }
            szId = DeviceQEMU_QmpField(sz, "id");
            if(dwId && szId && (strtoul(szId, &sze, 10) == dwId) && ((sze[0] == ',') || (sze[0] == '}') || (sze[0] == ' '))) {
                return DeviceQEMU_QmpField(sz, "return") && !DeviceQEMU_QmpField(sz, "error");
            }
            memmove(sz, sz + cbLine, *pcb - cbLine);
            *pcb -= cbLine;
            sz[*pcb] = 0;
        }
        if((*pcb == QMP_BUFFER_SIZE - 1) || !DeviceQEMU_QmpWait(sock, false, tmDeadline)) { return false; }
        cbRead = recv(sock, sz + *pcb, QMP_BUFFER_SIZE - 1 - *pcb, 0);
        if(cbRead < 0) {
            if((errno == EINTR) || (errno == EAGAIN) || (errno == EWOULDBLOCK)) { continue; }
            return false;
        }
        if(!cbRead) { return false; }
        *pcb += cbRead;
    }
}

static VOID DeviceQEMU_QmpConsume(_Inout_bytecount_(QMP_BUFFER_SIZE) LPSTR sz, _Inout_ SIZE_T *pcb)
{
    SIZE_T cbLine = strlen(sz) + 1;
    memmove(sz, sz + cbLine, *pcb - cbLine);
    *pcb -= cbLine;
}

_Success_(return)
BOOL DeviceQEMU_QmpMemoryMap(_In_ PLC_CONTEXT ctxLC, _In_ LPSTR szPathQmp)
{
    BOOL fResult = false;
    int sock = -1;
    int err;
    socklen_t cbErr = sizeof(err);
    struct sockaddr_un addr = { 0 };
    LPSTR szBuffer = NULL;
    LPSTR sz;
    SIZE_T cbBuffer = 0;
    DWORD cMemMap = ctxLC->cMemMap;

    szBuffer = malloc(QMP_BUFFER_SIZE);
    if(!szBuffer || (strlen(szPathQmp) >= sizeof(addr.sun_path))) { goto fail; }
    sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if(sock < 0) { goto fail; }
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, szPathQmp);
    if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if(errno != EINPROGRESS) { goto fail; }
        if(!DeviceQEMU_QmpWait(sock, true, DeviceQEMU_QmpTimeMs() + QMP_TIMEOUT_MS)) { goto fail; }
        if(getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &cbErr) || err) { goto fail; }
    }

    if(!DeviceQEMU_QmpReceive(sock, szBuffer, &cbBuffer, 0)) { goto fail; }
    DeviceQEMU_QmpConsume(szBuffer, &cbBuffer);
    if(!DeviceQEMU_QmpSend(sock, "{\"execute\":\"qmp_capabilities\",\"id\":1}\n")) { goto fail; }
    if(!DeviceQEMU_QmpReceive(sock, szBuffer, &cbBuffer, 1)) { goto fail; }
    DeviceQEMU_QmpConsume(szBuffer, &cbBuffer);
    if(!DeviceQEMU_QmpSend(sock, "{\"execute\":\"human-monitor-command\",\"arguments\":{\"command-line\":\"info mtree -f\"},\"id\":2}\n")) { goto fail; }
    if(!DeviceQEMU_QmpReceive(sock, szBuffer, &cbBuffer, 2)) { goto fail; }
    sz = strstr(szBuffer, "Root memory region: system");
    if(sz) { fResult = DeviceQEMU_QmpMemoryMap_Parse(ctxLC, sz); }
fail:
    if(!fResult) {
        ctxLC->cMemMap = cMemMap;
        lcprintf(ctxLC, "DEVICE: QEMU: WARN: QMP: Unable to retrieve memory regions.\n");
    }
    free(szBuffer);
    if(sock >= 0) { close(sock); }
    return fResult;
}

//-----------------------------------------------------------------------------
// INITIALIZATION FUNCTIONALITY BELOW:
//-----------------------------------------------------------------------------

// Return one argument from a complete, NUL-separated /proc/<pid>/cmdline.
_Success_(return != NULL)
static LPSTR DeviceQEMU_PmxArg(_In_reads_bytes_(cb) LPSTR szArgs, _In_ SIZE_T cb, _In_ LPCSTR szKey)
{
    LPSTR sz, szNext, szResult = NULL;
    for(sz = szArgs; sz < szArgs + cb; sz = szNext) {
        szNext = sz + strlen(sz) + 1;
        if(strcmp(sz, szKey)) { continue; }
        if(szResult || (szNext >= szArgs + cb) || !szNext[0]) { return NULL; }
        szResult = szNext;
    }
    return szResult;
}

// Deliberately support only the single shared qemu-ram file and direct QMP
// endpoint used by the acquisition setup. Do not parse JSON memory objects.
_Success_(return)
static BOOL DeviceQEMU_PmxPaths(_Inout_bytecount_(cb) LPSTR szArgs, _In_ SIZE_T cb,
    _Out_writes_(MAX_PATH) LPSTR szPathMem, _Out_writes_(MAX_PATH) LPSTR szPathQmp)
{
    LPSTR sz, szValue, szNext, szToken, szEnd, szCtx;
    DWORD cBackends = 0, cReferences = 0, cConfigs = 0, dwFields = 0, dwField;
    szValue = DeviceQEMU_PmxArg(szArgs, cb, "-qmp");
    if(!szValue || strncmp(szValue, "unix:/", 6)) { return false; }
    szEnd = strchr(szValue + 5, ',');
    if(!szEnd || ((SIZE_T)(szEnd - szValue - 5) >= MAX_PATH)) { return false; }
    if(strcmp(szEnd, ",server=on,wait=off") && strcmp(szEnd, ",wait=off,server=on")) { return false; }
    memcpy(szPathQmp, szValue + 5, szEnd - szValue - 5);
    szPathQmp[szEnd - szValue - 5] = 0;
    for(sz = szArgs; sz < szArgs + cb; sz = szNext) {
        szNext = sz + strlen(sz) + 1;
        if(!strcmp(sz, "-numa") || !strcmp(sz, "-mem-path") || !strcmp(sz, "-qmp-pretty")) { return false; }
        if(strcmp(sz, "-object") && strcmp(sz, "-machine") && strcmp(sz, "-readconfig")) { continue; }
        if(szNext >= szArgs + cb) { return false; }
        szValue = szNext;
        szNext += strlen(szValue) + 1;
        if(!strcmp(sz, "-readconfig")) {
            // Proxmox's standard Q35 device configuration; custom configs fail.
            if(strcmp(szValue, "/usr/share/qemu-server/pve-q35-4.0.cfg") || cConfigs++) { return false; }
            continue;
        }
        if(!strcmp(sz, "-machine")) {
            if((szValue[0] == '{') || strstr(szValue, ",,")) { return false; }
            for(szToken = strtok_r(szValue, ",", &szCtx); szToken; szToken = strtok_r(NULL, ",", &szCtx)) {
                if(strncmp(szToken, "memory-backend=", 15)) { continue; }
                if(strcmp(szToken, "memory-backend=qemu-ram") || cReferences++) { return false; }
            }
            continue;
        }
        // Proxmox also supplies unrelated objects (e.g. JSON throttle groups).
        if(!strstr(szValue, "memory-backend")) { continue; }
        if(strstr(szValue, ",,") || strncmp(szValue, "memory-backend-file,", 20) || cBackends++) { return false; }
        for(szToken = strtok_r(szValue + 20, ",", &szCtx); szToken; szToken = strtok_r(NULL, ",", &szCtx)) {
            if(!strcmp(szToken, "id=qemu-ram")) { dwField = 1; }
            else if(!strcmp(szToken, "share=on")) { dwField = 2; }
            else if(!strncmp(szToken, "mem-path=/", 10) && (strlen(szToken + 9) < MAX_PATH)) {
                strcpy(szPathMem, szToken + 9);
                dwField = 4;
            } else if(!strncmp(szToken, "size=", 5)) { dwField = 8; }
            else if(!strncmp(szToken, "prealloc=", 9)) { dwField = 16; }
            else { return false; }
            if(dwFields & dwField) { return false; }
            dwFields |= dwField;
        }
    }
    return (cBackends == 1) && (cReferences == 1) && ((dwFields & 7) == 7);
}

_Success_(return)
static BOOL DeviceQEMU_PmxResolve(_In_ PLC_CONTEXT ctxLC, _In_opt_ PLC_DEVICE_PARAMETER_ENTRY pVmid,
    _In_opt_ PLC_DEVICE_PARAMETER_ENTRY pVmname, _Out_writes_(MAX_PATH) LPSTR szPathMem, _Out_writes_(MAX_PATH) LPSTR szPathQmp)
{
    DIR *pDir = NULL;
    struct dirent *pEntry;
    FILE *pFile;
    CHAR szPath[MAX_PATH], szId[16], szArgs[0x10000];
    LPSTR szEnd, szIdArg, szName, szExe;
    SIZE_T cb;
    DWORD dwPid, dwVmid, cMatches = 0;
    unsigned long ulVmid;
    BOOL fResult = false;
    if(pVmid) {
        cb = strlen(pVmid->szValue);
        if(!cb || (cb > 9) || (strspn(pVmid->szValue, "0123456789") != cb)) { goto fail; }
    }
    if(pVmname && !pVmname->szValue[0]) { goto fail; }
    if(!(pDir = opendir("/run/qemu-server"))) { goto fail; }
    while((pEntry = readdir(pDir))) {
        if((pEntry->d_name[0] < '0') || (pEntry->d_name[0] > '9')) { continue; }
        ulVmid = strtoul(pEntry->d_name, &szEnd, 10);
        if(!ulVmid || (ulVmid > 999999999) || strcmp(szEnd, ".pid")) { continue; }
        dwVmid = (DWORD)ulVmid;
        if(pVmid && (strtoul(pVmid->szValue, NULL, 10) != dwVmid)) { continue; }
        snprintf(szId, sizeof(szId), "%u", dwVmid);
        snprintf(szPath, sizeof(szPath), "/run/qemu-server/%u.pid", dwVmid);
        if(!(pFile = fopen(szPath, "r"))) { continue; }
        dwPid = 0;
        if(fscanf(pFile, "%u", &dwPid) != 1) { dwPid = 0; }
        fclose(pFile);
        if(!dwPid) { continue; }
        snprintf(szPath, sizeof(szPath), "/proc/%u/cmdline", dwPid);
        if(!(pFile = fopen(szPath, "rb"))) { continue; }
        cb = fread(szArgs, 1, sizeof(szArgs), pFile);
        fclose(pFile);
        if(!cb || (cb == sizeof(szArgs)) || szArgs[cb - 1]) { continue; }
        szExe = strrchr(szArgs, '/');
        szExe = szExe ? szExe + 1 : szArgs;
        if(strcmp(szExe, "kvm") && strcmp(szExe, "qemu-system-x86_64")) { continue; }
        szIdArg = DeviceQEMU_PmxArg(szArgs, cb, "-id");
        if(!szIdArg || strcmp(szIdArg, szId)) { continue; }
        if(pVmname) {
            szName = DeviceQEMU_PmxArg(szArgs, cb, "-name");
            if(!szName) { continue; }
            if(!strncmp(szName, "guest=", 6)) { szName += 6; }
            if((strcspn(szName, ",") != strlen(pVmname->szValue)) || strncmp(szName, pVmname->szValue, strlen(pVmname->szValue))) { continue; }
        }
        if(cMatches++) { goto fail; }
        if(!DeviceQEMU_PmxPaths(szArgs, cb, szPathMem, szPathQmp)) { goto fail; }
        lcprintfv(ctxLC, "DEVICE: QEMU: Proxmox VMID %u, PID %u.\n", dwVmid, dwPid);
    }
    fResult = (cMatches == 1);
fail:
    if(pDir) { closedir(pDir); }
    if(!fResult) { lcprintf(ctxLC, "DEVICE: QEMU: FAIL: No unique running local Proxmox VM with supported shared RAM and QMP.\n"); }
    return fResult;
}

_Success_(return)
static BOOL DeviceQEMU_MapFd(_In_ PLC_CONTEXT ctxLC, _Inout_ PDEVICE_CONTEXT_QEMU ctx, _In_ int fd)
{
    struct stat st;
    if(fstat(fd, &st) || !S_ISREG(st.st_mode) || (st.st_size <= 0) || (st.st_size % 0x1000)) {
        lcprintf(ctxLC, "DEVICE: QEMU: FAIL: RAM backing must be a nonempty, page-aligned regular file.\n");
        return false;
    }
    ctx->cb = st.st_size;
    ctx->pb = mmap(NULL, ctx->cb, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(ctx->pb == MAP_FAILED) {
        ctx->pb = NULL;
        lcprintf(ctxLC, "DEVICE: QEMU: FAIL: 'mmap' failed.\n");
        return false;
    }
    return true;
}


_Success_(return)
BOOL LcPluginCreate_Shm(_In_ PLC_CONTEXT ctxLC, _Inout_ PDEVICE_CONTEXT_QEMU ctx, _In_ PLC_DEVICE_PARAMETER_ENTRY pPathShm)
{
    int fd = -1;
    BOOL fResult;
    CHAR szPathMem[MAX_PATH] = { 0 };

    if(!pPathShm || !pPathShm->szValue[0] || (strlen(pPathShm->szValue) > MAX_PATH - 10)) {
        lcprintf(ctxLC, "DEVICE: QEMU: FAIL: Required parameter shm not given.\n");
        lcprintf(ctxLC, "   Example: qemu://shm=qemu-ram\n");
        goto fail;
    } else {
        strcat(szPathMem, "/dev/shm/");
        strcat(szPathMem, pPathShm->szValue);
    }

    // Open before querying the size so both operations refer to the same file.
    fd = shm_open(pPathShm->szValue, O_RDWR | O_SYNC, 0);
    if(fd < 0) {
        lcprintf(ctxLC, "DEVICE: QEMU: FAIL: 'shm_open' failed path='%s', errorcode=%i.\n", szPathMem, errno);
        goto fail;
    }
    fResult = DeviceQEMU_MapFd(ctxLC, ctx, fd);
    close(fd);
    return fResult;

fail:
    if(fd >= 0) { close(fd); }
    return false;
}

_Success_(return)
BOOL LcPluginCreate_HugePages(_In_ PLC_CONTEXT ctxLC, _Inout_ PDEVICE_CONTEXT_QEMU ctx, _In_ QWORD qwHugePagePid)
{
    
    DIR *fdDir;
    int fd = -1;
    BOOL fResult;
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

    fResult = DeviceQEMU_MapFd(ctxLC, ctx, fd);
    close(fd);
    return fResult;

fail:
    if(fd >= 0) { close(fd); }
    return false;
}

_Success_(return) EXPORTED_FUNCTION
BOOL LcPluginCreate(_Inout_ PLC_CONTEXT ctxLC, _Out_opt_ PPLC_CONFIG_ERRORINFO ppLcCreateErrorInfo)
{
    PDEVICE_CONTEXT_QEMU ctx = NULL;
    PLC_DEVICE_PARAMETER_ENTRY pPathShm = NULL;
    PLC_DEVICE_PARAMETER_ENTRY pPathQmp = NULL;
    CHAR szPathQmp[MAX_PATH] = { 0 };
    CHAR szPathMem[MAX_PATH] = { 0 };
    PLC_DEVICE_PARAMETER_ENTRY pPmxVmid, pPmxVmname;
    BOOL fResult;
    int fd;
    QWORD qwHugePagePid;

    lcprintf(ctxLC, "DEVICE: QEMU: Initializing\n");

    // safety checks
    if(ppLcCreateErrorInfo) { *ppLcCreateErrorInfo = NULL; }
    if(ctxLC->version != LC_CONTEXT_VERSION) { return false; }

    // init context & parameters:
    ctx = (PDEVICE_CONTEXT_QEMU)calloc(1, sizeof(DEVICE_CONTEXT_QEMU));
    if(!ctx) { return false; }

    qwHugePagePid = LcDeviceParameterGetNumeric(ctxLC, "hugepage-pid");
    pPathShm = LcDeviceParameterGet(ctxLC, "shm");
    pPathQmp = LcDeviceParameterGet(ctxLC, "qmp");

    ctx->tmnsDelayLatency = LcDeviceParameterGetNumeric(ctxLC, "delay-latency-ns");
    ctx->tmnsDelayReadPage = LcDeviceParameterGetNumeric(ctxLC, "delay-readpage-ns");
    ctx->fDelay = (ctx->tmnsDelayLatency > 0) || (ctx->tmnsDelayReadPage > 0);

    pPmxVmid = LcDeviceParameterGet(ctxLC, "pmx-vmid");
    pPmxVmname = LcDeviceParameterGet(ctxLC, "pmx-vmname");
    if(pPmxVmid || pPmxVmname) {
        fResult = !(pPmxVmid && pPmxVmname) && !pPathShm && !pPathQmp && !LcDeviceParameterGet(ctxLC, "hugepage-pid");
        if(!fResult) {
            lcprintf(ctxLC, "DEVICE: QEMU: FAIL: Use one pmx selector without shm, hugepage-pid or qmp.\n");
            goto fail;
        }
        if(!DeviceQEMU_PmxResolve(ctxLC, pPmxVmid, pPmxVmname, szPathMem, szPathQmp)) { goto fail; }
        fd = open(szPathMem, O_RDWR | O_SYNC, 0);
        fResult = (fd >= 0) && DeviceQEMU_MapFd(ctxLC, ctx, fd);
        if(fd >= 0) { close(fd); }
        if(!fResult) {
            lcprintf(ctxLC, "DEVICE: QEMU: FAIL: Unable to map discovered RAM backing file.\n");
            goto fail;
        }
        if(!DeviceQEMU_QmpMemoryMap(ctxLC, szPathQmp)) { goto fail; }
        goto success;
    }

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
success:
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
