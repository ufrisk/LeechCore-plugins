// device605_tcp.c : implementation related to the Xilinx SP605 dev board flashed with @d_olex bitstream.
//
// (c) Ulf Frisk & @d_olex, 2017-2020
//
#ifdef _WIN32

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#endif /* _WIN32 */

#ifdef _WIN32
#include <PshPack1.h>
#endif /* _WIN32 */

typedef struct _PCIE_CTL {
    unsigned char flags; // see PCIE_F_*
    unsigned int data;
}
#ifdef LINUX
__attribute__((packed))
#endif /* LINUX */
PCIE_CTL;

#ifdef _WIN32
#include <PopPack.h>
#endif /* _WIN32 */

#include <leechcore_device.h>
#include "oscompatibility.h"
#include "tlp.h"

//-------------------------------------------------------------------------------
// FPGA/SP605/TCP defines below.
//-------------------------------------------------------------------------------

#define PCIE_F_HAS_DATA         0x01 // PCIE_CTL has TLP dword to send
#define PCIE_F_RECV_REPLY       0x02 // receive reply TLP
#define PCIE_F_TLAST            0x04 // last TLP dword
#define PCIE_F_TIMEOUT          0x08 // TLP receive timeout occured
#define PCIE_F_ERROR            0x10 // some error occured
#define PCIE_F_STATUS           0x20 // get PCI-E link status

#define TLP_RX_SIZE             128
#define TLP_RX_MAX_SIZE         1024

#define SP605_PROBE_MAXPAGES    1
#define SP605_TCP_MAX_SIZE_RX   0x00001000
#define SP605_TCP_MAX_SIZE_TX   0x00001000

#define ENDIAN_SWAP_WORD(x)     (x = (x << 8) | (x >> 8))
#define ENDIAN_SWAP_DWORD(x)    (x = (x << 24) | ((x >> 8) & 0xff00) | ((x << 8) & 0xff0000) | (x >> 24))

#define DEFAULT_PORT 28472

typedef struct tdDEVICE_CONTEXT_SP605_TCP {
    DWORD TcpAddr;
    WORD TcpPort;
    SOCKET Sock;
    WORD wDeviceId;
    PTLP_CALLBACK_BUF_MRd pMRdBuffer;
    struct {
        PBYTE pb;
        DWORD cb;
        DWORD cbMax;
    } rxbuf;
    struct {
        PBYTE pb;
        DWORD cb;
        DWORD cbMax;
    } txbuf;
    VOID(*hRxTlpCallbackFn)(_Inout_ PTLP_CALLBACK_BUF_MRd pBufferMrd, _In_ PBYTE pb, _In_ DWORD cb);
} DEVICE_CONTEXT_SP605_TCP, *PDEVICE_CONTEXT_SP605_TCP;

//-------------------------------------------------------------------------------
// FPGA/SP605/TCP implementation below.
//-------------------------------------------------------------------------------

VOID Util_Split2(_In_ LPSTR sz, CHAR chDelimiter, _Out_writes_(MAX_PATH) PCHAR _szBuf, _Out_ LPSTR *psz1, _Out_ LPSTR *psz2)
{
    DWORD i;
    strcpy_s(_szBuf, MAX_PATH, sz);
    *psz1 = _szBuf;
    for(i = 0; i < MAX_PATH; i++) {
        if('\0' == _szBuf[i]) {
            *psz2 = _szBuf + i;
            return;
        }
        if(chDelimiter == _szBuf[i]) {
            _szBuf[i] = '\0';
            *psz2 = _szBuf + i + 1;
            return;
        }
    }
}

SOCKET Device605_TCP_Connect(_In_ DWORD Addr, _In_ WORD Port)
{
    SOCKET Sock = 0;
    struct sockaddr_in sAddr;
    sAddr.sin_family = AF_INET;
    sAddr.sin_port = htons(Port);
    sAddr.sin_addr.s_addr = Addr;
    if((Sock = socket(AF_INET, SOCK_STREAM, 0)) != INVALID_SOCKET) {
        if(connect(Sock, (struct sockaddr *)&sAddr, sizeof(sAddr)) != SOCKET_ERROR) { return Sock; }
        fprintf(stderr, "ERROR: connect() fails\n");
        closesocket(Sock);
    } else {
        fprintf(stderr, "ERROR: socket() fails\n");
    }
    return 0;
}

