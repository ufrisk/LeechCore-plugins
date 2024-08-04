/*
  Contribution by Zero Tang - tangptr.com

  leechcore_device_qemu_pcileech.c: implementation related to QEMU PCILeech device.
*/

#include <stdint.h>
#include <stdlib.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#include <byteswap.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
#include <leechcore_device.h>

#define QEMU_PCILEECH_PRTOCOL_PREFIX "qemupcileech://"

#ifdef _WIN32
#define bswap_64 _byteswap_uint64
#define bswap_32 _byteswap_ulong
#define bswap_16 _byteswap_ushort
#else
#define TRUE 1
#define FALSE 0
#endif

typedef struct _DEVICE_CONTEXT_QEMU_PCILEECH
{
	char Host[MAX_PATH];
	uint16_t Port;
    uint8_t Endianness;
#ifdef _WIN32
    SOCKET sock_fd;
#else
	int sock_fd;
#endif
    struct sockaddr_in addrin;
}DEVICE_CONTEXT_QEMU_PCILEECH,*PDEVICE_CONTEXT_QEMU_PCILEECH;

typedef struct _PCILEECH_REQUEST_HEADER
{
    uint8_t endianness;
    uint8_t command;
    uint8_t reserved[6];
    uint64_t address;
    uint64_t length;
}PCILEECH_REQUEST_HEADER,*PPCILEECH_REQUEST_HEADER;

typedef struct _PCILEECH_RESPONSE_HEADER
{
    uint8_t endianness;
    uint8_t reserved[3];
    uint32_t result;    // See MemTxResult definition from QEMU.
    uint64_t length;    // Length of data followed after this header.
}PCILEECH_RESPONSE_HEADER,*PPCILEECH_RESPONSE_HEADER;

DEVICE_CONTEXT_QEMU_PCILEECH QemuPciLeechContext;

BOOL InternalConnect(PLC_CONTEXT ctxLC)
{
    QemuPciLeechContext.sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (QemuPciLeechContext.sock_fd < 0)
    {
        lcprintf(ctxLC, "QEMU-PCILeech: Failed to create socket!\n");
        return FALSE;
    }
    if (connect(QemuPciLeechContext.sock_fd, (struct sockaddr *)&QemuPciLeechContext.addrin,
                sizeof(QemuPciLeechContext.addrin)) < 0) {
        lcprintf(ctxLC, "QEMU-PCILeech: Failed to connect to target!\n");
#ifdef _WIN32
        closesocket(QemuPciLeechContext.sock_fd);
#else
        close(QemuPciLeechContext.sock_fd);
#endif
        return FALSE;
    }
    return TRUE;
}

void InternalClose(PLC_CONTEXT ctxLC)
{
#ifdef _WIN32
    closesocket(QemuPciLeechContext.sock_fd);
#else
    close(QemuPciLeechContext.sock_fd);
#endif
}

uint32_t InternalReadDma(PLC_CONTEXT ctxLC, uint64_t address, uint8_t *buffer, uint64_t length)
{
    if (InternalConnect(ctxLC))
    {
        // Send request.
        PCILEECH_REQUEST_HEADER Request = {.address = address,
                                           .command = 0,
                                           .reserved = {0, 0, 0, 0, 0, 0},
                                           .endianness =
                                               QemuPciLeechContext.Endianness,
                                           .length = length};
        PCILEECH_RESPONSE_HEADER Response = {0};
        char *buff = (char *)&Request;
        int sendlen = 0, recvlen = 0;
        while (sendlen < sizeof(Request))
            sendlen += send(QemuPciLeechContext.sock_fd, &buff[sendlen],
                            sizeof(Request) - sendlen, 0);
        // Receive contents.
        while (recvlen < length)
        {
            int resplen = 0, recvlen_i = 0;
            buff = (char *)&Response;
            // Receive the header.
            while (resplen < sizeof(Response))
                resplen += recv(QemuPciLeechContext.sock_fd, &buff[resplen],
                                sizeof(Response) - resplen, 0);
            // Swap endianness if needed.
            if (Response.endianness != QemuPciLeechContext.Endianness)
            {
                Response.result = bswap_32(Response.result);
                Response.length = bswap_64(Response.length);
            }
            // Check the result.
            if (Response.result)
            {
                lcprintf(ctxLC,
                         "QEMU-PCILeech: DMA-Read Encountered Error! "
                         "MemTxResult=0x%X\n",
                         Response.result);
                break;
            }
            // Receive contents.
            while (recvlen_i < Response.length)
                recvlen_i += recv(QemuPciLeechContext.sock_fd,
                                  &buffer[recvlen + recvlen_i],
                                  (int)(Response.length - recvlen_i), 0);
            // Accumulate counter.
            recvlen += recvlen_i;
        }
        InternalClose(ctxLC);
        return Response.result;
    }
    return 0xFFFFFFFF;
}

