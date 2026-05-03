// oscompatibility.h : Windows/Linux compatibility layer.
//
// (c) Ulf Frisk, 2020
// Author: Ulf Frisk, pcileech@frizk.net
//
#ifndef __OSCOMPATIBILITY_H__
#define __OSCOMPATIBILITY_H__

#ifdef LINUX
#define _GNU_SOURCE
#endif /* LINUX */

#if defined(LINUX) || defined(MACOS)
#include <leechcore.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#define SOCKET                              int
#define INVALID_SOCKET	                    -1
#define SOCKET_ERROR	                    -1
#define WSAEWOULDBLOCK                      10035L

#define TRUE                                1
#define FALSE                               0
#define LMEM_ZEROINIT                       0x0040

#define strcpy_s(dst, len, src)             (strncpy(dst, src, len))
#define ZeroMemory(pb, cb)                  (memset(pb, 0, cb))
#define closesocket(s)                      close(s)

HANDLE LocalAlloc(DWORD uFlags, SIZE_T uBytes);
VOID LocalFree(HANDLE hMem);

#endif /* LINUX || MACOS */

#endif /* __OSCOMPATIBILITY_H__ */