WORD Device605_TCP_GetDeviceID(_In_ PDEVICE_CONTEXT_SP605_TCP ctx605)
{
    PCIE_CTL Rx, Tx;
    DWORD cbRead;
    Tx.flags = PCIE_F_STATUS;
    Tx.data = 0;
    if(send(ctx605->Sock, (const char *)&Tx, sizeof(Tx), 0) != sizeof(Tx)) {
        fprintf(stderr, "ERROR: send() fails\n");
        return 0;
    }
    cbRead = 0;
    while(cbRead < sizeof(Rx)) {
        DWORD len = recv(ctx605->Sock, (char *)&Rx + cbRead, sizeof(Rx) - cbRead, 0);
        if(len == SOCKET_ERROR || len == 0) {
            fprintf(stderr, "ERROR: recv() fails\n");
            return 0;
        }
        cbRead += len;
    }
    if(!(Rx.flags & (PCIE_F_STATUS | PCIE_F_HAS_DATA))) { return 0; }
    if(Rx.data == 0) {
        fprintf(stderr, "ERROR: PCI-E endpoint is not configured by root complex yet\n");
    }
    return (WORD)Rx.data;
}

VOID Device605_TCP_Close(_Inout_ PLC_CONTEXT ctxLC)
{
    PDEVICE_CONTEXT_SP605_TCP ctx = (PDEVICE_CONTEXT_SP605_TCP)ctxLC->hDevice;
    if(!ctx) { return; }
    if(ctx->Sock) { closesocket(ctx->Sock); }
    LocalFree(ctx->rxbuf.pb);
    LocalFree(ctx->txbuf.pb);
    LocalFree(ctx);
    ctxLC->hDevice = 0;
}

_Success_(return)
BOOL Device605_TCP_TxTlp(_In_ PLC_CONTEXT ctxLC, _In_ PDEVICE_CONTEXT_SP605_TCP ctx, _In_reads_(cbTlp) PBYTE pbTlp, _In_ DWORD cbTlp, BOOL fFlush)
{
    PBYTE pbTx;
    DWORD i, cbTx, len, Total = 0;
    PCIE_CTL *Tx, *Last = NULL;
    if(cbTlp & 0x3) { return FALSE; }
    if(cbTlp > 2048) { return FALSE; }
    if(ctxLC->fPrintf[LC_PRINTF_VVV]) {
        TLP_Print(pbTlp, cbTlp, TRUE);
    }
    // prepare transmit buffer
    pbTx = ctx->txbuf.pb + ctx->txbuf.cb;
    cbTx = sizeof(PCIE_CTL) * (cbTlp / sizeof(DWORD));
    for(Tx = (PCIE_CTL *)pbTx, i = 0; i < cbTlp; Tx++, i += 4) {
        Tx->data = ENDIAN_SWAP_DWORD(*(PDWORD)(pbTlp + i));
        Tx->flags = PCIE_F_HAS_DATA;
        Last = Tx;
    }
    if(Last) {
        Last->flags |= PCIE_F_TLAST;
    }
    ctx->txbuf.cb += cbTx;
    // transmit
    if(ctx->txbuf.cb && (fFlush || (ctx->txbuf.cb > ctx->txbuf.cbMax - 0x1000))) {
        while(Total < ctx->txbuf.cb) {
            len = send(ctx->Sock, (const char *)(ctx->txbuf.pb + Total), ctx->txbuf.cb - Total, 0);
            if(len == 0 || len == SOCKET_ERROR) {
                fprintf(stderr, "ERROR: send() fails\n");
                return FALSE;
            }
            Total += len;
        }
        ctx->txbuf.cb = 0;
    }
    return TRUE;
}

