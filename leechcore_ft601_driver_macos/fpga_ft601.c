// fpga_ft601.c : implementation of the FT601 wrapper library for the FPGA D3XX driver.
//
// (c) Ulf Frisk, 2025
// Author: Ulf Frisk, pcileech@frizk.net
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <dispatch/dispatch.h>
#include "fpga_ft601.h"

#define FT_OTHER_ERROR                      32
#define TRUE                                1
#define FALSE                               0
#define _Inout_

typedef void                                VOID, *PVOID, *LPVOID;
typedef uint8_t                             UCHAR, *PUCHAR;
typedef void                                *HANDLE, **PHANDLE, *HMODULE, *FARPROC;
typedef uint32_t                            BOOL, *PBOOL;
typedef uint32_t                            UINT, DWORD, *PDWORD, *LPDWORD, NTSTATUS, ULONG, *PULONG, ULONG32;

typedef ULONG(*PFN_FT_Create)(PVOID pvArg, DWORD dwFlags, HANDLE *pftHandle);
typedef ULONG(*PFN_FT_Close)(HANDLE ftHandle);
typedef ULONG(*PFN_FT_GetChipConfiguration)(HANDLE ftHandle, PVOID pvConfiguration);
typedef ULONG(*PFN_FT_SetChipConfiguration)(HANDLE ftHandle, PVOID pvConfiguration);
typedef ULONG(*PFN_FT_AbortPipe)(HANDLE ftHandle, UCHAR ucPipeID);
typedef ULONG(*PFN_FT_WritePipe)(HANDLE ftHandle, UCHAR ucPipeID, PUCHAR pucBuffer, ULONG ulBufferLength, PULONG pulBytesTransferred, DWORD dwTimeoutInMs);
typedef ULONG(*PFN_FT_ReadPipe)(HANDLE ftHandle, UCHAR ucPipeID, PUCHAR pucBuffer, ULONG ulBufferLength, PULONG pulBytesTransferred, DWORD dwTimeoutInMs);



// ----------------------------------------------------------------------------
// SRWLock functionality:
// ----------------------------------------------------------------------------

typedef struct tdSRWLOCK {
    int f_init;
    dispatch_semaphore_t sem;
} SRWLOCK, *PSRWLOCK;

VOID InitializeSRWLock(PSRWLOCK pSRWLock)
{
    if(!pSRWLock->f_init) {
        pSRWLock->sem = dispatch_semaphore_create(1);
        pSRWLock->f_init = 1;
    }
}

VOID AcquireSRWLockExclusive(_Inout_ PSRWLOCK pSRWLock)
{
    if(!pSRWLock->f_init) { InitializeSRWLock(pSRWLock); }
    dispatch_semaphore_wait(pSRWLock->sem, DISPATCH_TIME_FOREVER);
}

VOID ReleaseSRWLockExclusive(_Inout_ PSRWLOCK pSRWLock)
{
    if(pSRWLock->f_init) {
        dispatch_semaphore_signal(pSRWLock->sem);
    }
}



// ----------------------------------------------------------------------------
// FPGA driver defines:
// ----------------------------------------------------------------------------

#define vprintfv(format, ...)       { printf(format, ##__VA_ARGS__); }

struct fpga_context {
    void *lib;
    HANDLE ftHandle;
    // pfn:
    struct {
        PFN_FT_Create pfnFT_Create;
        PFN_FT_Close pfnFT_Close;
        PFN_FT_GetChipConfiguration pfnFT_GetChipConfiguration;
        PFN_FT_SetChipConfiguration pfnFT_SetChipConfiguration;
        PFN_FT_AbortPipe pfnFT_AbortPipe;
        PFN_FT_WritePipe pfnFT_WritePipe;
        PFN_FT_ReadPipe pfnFT_ReadPipe;
    } pfn;
    int is_safe_mode;
    SRWLOCK lock;
    // async context:
    struct {
        uint32_t is_valid;
        // thread control:
        pthread_t tid;
        int is_thread_running;
        // thread read lock:
        SRWLOCK lock_thread_read;
        int is_thread_read;
        // result lock (wait for read to complete):
        SRWLOCK lock_result;
        int is_result;
        // data:
        void* data;
        int data_read;
        int data_size;
    } async;
};



// ----------------------------------------------------------------------------
// FPGA driver synchronous functionality:
// ----------------------------------------------------------------------------

struct fpga_context *fpga_open(void *pvArg, uint32_t dwFlags)
{
    ULONG rc;
    struct fpga_context *ctx;