uint32_t InternalWriteDma(PLC_CONTEXT ctxLC, uint64_t address, uint8_t* buffer, uint64_t length)
{
    if (InternalConnect(ctxLC))
    {
        // Send request.
        PCILEECH_REQUEST_HEADER Request = {.address = address,
                                           .command = 1,
                                           .reserved = {0, 0, 0, 0, 0, 0},
                                           .endianness =
                                               QemuPciLeechContext.Endianness,
                                           .length = length};
        PCILEECH_RESPONSE_HEADER Response = {0};
        char *buff = (char *)&Request;
        int sendlen = 0;
        while (sendlen < sizeof(Request))
            sendlen += send(QemuPciLeechContext.sock_fd, &buff[sendlen],
                            sizeof(Request) - sendlen, 0);
        // Send data.
        sendlen = 0;
        while (sendlen < length)
        {
            int resplen = 0, sendlen_i = 0;
            buff = (char *)&Response;
            // Send a segment.
            while (sendlen_i < 1024)
                sendlen_i +=
                    send(QemuPciLeechContext.sock_fd,
                         &buffer[sendlen + sendlen_i], 1024 - sendlen_i, 0);
            // Receive response.
            while (resplen < sizeof(Response))
                resplen += recv(QemuPciLeechContext.sock_fd, &buff[resplen],
                                sizeof(Response) - resplen, 0);
            // Swap endianness if needed.
            if (Response.endianness != QemuPciLeechContext.Endianness)
            {
                Response.result = bswap_32(Response.result);
                Response.length = bswap_64(Response.length);
            }
            // Check the result.
            if (Response.result)
            {
                lcprintf(ctxLC,
                         "QEMU-PCILeech: DMA-Write Encountered Error! "
                         "MemTxResult=0x%X\n",
                         Response.result);
                break;
            }
        }
        InternalClose(ctxLC);
    }
    return 0xFFFFFFFF;
}

void LcPluginReadScatter(PLC_CONTEXT ctxLC, DWORD cpMEMs, PPMEM_SCATTER ppMEMs)
{
    for (DWORD i = 0; i < cpMEMs; i++)
    {
        uint32_t result = InternalReadDma(ctxLC, ppMEMs[i]->qwA, ppMEMs[i]->pb,
                                          ppMEMs[i]->cb);
        ppMEMs[i]->f = (result == 0);
    }
}

void LcPluginWriteScatter(PLC_CONTEXT ctxLC, DWORD cpMEMs, PPMEM_SCATTER ppMEMs)
{
    for (DWORD i = 0; i < cpMEMs; i++)
    {
        uint32_t result = InternalWriteDma(ctxLC, ppMEMs[i]->qwA, ppMEMs[i]->pb,
                                           ppMEMs[i]->cb);
        ppMEMs[i]->f = (result == 0);
    }
}

void LcPluginClose(PLC_CONTEXT ctxLC)
{
#ifdef _WIN32
    // Win32 requires Startup and Cleanup for socket operations.
    WSACleanup();
#endif
}

EXPORTED_FUNCTION BOOL LcPluginCreate(PLC_CONTEXT ctxLC, PPLC_CONFIG_ERRORINFO ppLcCreateErrorInfo)
{
    struct hostent *he;
    int x = 1;
    char *y = (char *)&x;
    // Setup Context
    if (ctxLC->version != LC_CONTEXT_VERSION)
        return FALSE;
    ctxLC->hDevice = (HANDLE)&QemuPciLeechContext;
    QemuPciLeechContext.Endianness = (*y != 1);
    if (memcmp(ctxLC->Config.szDevice, QEMU_PCILEECH_PRTOCOL_PREFIX, sizeof(QEMU_PCILEECH_PRTOCOL_PREFIX) - 1))
    {
        lcprintf(ctxLC, "QEMU-PCILeech: This protocol is unknown!\n");
        return FALSE;
    }
    // Parse target
    QemuPciLeechContext.Port = 6789;    // Default Port
    for (int i = sizeof(QEMU_PCILEECH_PRTOCOL_PREFIX) - 1; i < MAX_PATH; i++)
    {
        QemuPciLeechContext.Host[i - sizeof(QEMU_PCILEECH_PRTOCOL_PREFIX) + 1] =
            ctxLC->Config.szDevice[i];
        if (ctxLC->Config.szDevice[i] == '\0')
            break;
        if (ctxLC->Config.szDevice[i] == ':')
        {
            QemuPciLeechContext.Host[i - sizeof(QEMU_PCILEECH_PRTOCOL_PREFIX) + 1] =
                '\0';
            QemuPciLeechContext.Port =
                (uint16_t)strtoul(&ctxLC->Config.szDevice[i + 1], NULL, 10);
            break;
        }
    }
#ifdef _WIN32
    // Win32 requires Startup and Cleanup for socket operations.
    WSADATA wd;
    int err = WSAStartup(MAKEWORD(2, 0), &wd);
    if (err)
    {
        lcprintf(ctxLC, "QEMU-PCILeech: WSAStartup Failed! Error Code: %d\n",
                 err);
        return FALSE;
    }
#endif
    he = gethostbyname(QemuPciLeechContext.Host);
    QemuPciLeechContext.addrin.sin_family = AF_INET;
    QemuPciLeechContext.addrin.sin_port = htons(QemuPciLeechContext.Port);
    QemuPciLeechContext.addrin.sin_addr.s_addr =
        (he == NULL) ? inet_addr(QemuPciLeechContext.Host)
                     : (*(struct in_addr *)he->h_addr_list[0]).s_addr;
    lcprintf(ctxLC, "QEMU-PCILeech: Connecting to qemupcileech://%s:%u ...\n",
             inet_ntoa(QemuPciLeechContext.addrin.sin_addr), QemuPciLeechContext.Port);
    // Register plugin functions...
    ctxLC->pfnReadScatter = LcPluginReadScatter;
    ctxLC->pfnWriteScatter = LcPluginWriteScatter;
    ctxLC->pfnClose = LcPluginClose;
    return TRUE;
}