VOID Device605_TCP_RxTlpSynchronous(_In_ PLC_CONTEXT ctxLC, _In_ PDEVICE_CONTEXT_SP605_TCP ctx)
{
    DWORD i = 0, cdwTlp = 0, Total = 0, len;
    PCIE_CTL *pRx, Tx;
    BYTE pbTlp[TLP_RX_MAX_SIZE];
    PDWORD pdwTlp = (PDWORD)pbTlp;
    //PDWORD pdwRx = (PDWORD)ctx->rxbuf.pb;
    // Request Replies
    Tx.flags = PCIE_F_RECV_REPLY | PCIE_F_TIMEOUT;
    Tx.data = 0;
    len = send(ctx->Sock, (const char *)&Tx, sizeof(Tx), 0);
    if(len == 0 || len == SOCKET_ERROR) {
        fprintf(stderr, "ERROR: send() fails\n");
        return;
    }
    // Receive Data
    ctx->rxbuf.cb = recv(ctx->Sock, ctx->rxbuf.pb, ctx->rxbuf.cbMax, 0);
    pRx = (PCIE_CTL*)ctx->rxbuf.pb;
    for(i = 0; i < ctx->rxbuf.cb; i += sizeof(PCIE_CTL)) {
        if(pRx->flags & PCIE_F_ERROR) {
            fprintf(stderr, "ERROR: failed to receive TLP\n");
            return;
        }
        if(!(pRx->flags & PCIE_F_HAS_DATA)) { return; }
        pdwTlp[cdwTlp] = ENDIAN_SWAP_DWORD(pRx->data);
        cdwTlp++;
        if(pRx->flags & PCIE_F_TLAST) {
            if(cdwTlp >= 3) {
                if(ctxLC->fPrintf[LC_PRINTF_VVV]) {
                    TLP_Print(pbTlp, cdwTlp << 2, FALSE);
                }
                if(ctx->hRxTlpCallbackFn) {
                    ctx->hRxTlpCallbackFn(ctx->pMRdBuffer, pbTlp, cdwTlp << 2);
                }
            } else {
                fprintf(stderr, "WARNING: BAD PCIe TLP RECEIVED! THIS SHOULD NOT HAPPEN!\n");
                return;
            }
            cdwTlp = 0;
        }
        pRx++;
    }
}

VOID Device605_TCP_ReadDMA(_In_ PLC_CONTEXT ctxLC, _In_ QWORD qwAddr, _Out_writes_(cb) PBYTE pb, _In_ DWORD cb)
{
    PDEVICE_CONTEXT_SP605_TCP ctx = (PDEVICE_CONTEXT_SP605_TCP)ctxLC->hDevice;
    TLP_CALLBACK_BUF_MRd rxbuf;
    DWORD tx[4], o = 0, i;
    BOOL is32;
    PTLP_HDR_MRdWr64 hdrRd64 = (PTLP_HDR_MRdWr64)tx;
    PTLP_HDR_MRdWr32 hdrRd32 = (PTLP_HDR_MRdWr32)tx;
    if(cb > SP605_TCP_MAX_SIZE_RX) { return; }
    if(qwAddr % 0x1000) { return; }
    if((cb >= 0x1000) && (cb % 0x1000)) { return; }
    if((cb < 0x1000) && (cb % 0x8)) { return; }
    // prepare
    ctx->pMRdBuffer = &rxbuf;
    ctx->hRxTlpCallbackFn = TLP_CallbackMRd;
    rxbuf.cb = 0;
    rxbuf.pb = pb;
    // transmit TLPs
    while(o < cb) {
        memset(tx, 0, 16);
        is32 = qwAddr + o < 0x100000000;
        if(is32) {
            hdrRd32->h.TypeFmt = TLP_MRd32;
            hdrRd32->h.Length = (TLP_RX_SIZE >> 2);
            hdrRd32->RequesterID = ctx->wDeviceId;
            hdrRd32->Tag = 0;
            hdrRd32->FirstBE = 0xf;
            hdrRd32->LastBE = 0xf;
            hdrRd32->Address = (DWORD)(qwAddr + o);
        } else {
            hdrRd64->h.TypeFmt = TLP_MRd64;
            hdrRd64->h.Length = (TLP_RX_SIZE >> 2);
            hdrRd64->RequesterID = ctx->wDeviceId;
            hdrRd64->Tag = 0;
            hdrRd64->FirstBE = 0xf;
            hdrRd64->LastBE = 0xf;
            hdrRd64->AddressHigh = (DWORD)((qwAddr + o) >> 32);
            hdrRd64->AddressLow = (DWORD)(qwAddr + o);
        }
        for(i = 0; i < 4; i++) {
            ENDIAN_SWAP_DWORD(tx[i]);
        }
        if(!Device605_TCP_TxTlp(ctxLC, ctx, (PBYTE)tx, is32 ? 12 : 16, TRUE)) {
            break;
        }
        o += TLP_RX_SIZE;
        rxbuf.cbMax = o;
        Device605_TCP_RxTlpSynchronous(ctxLC, ctx);
    }
    ctx->hRxTlpCallbackFn = NULL;
    ctx->pMRdBuffer = NULL;
}

