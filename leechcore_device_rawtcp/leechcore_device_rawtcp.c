// Contribution by Synacktiv - www.synacktiv.com
// https://www.synacktiv.com/posts/exploit/using-your-bmc-as-a-dma-device-plugging-pcileech-to-hpe-ilo-4.html
//
// leechcore_device_rawtcp.c : implementation related to dummy device backed by a TCP service.
//

#ifdef _WIN32

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <stdio.h>
#pragma comment(lib, "ws2_32.lib")

#endif /* _WIN32 */

#include <leechcore_device.h>
#include "oscompatibility.h"

#define RAWTCP_MAX_SIZE_RX      0x01000000
#define RAWTCP_MAX_SIZE_TX      0x00100000
#define RAWTCP_DEFAULT_PORT           8888

typedef enum tdRawTCPCmd {
	STATUS,
	MEM_READ,
	MEM_WRITE
} RawTCPCmd;

typedef struct tdDEVICE_CONTEXT_RAWTCP {
	DWORD TcpAddr;
	WORD TcpPort;
	SOCKET Sock;
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
	BYTE pbBufferScatterGather[RAWTCP_MAX_SIZE_RX];
} DEVICE_CONTEXT_RAWTCP, *PDEVICE_CONTEXT_RAWTCP;

typedef struct tdRAWTCP_PROTO_PACKET {
	RawTCPCmd cmd;
	QWORD addr;
	QWORD cb;
} RAWTCP_PROTO_PACKET, *PRAWTCP_PROTO_PACKET;

VOID DeviceRawTCP_Util_Split2(_In_ LPSTR sz, CHAR chDelimiter, _Out_writes_(MAX_PATH) PCHAR _szBuf, _Out_ LPSTR *psz1, _Out_ LPSTR *psz2)
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

SOCKET DeviceRawTCP_Connect(_In_ PLC_CONTEXT ctxLC, _In_ DWORD Addr, _In_ WORD Port)
{
	SOCKET Sock = 0;
	struct sockaddr_in sAddr;
	sAddr.sin_family = AF_INET;
	sAddr.sin_port = htons(Port);
	sAddr.sin_addr.s_addr = Addr;
	if((Sock = socket(AF_INET, SOCK_STREAM, 0)) != INVALID_SOCKET) {
		if(connect(Sock, (struct sockaddr *) & sAddr, sizeof(sAddr)) != SOCKET_ERROR) { return Sock; }
		lcprintf(ctxLC, "RAWTCP: ERROR: connect() fails\n");
		closesocket(Sock);
	} else {
		lcprintf(ctxLC, "RAWTCP: ERROR: socket() fails\n");
	}
	return 0;
}

_Success_(return)
BOOL DeviceRawTCP_Status(_In_ PLC_CONTEXT ctxLC, _In_ PDEVICE_CONTEXT_RAWTCP ctxrawtcp)
{
	RAWTCP_PROTO_PACKET Rx = { 0 }, Tx = { 0 };
	DWORD cbRead;
	BYTE ready;
	DWORD len;

	Tx.cmd = STATUS;

	if(send(ctxrawtcp->Sock, (const char *)&Tx, sizeof(Tx), 0) != sizeof(Tx)) {
		lcprintf(ctxLC, "RAWTCP: ERROR: send() fails\n");
		return 0;
	}

	cbRead = 0;
	while(cbRead < sizeof(Rx)) {
		len = recv(ctxrawtcp->Sock, (char *)&Rx + cbRead, sizeof(Rx) - cbRead, 0);
		if(len == SOCKET_ERROR || len == 0) {
			lcprintf(ctxLC, "RAWTCP: ERROR: recv() fails\n");
			return 0;
		}
		cbRead += len;
	}

	len = recv(ctxrawtcp->Sock, (char *)&ready, sizeof(ready), 0);
	if(len == SOCKET_ERROR || len != sizeof(ready)) {
		lcprintf(ctxLC, "RAWTCP: ERROR: recv() fails\n");
		return 0;
	}

	if(Rx.cmd != STATUS || Rx.cb != sizeof(ready)) {
		lcprintf(ctxLC, "RAWTCP: ERROR: Fail getting device status\n");
	}

	return ready != 0;
}

