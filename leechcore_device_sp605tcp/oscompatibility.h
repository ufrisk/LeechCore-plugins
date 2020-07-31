// oscompatibility.h : Windows/Linux compatibility layer.
//
// (c) Ulf Frisk, 2020
// Author: Ulf Frisk, pcileech@frizk.net
//
#ifndef __OSCOMPATIBILITY_H__
#define __OSCOMPATIBILITY_H__

#include <stdio.h>
#include <leechcore.h>

#ifdef LINUX
#define _GNU_SOURCE

#include <byteswap.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef void                                VOID, *PVOID;
typedef void                                *HANDLE, **PHANDLE;
typedef char                                CHAR, *PCHAR, *PSTR, *LPSTR;
typedef uint16_t                            WORD, *PWORD, USHORT, *PUSHORT;
typedef uint32_t                            DWORD, *PDWORD;
typedef uint64_t                            SIZE_T, *PSIZE_T;

#define SOCKET                              int
#define INVALID_SOCKET	                    -1
#define SOCKET_ERROR	                    -1
#define WSAEWOULDBLOCK                      10035L

#define TRUE                                1
#define FALSE                               0
#define LMEM_ZEROINIT                       0x0040

#define _Inout_opt_
#define __bcount(x)

#define max(a, b)                           (((a) > (b)) ? (a) : (b))
#define min(a, b)                           (((a) < (b)) ? (a) : (b))
#define _byteswap_ulong(v)                  (bswap_32(v))
#define strcpy_s(dst, len, src)             (strncpy(dst, src, len))
#define ZeroMemory(pb, cb)                  (memset(pb, 0, cb))
#define closesocket(s)                      close(s)
#define Sleep(dwMilliseconds)               (usleep(1000*dwMilliseconds))

HANDLE LocalAlloc(DWORD uFlags, SIZE_T uBytes);
VOID LocalFree(HANDLE hMem);
QWORD GetTickCount64();

#endif /* LINUX */

#endif /* __OSCOMPATIBILITY_H__ */