VOID Device605_TCP_ReadScatterMEM(_In_ PLC_CONTEXT ctxLC, _In_ DWORD cMEMs, _Inout_ PPMEM_SCATTER ppMEMs)
{
    DWORD i;
    PMEM_SCATTER pMEM;
    // OK to read 1 SCATTER MEM per call to Device605_TCP_ReadDMA - device is
    // so slow anyway so it's not making an impact in reality.
    for(i = 0; i < cMEMs; i++) {
        pMEM = ppMEMs[i];
        if(pMEM->f || MEM_SCATTER_ADDR_ISINVALID(pMEM)) { continue; }
        MEM_SCATTER_STACK_PUSH(pMEM, 0);
        Device605_TCP_ReadDMA(ctxLC, pMEM->qwA, pMEM->pb, pMEM->cb);
        pMEM->f = pMEM->cb == MEM_SCATTER_STACK_POP(pMEM);
    }
}

VOID Device605_TCP_ProbeDMA(_In_ PLC_CONTEXT ctxLC, _In_ QWORD qwAddr, _In_ DWORD cPages, _Out_ __bcount(cPages) PBYTE pbResultMap)
{
    QWORD i;
    DWORD j;
    PDEVICE_CONTEXT_SP605_TCP ctx = (PDEVICE_CONTEXT_SP605_TCP)ctxLC->hDevice;
    TLP_CALLBACK_BUF_MRd bufMRd;
    DWORD tx[4];
    BOOL is32;
    PTLP_HDR_MRdWr64 hdrRd64 = (PTLP_HDR_MRdWr64)tx;
    PTLP_HDR_MRdWr32 hdrRd32 = (PTLP_HDR_MRdWr32)tx;
    // split probe into processing chunks if too large...
    while(cPages > SP605_PROBE_MAXPAGES) {
        Device605_TCP_ProbeDMA(ctxLC, qwAddr, SP605_PROBE_MAXPAGES, pbResultMap);
        cPages -= SP605_PROBE_MAXPAGES;
        pbResultMap += SP605_PROBE_MAXPAGES;
        qwAddr += SP605_PROBE_MAXPAGES << 12;
    }
    memset(pbResultMap, 0, cPages);
    // prepare
    bufMRd.cb = 0;
    bufMRd.pb = pbResultMap;
    bufMRd.cbMax = cPages;
    ctx->pMRdBuffer = &bufMRd;
    ctx->hRxTlpCallbackFn = TLP_CallbackMRdProbe;
    // transmit TLPs
    for(i = 0; i < cPages; i++) {
        memset(tx, 0, 16);
        is32 = qwAddr + (i << 12) < 0x100000000;
        if(is32) {
            hdrRd32->h.TypeFmt = TLP_MRd32;
            hdrRd32->h.Length = 1;
            hdrRd32->RequesterID = ctx->wDeviceId;
            hdrRd32->FirstBE = 0xf;
            hdrRd32->LastBE = 0;
            hdrRd32->Address = (DWORD)(qwAddr + (i << 12) + ((i & 0x1f) << 2)); // 5 low address bits coded into the dword read.
            hdrRd32->Tag = (BYTE)((i >> 5) & 0x1f); // 5 high address bits coded into tag.
        } else {
            hdrRd64->h.TypeFmt = TLP_MRd64;
            hdrRd64->h.Length = 1;
            hdrRd64->RequesterID = ctx->wDeviceId;
            hdrRd64->FirstBE = 0xf;
            hdrRd64->LastBE = 0;
            hdrRd64->AddressHigh = (DWORD)((qwAddr + (i << 12)) >> 32);
            hdrRd64->AddressLow = (DWORD)(qwAddr + (i << 12) + ((i & 0x1f) << 2)); // 5 low address bits coded into the dword read.
            hdrRd64->Tag = (BYTE)((i >> 5) & 0x1f); // 5 high address bits coded into tag.
        }
        for(j = 0; j < 4; j++) {
            ENDIAN_SWAP_DWORD(tx[j]);
        }
        Device605_TCP_TxTlp(ctxLC, ctx, (PBYTE)tx, is32 ? 12 : 16, FALSE);
    }
    Device605_TCP_TxTlp(ctxLC, ctx, NULL, 0, TRUE);
    Device605_TCP_RxTlpSynchronous(ctxLC, ctx);
    ctx->hRxTlpCallbackFn = NULL;
    ctx->pMRdBuffer = NULL;
}

