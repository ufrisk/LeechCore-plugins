// tlp.c : implementation of PCIe TLP (transaction layer packets) functionality.
//
// (c) Ulf Frisk, 2017-2020
// Author: Ulf Frisk, pcileech@frizk.net
//
#include "tlp.h"
#include "oscompatibility.h"

#define Util_2HexChar(x) (((((x) & 0xf) <= 9) ? '0' : ('a' - 10)) + ((x) & 0xf))

#define UTIL_PRINTASCII \
    "................................ !\"#$%&'()*+,-./0123456789:;<=>?" \
    "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ " \
    "................................................................" \
    "................................................................" \

BOOL Util_FillHexAscii(_In_ PBYTE pb, _In_ DWORD cb, _In_ DWORD cbInitialOffset, _Inout_opt_ LPSTR sz, _Out_ PDWORD pcsz)
{
    DWORD i, j, o = 0, szMax, iMod;
    // checks
    if((cbInitialOffset > cb) || (cbInitialOffset > 0x1000) || (cbInitialOffset & 0xf)) { return FALSE; }
    *pcsz = szMax = cb * 5 + 80;
    if(cb > szMax) { return FALSE; }
    if(!sz) { return TRUE; }
    // fill buffer with bytes
    for(i = cbInitialOffset; i < cb + ((cb % 16) ? (16 - cb % 16) : 0); i++) {
        // address
        if(0 == i % 16) {
            iMod = i % 0x10000;
            sz[o++] = Util_2HexChar(iMod >> 12);
            sz[o++] = Util_2HexChar(iMod >> 8);
            sz[o++] = Util_2HexChar(iMod >> 4);
            sz[o++] = Util_2HexChar(iMod);
            sz[o++] = ' ';
            sz[o++] = ' ';
            sz[o++] = ' ';
            sz[o++] = ' ';
        } else if(0 == i % 8) {
            sz[o++] = ' ';
        }
        // hex
        if(i < cb) {
            sz[o++] = Util_2HexChar(pb[i] >> 4);
            sz[o++] = Util_2HexChar(pb[i]);
            sz[o++] = ' ';
        } else {
            sz[o++] = ' ';
            sz[o++] = ' ';
            sz[o++] = ' ';
        }
        // ascii
        if(15 == i % 16) {
            sz[o++] = ' ';
            sz[o++] = ' ';
            for(j = i - 15; j <= i; j++) {
                if(j >= cb) {
                    sz[o++] = ' ';
                } else {
                    sz[o++] = UTIL_PRINTASCII[pb[j]];
                }
            }
            sz[o++] = '\n';
        }
    }
    sz[o++] = 0;
    return TRUE;
}

VOID Util_PrintHexAscii(_In_ PBYTE pb, _In_ DWORD cb, _In_ DWORD cbInitialOffset)
{
    DWORD szMax;
    LPSTR sz;
    if(cb > 0x10000) {
        printf("Large output. Only displaying first 65kB.\n");
        cb = 0x10000 - cbInitialOffset;
    }
    Util_FillHexAscii(pb, cb, cbInitialOffset, NULL, &szMax);
    if(!(sz = LocalAlloc(0, szMax))) { return; }
    Util_FillHexAscii(pb, cb, cbInitialOffset, sz, &szMax);
    printf("%s", sz);
    LocalFree(sz);
}