VOID DeviceRawTCP_Close(_Inout_ PLC_CONTEXT ctxLC)
{
	PDEVICE_CONTEXT_RAWTCP ctx = (PDEVICE_CONTEXT_RAWTCP)ctxLC->hDevice;
	if(!ctx) { return; }
	if(ctx->Sock) { closesocket(ctx->Sock); }
	if(ctx->rxbuf.pb) { LocalFree(ctx->rxbuf.pb); }
	if(ctx->txbuf.pb) { LocalFree(ctx->txbuf.pb); }
	LocalFree(ctx);
	ctxLC->hDevice = 0;
}

VOID DeviceRawTCP_ReadContigious(PLC_READ_CONTIGIOUS_CONTEXT ctxRC)
{
	PLC_CONTEXT ctxLC = ctxRC->ctxLC;
	PDEVICE_CONTEXT_RAWTCP ctxrawtcp = (PDEVICE_CONTEXT_RAWTCP)ctxLC->hDevice;
	RAWTCP_PROTO_PACKET Rx = { 0 }, Tx = { 0 };
	DWORD cbRead;
	DWORD len;

	if(ctxRC->cb > RAWTCP_MAX_SIZE_RX) { return; }
	if(ctxRC->paBase % 0x1000) { return; }
	if((ctxRC->cb >= 0x1000) && (ctxRC->cb % 0x1000)) { return; }
	if((ctxRC->cb < 0x1000) && (ctxRC->cb % 0x8)) { return; }

	Tx.cmd = MEM_READ;
	Tx.addr = ctxRC->paBase;
	Tx.cb = ctxRC->cb;

	if(send(ctxrawtcp->Sock, (const char *)&Tx, sizeof(Tx), 0) != sizeof(Tx)) {
		lcprintf(ctxLC, "RAWTCP: ERROR: send() fails\n");
		return;
	}

	cbRead = 0;
	while(cbRead < sizeof(Rx)) {
		len = recv(ctxrawtcp->Sock, (char *)&Rx + cbRead, sizeof(Rx) - cbRead, 0);
		if(len == SOCKET_ERROR || len == 0) {
			lcprintf(ctxLC, "RAWTCP: ERROR: recv() fails\n");
			return;
		}
		cbRead += len;
	}


	cbRead = 0;
	while(cbRead < Rx.cb) {
		len = recv(ctxrawtcp->Sock, (char *)ctxRC->pb + cbRead, (int)(Rx.cb - cbRead), 0);
		if(len == SOCKET_ERROR || len == 0) {
			lcprintf(ctxLC, "RAWTCP: ERROR: recv() fails\n");
			return;
		}
		cbRead += len;
	}

	if(Rx.cmd != MEM_READ) {
		lcprintf(ctxLC, "RAWTCP: ERROR: Memory read fail (0x%x bytes read)\n", cbRead);
	}

	ctxRC->cbRead = (DWORD)Rx.cb;
}

_Success_(return)
BOOL DeviceRawTCP_WriteDMA(_In_ PLC_CONTEXT ctxLC, _In_ QWORD qwAddr, _In_ DWORD cb, _In_reads_(cb) PBYTE pb)
{
	PDEVICE_CONTEXT_RAWTCP ctxrawtcp = (PDEVICE_CONTEXT_RAWTCP)ctxLC->hDevice;
	RAWTCP_PROTO_PACKET Rx = { 0 }, Tx = { 0 };
	DWORD cbRead, cbWritten;
	DWORD len;

	while(cb > RAWTCP_MAX_SIZE_TX) {
		if(!DeviceRawTCP_WriteDMA(ctxLC, qwAddr, RAWTCP_MAX_SIZE_TX, pb)) {
			return FALSE;
		}
		qwAddr += RAWTCP_MAX_SIZE_TX;
		pb = pb + RAWTCP_MAX_SIZE_TX;
		cb -= RAWTCP_MAX_SIZE_TX;
	}

	Tx.cmd = MEM_WRITE;
	Tx.addr = qwAddr;
	Tx.cb = cb;

	if(send(ctxrawtcp->Sock, (const char *)&Tx, sizeof(Tx), 0) != sizeof(Tx)) {
		lcprintf(ctxLC, "RAWTCP: ERROR: send() fails\n");
		return FALSE;
	}

	cbWritten = 0;
	while(cbWritten < cb) {
		len = send(ctxrawtcp->Sock, (char *)pb + cbWritten, cb - cbWritten, 0);
		if(len == SOCKET_ERROR || len == 0) {
			lcprintf(ctxLC, "RAWTCP: ERROR: send() fails\n");
			return FALSE;
		}
		cbWritten += len;
	}


	cbRead = 0;
	while(cbRead < sizeof(Rx)) {
		len = recv(ctxrawtcp->Sock, (char *)&Rx + cbRead, sizeof(Rx) - cbRead, 0);
		if(len == SOCKET_ERROR || len == 0) {
			lcprintf(ctxLC, "RAWTCP: ERROR: recv() fails\n");
			return FALSE;
		}
		cbRead += len;
	}

	if(Rx.cmd != MEM_WRITE) {
		lcprintf(ctxLC, "RAWTCP: ERROR: Memory write fail\n");
	}

	return cbWritten >= cb;
}