// write max 128 byte packets.
_Success_(return)
BOOL Device605_TCP_WriteDMA_TXP(_In_ PLC_CONTEXT ctxLC, _Inout_ PDEVICE_CONTEXT_SP605_TCP ctx, _In_ QWORD qwA, _In_ BYTE bFirstBE, _In_ BYTE bLastBE, _In_ PBYTE pb, _In_ DWORD cb)
{
    DWORD txbuf[36], i, cbTlp;
    PBYTE pbTlp = (PBYTE)txbuf;
    PTLP_HDR_MRdWr32 hdrWr32 = (PTLP_HDR_MRdWr32)txbuf;
    PTLP_HDR_MRdWr64 hdrWr64 = (PTLP_HDR_MRdWr64)txbuf;
    memset(pbTlp, 0, 16);
    if(qwA < 0x100000000) {
        hdrWr32->h.TypeFmt = TLP_MWr32;
        hdrWr32->h.Length = (WORD)(cb + 3) >> 2;
        hdrWr32->FirstBE = bFirstBE;
        hdrWr32->LastBE = bLastBE;
        hdrWr32->RequesterID = ctx->wDeviceId;
        hdrWr32->Address = (DWORD)qwA;
        for(i = 0; i < 3; i++) {
            ENDIAN_SWAP_DWORD(txbuf[i]);
        }
        memcpy(pbTlp + 12, pb, cb);
        cbTlp = (12 + cb + 3) & ~0x3;
    } else {
        hdrWr64->h.TypeFmt = TLP_MWr64;
        hdrWr64->h.Length = (WORD)(cb + 3) >> 2;
        hdrWr64->FirstBE = bFirstBE;
        hdrWr64->LastBE = bLastBE;
        hdrWr64->RequesterID = ctx->wDeviceId;
        hdrWr64->AddressHigh = (DWORD)(qwA >> 32);
        hdrWr64->AddressLow = (DWORD)qwA;
        for(i = 0; i < 4; i++) {
            ENDIAN_SWAP_DWORD(txbuf[i]);
        }
        memcpy(pbTlp + 16, pb, cb);
        cbTlp = (16 + cb + 3) & ~0x3;
    }
    return Device605_TCP_TxTlp(ctxLC, ctx, pbTlp, cbTlp, FALSE);
}