VOID TLP_Print(_In_ PBYTE pbTlp, _In_ DWORD cbTlp, _In_ BOOL isTx)
{
    DWORD i;
    LPSTR tp = "";
    BYTE pb[0x1000];
    PDWORD buf = (PDWORD)pb;
    PTLP_HDR hdr = (PTLP_HDR)pb;
    PTLP_HDR_CplD hdrC;
    PTLP_HDR_MRdWr32 hdrM32;
    PTLP_HDR_MRdWr64 hdrM64;
    PTLP_HDR_Cfg hdrCfg;
    if(cbTlp < 12 || cbTlp > 0x1000 || cbTlp & 0x3) { return; }
    for(i = 0; i < cbTlp; i += 4) {
        buf[i >> 2] = _byteswap_ulong(*(PDWORD)(pbTlp + i));
    }
    if((hdr->TypeFmt == TLP_Cpl) || (hdr->TypeFmt == TLP_CplD) || (hdr->TypeFmt == TLP_CplLk) || (hdr->TypeFmt == TLP_CplDLk)) {
        if(hdr->TypeFmt == TLP_Cpl) { tp = "Cpl:   "; }
        if(hdr->TypeFmt == TLP_CplD) { tp = "CplD:  "; }
        if(hdr->TypeFmt == TLP_CplLk) { tp = "CplLk: "; }
        if(hdr->TypeFmt == TLP_CplDLk) { tp = "CplDLk:"; }
        hdrC = (PTLP_HDR_CplD)pb;
        printf(
            "\n%s: %s Len: %03x ReqID: %04x CplID: %04x Status: %01x BC: %03x Tag: %02x LowAddr: %02x",
            (isTx ? "TX" : "RX"),
            tp,
            hdr->Length,
            hdrC->RequesterID,
            hdrC->CompleterID,
            hdrC->Status,
            hdrC->ByteCount,
            hdrC->Tag,
            hdrC->LowerAddress
        );
    } else if((hdr->TypeFmt == TLP_MRd32) || (hdr->TypeFmt == TLP_MWr32)) {
        hdrM32 = (PTLP_HDR_MRdWr32)pb;
        printf(
            "\n%s: %s Len: %03x ReqID: %04x BE_FL: %01x%01x Tag: %02x Addr: %08x",
            (isTx ? "TX" : "RX"),
            (hdr->TypeFmt == TLP_MRd32) ? "MRd32: " : "MWr32: ",
            hdr->Length,
            hdrM32->RequesterID,
            hdrM32->FirstBE,
            hdrM32->LastBE,
            hdrM32->Tag,
            hdrM32->Address);
    } else if((hdr->TypeFmt == TLP_MRd64) || (hdr->TypeFmt == TLP_MWr64)) {
        hdrM64 = (PTLP_HDR_MRdWr64)pb;
        printf(
            "\n%s: %s Len: %03x ReqID: %04x BE_FL: %01x%01x Tag: %02x Addr: %016llx",
            (isTx ? "TX" : "RX"),
            (hdr->TypeFmt == TLP_MRd64) ? "MRd64: " : "MWr64: ",
            hdr->Length,
            hdrM64->RequesterID,
            hdrM64->FirstBE,
            hdrM64->LastBE,
            hdrM64->Tag,
            ((QWORD)hdrM64->AddressHigh << 32) + hdrM64->AddressLow
        );
    } else if((hdr->TypeFmt == TLP_IORd) || (hdr->TypeFmt == TLP_IOWr)) {
        hdrM32 = (PTLP_HDR_MRdWr32)pb; // same format for IO Rd/Wr
        printf(
            "\n%s: %s Len: %03x ReqID: %04x BE_FL: %01x%01x Tag: %02x Addr: %08x",
            (isTx ? "TX" : "RX"),
            (hdr->TypeFmt == TLP_IORd) ? "IORd:  " : "IOWr:  ",
            hdr->Length,
            hdrM32->RequesterID,
            hdrM32->FirstBE,
            hdrM32->LastBE,
            hdrM32->Tag,
            hdrM32->Address
        );
    } else if((hdr->TypeFmt == TLP_CfgRd0) || (hdr->TypeFmt == TLP_CfgRd1) || (hdr->TypeFmt == TLP_CfgWr0) || (hdr->TypeFmt == TLP_CfgWr1)) {
        if(hdr->TypeFmt == TLP_CfgRd0) { tp = "CfgRd0:"; }
        if(hdr->TypeFmt == TLP_CfgRd1) { tp = "CfgRd1:"; }
        if(hdr->TypeFmt == TLP_CfgWr0) { tp = "CfgWr0:"; }
        if(hdr->TypeFmt == TLP_CfgWr1) { tp = "CfgWr1:"; }
        hdrCfg = (PTLP_HDR_Cfg)pb;
        printf(
            "\n%s: %s Len: %03x ReqID: %04x BE_FL: %01x%01x Tag: %02x Dev: %i:%i.%i ExtRegNum: %01x RegNum: %02x",
            (isTx ? "TX" : "RX"),
            tp,
            hdr->Length,
            hdrCfg->RequesterID,
            hdrCfg->FirstBE,
            hdrCfg->LastBE,
            hdrCfg->Tag,
            hdrCfg->BusNum,
            hdrCfg->DeviceNum,
            hdrCfg->FunctionNum,
            hdrCfg->ExtRegNum,
            hdrCfg->RegNum
        );
    } else {
        printf(
            "\n%s: TLP???: TypeFmt: %02x dwLen: %03x",
            (isTx ? "TX" : "RX"),
            hdr->TypeFmt,
            hdr->Length
        );
    }
    printf("\n");
    Util_PrintHexAscii(pbTlp, cbTlp, 0);
}