_Success_(return)
EXPORTED_FUNCTION BOOL LcPluginCreate(_Inout_ PLC_CONTEXT ctxLC, _Out_opt_ PPLC_CONFIG_ERRORINFO ppLcCreateErrorInfo)
{
	PDEVICE_CONTEXT_RAWTCP ctx;
	CHAR _szBuffer[MAX_PATH];
	LPSTR szAddress = NULL, szPort = NULL;
	if(ppLcCreateErrorInfo) { *ppLcCreateErrorInfo = NULL; }
	if(ctxLC->version != LC_CONTEXT_VERSION) { return FALSE; }
#ifdef _WIN32
	WSADATA WsaData;
	if(WSAStartup(MAKEWORD(2, 2), &WsaData)) { return FALSE; }
#endif /* _WIN32 */
	ctx = LocalAlloc(LMEM_ZEROINIT, sizeof(DEVICE_CONTEXT_RAWTCP));
	if(!ctx) { return FALSE; }
	ctxLC->hDevice = (HANDLE)ctx;
	// retrieve address and optional port from device string rawtcp://<host>[:port]
	DeviceRawTCP_Util_Split2(ctxLC->Config.szDevice + 9, ':', _szBuffer, &szAddress, &szPort);
	ctx->TcpAddr = inet_addr(szAddress);
	ctx->TcpPort = atoi(szPort);
	if(!ctx->TcpAddr || (ctx->TcpAddr == (DWORD)-1)) {
		lcprintf(ctxLC, "RAWTCP: ERROR: cannot resolve IP-address: '%s'\n", szAddress);
		return FALSE;
	}
	if(!ctx->TcpPort) {
		ctx->TcpPort = RAWTCP_DEFAULT_PORT;
	}
	// open device connection
	ctx->Sock = DeviceRawTCP_Connect(ctxLC, ctx->TcpAddr, ctx->TcpPort);
	if(!ctx->Sock) {
		lcprintf(ctxLC, "RAWTCP: ERROR: failed to connect.\n");
		goto fail;
	}
	if(!DeviceRawTCP_Status(ctxLC, ctx)) {
		lcprintf(ctxLC, "RAWTCP: ERROR: remote service is not ready.\n");
		goto fail;
	}
	ctx->rxbuf.cbMax = RAWTCP_MAX_SIZE_RX;
	ctx->rxbuf.pb = LocalAlloc(0, ctx->rxbuf.cbMax);
	if(!ctx->rxbuf.pb) { goto fail; }
	ctx->txbuf.cbMax = RAWTCP_MAX_SIZE_TX;
	ctx->txbuf.pb = LocalAlloc(0, ctx->txbuf.cbMax);
	if(!ctx->txbuf.pb) { goto fail; }
	// set callback functions and fix up config
	ctxLC->Config.fVolatile = TRUE;
	ctxLC->pfnClose = DeviceRawTCP_Close;
	ctxLC->pfnReadContigious = DeviceRawTCP_ReadContigious;
	ctxLC->pfnWriteContigious = DeviceRawTCP_WriteDMA;
	// return
	lcprintfv(ctxLC, "Device Info: Raw TCP.\n");
	return TRUE;
fail:
	DeviceRawTCP_Close(ctxLC);
	return FALSE;
}