_Success_(return)
BOOL Device605_TCP_WriteDMA(_In_ PLC_CONTEXT ctxLC, _In_ QWORD qwA, _In_ DWORD cb, _In_reads_(cb) PBYTE pb)
{
    PDEVICE_CONTEXT_SP605_TCP ctx = (PDEVICE_CONTEXT_SP605_TCP)ctxLC->hDevice;
    BOOL result = TRUE;
    BYTE be, pbb[4];
    DWORD cbtx;
    // TX 1st dword if not aligned
    if(cb && (qwA & 0x3)) {
        be = (cb < 3) ? (0xf >> (4 - cb)) : 0xf;
        be <<= qwA & 0x3;
        cbtx = min(cb, 4 - (qwA & 0x3));
        memcpy(pbb + (qwA & 0x3), pb, cbtx);
        result = Device605_TCP_WriteDMA_TXP(ctxLC, ctx, qwA & ~0x3, be, 0, pbb, 4);
        pb += cbtx;
        cb -= cbtx;
        qwA += cbtx;
    }
    // TX as 128-byte packets (aligned to 128-byte boundaries)
    while(result && cb) {
        cbtx = min(128 - (qwA & 0x7f), cb);
        be = (cbtx & 0x3) ? (0xf >> (4 - (cbtx & 0x3))) : 0xf;
        result = (cbtx <= 4) ?
            Device605_TCP_WriteDMA_TXP(ctxLC, ctx, qwA, be, 0, pb, 4) :
            Device605_TCP_WriteDMA_TXP(ctxLC, ctx, qwA, 0xf, be, pb, cbtx);
        pb += cbtx;
        cb -= cbtx;
        qwA += cbtx;
    }
    return Device605_TCP_TxTlp(ctxLC, ctx, NULL, 0, TRUE) && result; // Flush and Return.
}

_Success_(return)
BOOL Device605_TCP_ListenTlp(_In_ PLC_CONTEXT ctxLC, _In_ DWORD dwTime)
{
    PDEVICE_CONTEXT_SP605_TCP ctx = (PDEVICE_CONTEXT_SP605_TCP)ctxLC->hDevice;
    QWORD tmStart = GetTickCount64();
    while(GetTickCount64() - tmStart < dwTime) {
        if(!Device605_TCP_TxTlp(ctxLC, ctx, NULL, 0, TRUE)) {
            return FALSE;
        }
        Sleep(10);
        Device605_TCP_RxTlpSynchronous(ctxLC, ctx);
    }
    return TRUE;
}

_Success_(return)
BOOL Device605_TCP_WriteTlp(_In_ PLC_CONTEXT ctxLC, _In_ PBYTE pbTlp, _In_ DWORD cbTlp)
{
    PDEVICE_CONTEXT_SP605_TCP ctx = (PDEVICE_CONTEXT_SP605_TCP)ctxLC->hDevice;
    return Device605_TCP_TxTlp(ctxLC, ctx, pbTlp, cbTlp, TRUE);
}

_Success_(return)
BOOL Device605_TCP_Command(_In_ PLC_CONTEXT ctxLC, _In_ QWORD fOption, _In_ DWORD cbDataIn, _In_reads_opt_(cbDataIn) PBYTE pbDataIn, _Out_opt_ PBYTE *ppbDataOut, _Out_opt_ PDWORD pcbDataOut)
{
    QWORD qwOptionHi, qwOptionLo;
    qwOptionLo = fOption & 0x00000000ffffffff;
    qwOptionHi = fOption & 0xffffffff00000000;
    if(ppbDataOut) { *ppbDataOut = NULL; }
    if(pcbDataOut) { *pcbDataOut = 0; }
    switch(qwOptionHi) {
        case LC_CMD_FPGA_WRITE_TLP:
            return (cbDataIn >= 12) && !(cbDataIn % 4) && pbDataIn && Device605_TCP_WriteTlp(ctxLC, pbDataIn, cbDataIn);
        case LC_CMD_FPGA_LISTEN_TLP:
            return (cbDataIn == 4) && !(cbDataIn % 4) && pbDataIn && Device605_TCP_ListenTlp(ctxLC, *(PDWORD)pbDataIn);
        case LC_CMD_FPGA_PROBE:
            if(!pbDataIn || !ppbDataOut || (cbDataIn != 8) || (0x01000000 < qwOptionLo /* cPages */)) { return FALSE; }
            if(!(*ppbDataOut = LocalAlloc(LMEM_ZEROINIT, (SIZE_T)qwOptionLo))) { return FALSE; }
            Device605_TCP_ProbeDMA(ctxLC, *(PQWORD)pbDataIn, (DWORD)qwOptionLo, *ppbDataOut);
            if(pcbDataOut) { *pcbDataOut = (DWORD)qwOptionLo; }
            return TRUE;
    }
    return FALSE;
}