    // alloc ctx:
    ctx = malloc(sizeof(struct fpga_context));
    if(!ctx) { goto fail; }
    memset(ctx, 0, sizeof(struct fpga_context));

    // load lib:
    ctx->lib = dlopen("libftd3xx.dylib", RTLD_NOW);
    if(!ctx->lib) {
        vprintfv("[-] Unable to open library: 'libftd3xx.dylib'\n");
        goto fail;
    }

    // function lookup:
    ctx->pfn.pfnFT_AbortPipe = (PFN_FT_AbortPipe)dlsym(ctx->lib, "FT_AbortPipe");
    ctx->pfn.pfnFT_Close = (PFN_FT_Close)dlsym(ctx->lib, "FT_Close");
    ctx->pfn.pfnFT_Create = (PFN_FT_Create)dlsym(ctx->lib, "FT_Create");
    ctx->pfn.pfnFT_GetChipConfiguration = (PFN_FT_GetChipConfiguration)dlsym(ctx->lib, "FT_GetChipConfiguration");
    ctx->pfn.pfnFT_SetChipConfiguration = (PFN_FT_SetChipConfiguration)dlsym(ctx->lib, "FT_SetChipConfiguration");
    ctx->pfn.pfnFT_WritePipe = (PFN_FT_WritePipe)dlsym(ctx->lib, "FT_WritePipe");
    ctx->pfn.pfnFT_ReadPipe = (PFN_FT_ReadPipe)dlsym(ctx->lib, "FT_ReadPipe");
    if(!ctx->pfn.pfnFT_AbortPipe || !ctx->pfn.pfnFT_Close || !ctx->pfn.pfnFT_Create || !ctx->pfn.pfnFT_GetChipConfiguration || !ctx->pfn.pfnFT_SetChipConfiguration || !ctx->pfn.pfnFT_WritePipe || !ctx->pfn.pfnFT_ReadPipe) {
        vprintfv("[-] Unable to find function in library\n");
        goto fail;
    }

    ctx->is_safe_mode = ((uint64_t)pvArg >> 31) ? 0 : 1;
    pvArg = (void*)((uint64_t)pvArg & 0x7FFFFFFF);

    // ft601 initialize handle:
    rc = ctx->pfn.pfnFT_Create(pvArg, dwFlags, &ctx->ftHandle);
    if(rc) {
        vprintfv("[-] Unable to create device (rc = %i)\n", rc);
        goto fail;
    }

    // success:
    return ctx;

    // fail:
fail:
    fpga_close(ctx);
    return NULL;
}

uint32_t fpga_close(struct fpga_context *ctx)
{
    if(ctx) {
        if(ctx->ftHandle && ctx->pfn.pfnFT_Close) {
            ctx->pfn.pfnFT_Close(ctx->ftHandle);
        }
        if(ctx->lib) {
            dlclose(ctx->lib);
        }
        free(ctx);
    }
    return 0;
}

uint32_t fpga_get_chip_configuration(struct fpga_context *ctx, void *config)
{
    return ctx->pfn.pfnFT_GetChipConfiguration(ctx->ftHandle, config);
}

uint32_t fpga_set_chip_configuration(struct fpga_context *ctx, void *config)
{
    return ctx->pfn.pfnFT_SetChipConfiguration(ctx->ftHandle, config);
}

uint32_t fpga_read(struct fpga_context *ctx, void *data, uint32_t size, uint32_t *transferred)
{
    uint32_t rc;
    if(ctx->async.is_thread_read) {
        vprintfv("[-] previous async read is not yet completed. complete by reading results before initiating new read!\n");
        return FT_OTHER_ERROR;
    }
    if(ctx->is_safe_mode) {
        AcquireSRWLockExclusive(&ctx->lock);
        rc = ctx->pfn.pfnFT_ReadPipe(ctx->ftHandle, FTDI_ENDPOINT_IN, data, size, transferred, 0);
        ReleaseSRWLockExclusive(&ctx->lock);
    } else {
        rc = ctx->pfn.pfnFT_ReadPipe(ctx->ftHandle, FTDI_ENDPOINT_IN, data, size, transferred, 0);
    }
    return rc;
}

uint32_t fpga_write(struct fpga_context *ctx, void *data, uint32_t size, uint32_t *transferred)
{
    uint32_t rc;
    if(ctx->is_safe_mode) {
        AcquireSRWLockExclusive(&ctx->lock);
        rc = ctx->pfn.pfnFT_WritePipe(ctx->ftHandle, FTDI_ENDPOINT_OUT, data, size, transferred, 0);
        ReleaseSRWLockExclusive(&ctx->lock);
    } else {
        rc = ctx->pfn.pfnFT_WritePipe(ctx->ftHandle, FTDI_ENDPOINT_OUT, data, size, transferred, 0);
    }
    return rc;
}