VOID TLP_CallbackMRd(_Inout_ PTLP_CALLBACK_BUF_MRd pBufferMRd, _In_ PBYTE pb, _In_ DWORD cb)
{
    PTLP_HDR_CplD hdrC = (PTLP_HDR_CplD)pb;
    PTLP_HDR hdr = (PTLP_HDR)pb;
    PDWORD buf = (PDWORD)pb;
    DWORD o, c;
    buf[0] = _byteswap_ulong(buf[0]);
    if(cb < ((DWORD)hdr->Length << 2) + 12) { return; }
    if((hdr->TypeFmt == TLP_CplD) && pBufferMRd) {
        buf[1] = _byteswap_ulong(buf[1]);
        buf[2] = _byteswap_ulong(buf[2]);
        // NB! read algorithm below only support reading full 4kB pages _or_
        //     partial page if starting at page boundry and read is less than 4kB.
        o = ((DWORD)hdrC->Tag << 12) + min(0x1000, pBufferMRd->cbMax) - (hdrC->ByteCount ? hdrC->ByteCount : 0x1000);
        c = (DWORD)hdr->Length << 2;
        if(cb != c + 12) { return; }
        if(o + c <= pBufferMRd->cbMax) {
            memcpy(pBufferMRd->pb + o, pb + 12, c);
            pBufferMRd->cb += c;
        }
    }
}

VOID TLP_CallbackMRdProbe(_Inout_ PTLP_CALLBACK_BUF_MRd pBufferMRd, _In_ PBYTE pb, _In_ DWORD cb)
{
    PTLP_HDR_CplD hdrC = (PTLP_HDR_CplD)pb;
    PDWORD buf = (PDWORD)pb;
    DWORD i;
    if(cb < 16) { return; } // min size CplD = 16 bytes.
    buf[0] = _byteswap_ulong(buf[0]);
    buf[1] = _byteswap_ulong(buf[1]);
    buf[2] = _byteswap_ulong(buf[2]);
    if((hdrC->h.TypeFmt == TLP_CplD) && pBufferMRd) {
        // 5 low address bits coded into the dword read, 8 high address bits coded into tag.
        i = ((DWORD)hdrC->Tag << 5) + ((hdrC->LowerAddress >> 2) & 0x1f);
        if(i < pBufferMRd->cbMax) {
            pBufferMRd->pb[i] = 1;
            pBufferMRd->cb++;
        }
    }
}

/*
* Generic callback function that may be used by TLP capable devices to aid the
* collection of memory read completions. Receives single TLP packet.
* -- pBufferMrd_Scatter
* -- pb
* -- cb
*/
VOID TLP_CallbackMRd_Scatter(_Inout_ PTLP_CALLBACK_BUF_MRd_SCATTER pBufferMrd_Scatter, _In_ PBYTE pb, _In_ DWORD cb)
{
    PTLP_HDR_CplD hdrC = (PTLP_HDR_CplD)pb;
    PTLP_HDR hdr = (PTLP_HDR)pb;
    PDWORD buf = (PDWORD)pb;
    DWORD o, c, i;
    PMEM_SCATTER pMEM;
    buf[0] = _byteswap_ulong(buf[0]);
    buf[1] = _byteswap_ulong(buf[1]);
    buf[2] = _byteswap_ulong(buf[2]);
    if(cb < ((DWORD)hdr->Length << 2) + 12) { return; }
    if(hdr->TypeFmt == TLP_CplD) {
        if(pBufferMrd_Scatter->bEccBit != (hdrC->Tag >> 7)) { return; } // ECC bit mismatch
        if(pBufferMrd_Scatter->fTiny) {
            // Algoritm: Multiple MRd of size 128 bytes
            i = (hdrC->Tag >> 5) & 0x03;
            if(i >= pBufferMrd_Scatter->cph) { return; }
            pMEM = pBufferMrd_Scatter->pph[i];
            o = (DWORD)MEM_SCATTER_STACK_PEEK(pMEM, 1);
        } else {
            // Algoritm: Single MRd of page (0x1000) or less, multiple CplD.
            i = hdrC->Tag & 0x7f;
            if(i >= pBufferMrd_Scatter->cph) { return; }
            pMEM = pBufferMrd_Scatter->pph[i];
            if(pMEM->cb == 0x1000) {
                o = 0x1000 - (hdrC->ByteCount ? hdrC->ByteCount : 0x1000);
            } else {
                o = (DWORD)MEM_SCATTER_STACK_PEEK(pMEM, 1);
            }
        }
        c = (DWORD)hdr->Length << 2;
        if(o + c > pMEM->cb) { return; }
        memcpy(pMEM->pb + o, pb + 12, c);
        MEM_SCATTER_STACK_ADD(pMEM, 1, c);
        pBufferMrd_Scatter->cbReadTotal += c;
    }
    if((hdr->TypeFmt == TLP_Cpl) && hdrC->Status) {
        if(pBufferMrd_Scatter->bEccBit != (hdrC->Tag >> 7)) { return; } // ECC bit mismatch
        pBufferMrd_Scatter->cbReadTotal += (hdrC->ByteCount ? hdrC->ByteCount : 0x1000);
    }
}