_Success_(return)
EXPORTED_FUNCTION BOOL LeechCore_Plugin_Open(_Inout_ PLC_CONTEXT ctxLC)
{
    PDEVICE_CONTEXT_SP605_TCP ctx;
    CHAR _szBuffer[MAX_PATH];
    LPSTR szAddress = NULL, szPort = NULL;
    if(ctxLC->version != LC_CONTEXT_VERSION) { return FALSE; }
#ifdef _WIN32
    WSADATA WsaData;
    if(WSAStartup(MAKEWORD(2, 2), &WsaData)) { return FALSE; }
#endif /* _WIN32 */
    ctx = LocalAlloc(LMEM_ZEROINIT, sizeof(DEVICE_CONTEXT_SP605_TCP));
    if(!ctx) { return FALSE; }
    ctxLC->hDevice = (HANDLE)ctx;
    // retrieve address and optional port from device string sp605tcp://<host>[:port]
    Util_Split2(ctxLC->Config.szDevice + 11, ':', _szBuffer, &szAddress, &szPort);
    ctx->TcpAddr = inet_addr(szAddress);
    ctx->TcpPort = atoi(szPort);
    if(!ctx->TcpAddr || (ctx->TcpAddr == (DWORD)-1)) {
        lcprintf(ctxLC, "SP605TCP: ERROR: cannot resolve IP-address: '%s'\n", szAddress);
        return FALSE;
    }
    if(!ctx->TcpPort) {
        ctx->TcpPort = 28472;
    }
    // open device connection
    if(ctx->TcpPort == 0) { ctx->TcpPort = DEFAULT_PORT; }
    ctx->Sock = Device605_TCP_Connect(ctx->TcpAddr, ctx->TcpPort);
    if(!ctx->Sock) { goto fail; }
    ctx->wDeviceId = Device605_TCP_GetDeviceID(ctx);
    if(!ctx->wDeviceId) { goto fail; }
    ctx->rxbuf.cbMax = (DWORD)(1.5 * SP605_TCP_MAX_SIZE_RX * 0x1000);
    ctx->rxbuf.pb = LocalAlloc(0, ctx->rxbuf.cbMax);
    if(!ctx->rxbuf.pb) { goto fail; }
    ctx->txbuf.cbMax = SP605_TCP_MAX_SIZE_TX + 0x10000;
    ctx->txbuf.pb = LocalAlloc(0, ctx->txbuf.cbMax);
    if(!ctx->txbuf.pb) { goto fail; }
    // set callback functions and fix up config
    ctxLC->Config.fVolatile = TRUE;
    ctxLC->pfnClose = Device605_TCP_Close;
    ctxLC->pfnReadScatter = Device605_TCP_ReadScatterMEM;
    ctxLC->pfnWriteContigious = Device605_TCP_WriteDMA;
    ctxLC->pfnCommand = Device605_TCP_Command;
    lcprintfv(ctxLC, "Device Info: SP605 / MicroBlaze TCP.\n");
    return TRUE;
fail:
    Device605_TCP_Close(ctxLC);
    return FALSE;
}