// ----------------------------------------------------------------------------
// "ASYNC" functionality below:
// ----------------------------------------------------------------------------

int fpga_read_internal(struct fpga_context *ctx, void *data, int size, int *transferred)
{
    uint32_t rc;
    *transferred = 0;
    if(ctx->is_safe_mode) {
        AcquireSRWLockExclusive(&ctx->lock);
        rc = ctx->pfn.pfnFT_ReadPipe(ctx->ftHandle, FTDI_ENDPOINT_IN, data, size, (uint32_t *)transferred, 1000);
        ReleaseSRWLockExclusive(&ctx->lock);
    } else {
        rc = ctx->pfn.pfnFT_ReadPipe(ctx->ftHandle, FTDI_ENDPOINT_IN, data, size, (uint32_t *)transferred, 1000);
    }
    if(rc && !ctx->is_safe_mode) {
        usleep(100);
        ctx->is_safe_mode = 1;
        AcquireSRWLockExclusive(&ctx->lock);
        rc = ctx->pfn.pfnFT_ReadPipe(ctx->ftHandle, FTDI_ENDPOINT_IN, data, size, (uint32_t *)transferred, 1000);
        ReleaseSRWLockExclusive(&ctx->lock);
    }
    if(rc) {
        vprintfv("[-] bulk transfer error: %i \n", rc);
        return -1;
    }
    return 0;
}

void* fpga_async_thread(void* thread_ctx)
{
    struct fpga_context *ctx = thread_ctx;
    AcquireSRWLockExclusive(&ctx->async.lock_thread_read);
    while(ctx->async.is_valid) {
        usleep(20);
        fpga_read_internal(ctx, ctx->async.data, ctx->async.data_size, &ctx->async.data_read);
        ctx->async.is_result = 1;
        ReleaseSRWLockExclusive(&ctx->async.lock_result);
        AcquireSRWLockExclusive(&ctx->async.lock_thread_read);
    };
    ReleaseSRWLockExclusive(&ctx->async.lock_result);
    ctx->async.is_thread_read = 0;
    ctx->async.tid = 0;
    return NULL;
}

int fpga_async_init(struct fpga_context *ctx)
{
    if(ctx->async.is_valid) {
        vprintfv("[-] only one async overlapped supported. close previous one before open new!\n");
        return -1;
    }
    ctx->async.is_result = 1;
    ctx->async.is_valid = 1;
    AcquireSRWLockExclusive(&ctx->async.lock_result);
    AcquireSRWLockExclusive(&ctx->async.lock_thread_read);
    pthread_create(&ctx->async.tid, NULL, fpga_async_thread, ctx);
    if(!ctx->async.tid) {
        vprintfv("[-] failed creating thread.\n");
        memset(&ctx->async, 0, sizeof(ctx->async));
        return -1;
    }
    return 0;
}

int fpga_async_close(struct fpga_context *ctx)
{
    if(ctx->async.is_valid) {
        ctx->async.is_valid = 0;
        ReleaseSRWLockExclusive(&ctx->async.lock_thread_read);
        while(ctx->async.tid) { sched_yield(); }
        memset(&ctx->async, 0, sizeof(ctx->async));
    }
    return 0;
}

int fpga_async_read(struct fpga_context *ctx, void *data, int size)
{
    if(!ctx->async.is_valid) {
        vprintfv("[-] invalid context!\n");
        return -1;
    }
    if(ctx->async.is_thread_read) {
        vprintfv("[-] previous async read is not yet completed. complete by reading results before initiating new read!\n");
        return -1;
    }
    ctx->async.data = data;
    ctx->async.data_size = size;
    ctx->async.data_read = 0;
    ctx->async.is_result = 0;
    ctx->async.is_thread_read = 1;
    ReleaseSRWLockExclusive(&ctx->async.lock_thread_read);
    return 0;
}

int fpga_async_result(struct fpga_context *ctx, uint32_t *transferred)
{
    if(!ctx->async.is_valid) {
        vprintfv("[-] invalid context!\n");
        return -1;
    }
    if(ctx->async.is_thread_read) {
        AcquireSRWLockExclusive(&ctx->async.lock_result);
        *transferred = ctx->async.data_read;
        ctx->async.is_thread_read = 0;
        ctx->async.is_result = 1;
    } else {
        *transferred = 0;
        ctx->async.is_result = 1;
    }
    return 0;
}
