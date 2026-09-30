/*
 * rar_unpack.c — RAR decompression, converted to C for Rubraview from
 * UnRAR 7.3.1's source by RARLAB (unpack.cpp, unpackinline.cpp,
 * unpack15.cpp, unpack20.cpp, unpack30.cpp, unpack50.cpp, rarvm.cpp,
 * getbits.*), 2026-09-30. Single-threaded, one contiguous window; the
 * logic follows the original line for line, names included, so the two
 * can be read side by side.
 *
 * UnRAR source code may be used in any software to handle RAR archives
 * without limitations free of charge, but cannot be used to develop RAR
 * (WinRAR) compatible archiver and to re-create RAR compression algorithm,
 * which is proprietary. Distribution of modified UnRAR source code in
 * separate form or as a part of other software is permitted, provided that
 * full text of this paragraph, starting from "UnRAR source code" words, is
 * included in license, or in documentation if license is not available,
 * and in source code comments of resulting package.
 */
#include "rar_unpack.h"
#include "Ppmd7.h"
#include <stdlib.h>
#include <string.h>

typedef uint8_t byte;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef int64_t int64;

/* ---- PackDef ---- */
#define MAX_LZ_MATCH       0x1001
#define MAX_INC_LZ_MATCH   (MAX_LZ_MATCH + 3)
#define MAX3_LZ_MATCH      0x101
#define MAX3_INC_LZ_MATCH  (MAX3_LZ_MATCH + 3)
#define LOW_DIST_REP_COUNT 16
#define NC    306
#define DCB   64
#define DCX   80
#define LDC   16
#define RC    44
#define HUFF_TABLE_SIZEB (NC + DCB + RC + LDC)
#define HUFF_TABLE_SIZEX (NC + DCX + RC + LDC)
#define BC    20
#define NC30  299
#define DC30  60
#define LDC30 17
#define RC30  28
#define BC30  20
#define HUFF_TABLE_SIZE30 (NC30 + DC30 + RC30 + LDC30)
#define NC20  298
#define DC20  48
#define RC20  28
#define BC20  19
#define MC20  257
#define LARGEST_TABLE_SIZE 306

enum { FILTER_DELTA = 0, FILTER_E8, FILTER_E8E9, FILTER_ARM, FILTER_AUDIO, FILTER_RGB, FILTER_ITANIUM,
       FILTER_TEXT, FILTER_LONGRANGE, FILTER_EXHAUSTIVE, FILTER_NONE };

#define MAX_QUICK_DECODE_BITS 9
#define MAX_UNPACK_FILTERS    8192
#define MAX3_UNPACK_FILTERS   8192
#define MAX3_UNPACK_CHANNELS  1024
#define MAX_FILTER_BLOCK_SIZE 0x400000
#define UNPACK_MAX_WRITE      0x400000

#define VM_MEMSIZE 0x40000
#define VM_MEMMASK (VM_MEMSIZE - 1)
enum { VMSF_NONE, VMSF_E8, VMSF_E8E9, VMSF_ITANIUM, VMSF_RGB, VMSF_AUDIO, VMSF_DELTA };

#define BITINPUT_MAX_SIZE 0x8000
#define ASIZE(x) (sizeof(x) / sizeof((x)[0]))

static inline size_t Min(size_t a, size_t b) { return a < b ? a : b; }

/* ---- BitInput ---- */
typedef struct BitInput {
    int InAddr, InBit;
    byte *InBuf;
} BitInput;

static inline void bits_init(BitInput *b) { b->InAddr = b->InBit = 0; }
static inline void addbits(BitInput *b, uint Bits) {
    Bits += (uint)b->InBit;
    b->InAddr += (int)(Bits >> 3);
    b->InBit = (int)(Bits & 7);
}
static inline uint getbits(const BitInput *b) {
    uint BitField = (uint)b->InBuf[b->InAddr] << 16;
    BitField |= (uint)b->InBuf[b->InAddr + 1] << 8;
    BitField |= (uint)b->InBuf[b->InAddr + 2];
    BitField >>= (8 - b->InBit);
    return BitField & 0xffff;
}
static inline uint32_t be4(const byte *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline uint getbits32(const BitInput *b) {
    uint32_t BitField = be4(b->InBuf + b->InAddr);
    BitField <<= b->InBit;
    BitField |= (uint)b->InBuf[b->InAddr + 4] >> (8 - b->InBit);
    return BitField;
}
static inline uint64_t getbits64(const BitInput *b) {
    uint64_t BitField = (uint64_t)be4(b->InBuf + b->InAddr) << 32 | be4(b->InBuf + b->InAddr + 4);
    BitField <<= b->InBit;
    BitField |= (uint)b->InBuf[b->InAddr + 8] >> (8 - b->InBit);
    return BitField;
}
#define fgetbits getbits
#define faddbits addbits

static inline uint32_t RawGet4(const byte *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static inline void RawPut4(uint32_t v, byte *p) { p[0] = (byte)v; p[1] = (byte)(v >> 8); p[2] = (byte)(v >> 16); p[3] = (byte)(v >> 24); }

/* ---- tables ---- */
typedef struct DecodeTable {
    uint MaxNum;
    uint DecodeLen[16];
    uint DecodePos[16];
    uint QuickBits;
    byte QuickLen[1 << MAX_QUICK_DECODE_BITS];
    ushort QuickNum[1 << MAX_QUICK_DECODE_BITS];
    ushort DecodeNum[LARGEST_TABLE_SIZE];
} DecodeTable;

typedef struct UnpackBlockHeader {
    int BlockSize, BlockBitSize, BlockStart, HeaderSize;
    bool LastBlockInFile, TablePresent;
} UnpackBlockHeader;

typedef struct UnpackBlockTables {
    DecodeTable LD, DD, LDD, RD, BD;
} UnpackBlockTables;

typedef struct UnpackFilter {
    byte Type, Channels;
    bool NextWindow;
    size_t BlockStart;
    uint BlockLength;
} UnpackFilter;

typedef struct VM_PreparedProgram {
    int Type;
    uint InitR[7];
    byte *FilteredData;
    uint FilteredDataSize;
} VM_PreparedProgram;

typedef struct UnpackFilter30 {
    uint BlockStart, BlockLength;
    bool NextWindow;
    uint ParentFilter;
    VM_PreparedProgram Prg;
} UnpackFilter30;

typedef struct AudioVariables {
    int K1, K2, K3, K4, K5;
    int D1, D2, D3, D4;
    int LastDelta;
    uint Dif[11];
    uint ByteCount;
    int LastChar;
} AudioVariables;

typedef struct ByteReader { IByteIn vt; struct rar_unpack *u; } ByteReader;

struct rar_unpack {
    rar_read_fn read; void *read_ctx;
    rar_write_fn write; void *write_ctx;
    bool write_stopped, failed;

    BitInput Inp;
    byte *Window;
    uint64_t AllocWinSize;
    size_t MaxWinSize, MaxWinMask;
    bool ExtraDist;

    UnpackFilter *Filters; size_t FiltersCount, FiltersCap;
    byte *FilterSrcMemory, *FilterDstMemory; size_t FilterSrcCap, FilterDstCap;

    size_t OldDist[4], OldDistPtr;
    uint LastLength, LastDist;
    size_t UnpPtr, PrevPtr, WrPtr;
    bool FirstWinDone;
    int ReadTop, ReadBorder;
    UnpackBlockHeader BlockHeader;
    UnpackBlockTables BlockTables;
    size_t WriteBorder;
    int64 DestUnpSize;
    bool UnpSomeRead;
    int64 WrittenFileSize;
    bool FileExtracted;

    /* v1.5 */
    ushort ChSet[256], ChSetA[256], ChSetB[256], ChSetC[256];
    byte NToPl[256], NToPlB[256], NToPlC[256];
    uint FlagBuf, AvrPlc, AvrPlcB, AvrLn1, AvrLn2, AvrLn3;
    int Buf60, NumHuf, StMode, LCount, FlagsCnt;
    uint Nhfb, Nlzb, MaxDist3;

    /* v2.0 */
    DecodeTable MD[4];
    byte UnpOldTable20[MC20 * 4];
    bool UnpAudioBlock;
    uint UnpChannels, UnpCurChannel;
    int UnpChannelDelta;
    AudioVariables AudV[4];

    /* v3.0 */
    int PrevLowDist, LowDistRepCount;
    CPpmd7 PPM; bool PPMAllocated, PPMError;
    ByteReader PPMReader;
    int PPMEscChar;
    byte UnpOldTable[HUFF_TABLE_SIZE30];
    int UnpBlockType;
    bool TablesRead2, TablesRead3, TablesRead5;
    byte *VMMem;
    uint VMR[8];
    BitInput VMCodeInp;
    UnpackFilter30 **Filters30; size_t Filters30Count, Filters30Cap;
    UnpackFilter30 **PrgStack; size_t PrgStackCount, PrgStackCap;
    int *OldFilterLengths; size_t OldFilterLengthsCount, OldFilterLengthsCap;
    int LastFilter;
};
typedef struct rar_unpack Unpack;

enum { BLOCK_LZ, BLOCK_PPM };

static inline size_t WrapDown(const Unpack *u, size_t WinPos) { return WinPos >= u->MaxWinSize ? WinPos + u->MaxWinSize : WinPos; }
static inline size_t WrapUp(const Unpack *u, size_t WinPos) { return WinPos >= u->MaxWinSize ? WinPos - u->MaxWinSize : WinPos; }

/* ---- growable arrays ---- */
static bool grow(void **ptr, size_t *cap, size_t need, size_t item) {
    if (need <= *cap) return true;
    size_t n = *cap ? *cap * 2 : 16;
    while (n < need) n *= 2;
    void *p = realloc(*ptr, n * item);
    if (!p) return false;
    *ptr = p;
    *cap = n;
    return true;
}
static bool reserve_bytes(byte **ptr, size_t *cap, size_t need) {
    if (need <= *cap) return true;
    byte *p = (byte*)realloc(*ptr, need);
    if (!p) return false;
    *ptr = p;
    *cap = need;
    return true;
}

/* ---- the IO ---- */
static int UnpRead(Unpack *u, byte *Addr, size_t Count) {
    if (u->failed) return -1;
    return (int)u->read(u->read_ctx, Addr, Count);
}
static void UnpIOWrite(Unpack *u, const byte *Addr, size_t Count) {
    if (u->write_stopped || Count == 0) return;
    if (!u->write(u->write_ctx, Addr, Count)) u->write_stopped = true;
}

/* ---- common ---- */
static void InitFilters(Unpack *u) { u->FiltersCount = 0; }
static void UnpInitData20(Unpack *u, bool Solid);
static void UnpInitData30(Unpack *u, bool Solid);
static void UnpInitData50(Unpack *u, bool Solid) { if (!Solid) u->TablesRead5 = false; }

static void UnpInitData(Unpack *u, bool Solid) {
    if (!Solid) {
        u->OldDist[0] = u->OldDist[1] = u->OldDist[2] = u->OldDist[3] = (size_t)-1;
        u->OldDistPtr = 0;
        u->LastDist = (uint)-1;
        u->LastLength = 0;
        memset(&u->BlockTables, 0, sizeof(u->BlockTables));
        u->UnpPtr = u->WrPtr = 0;
        u->PrevPtr = 0;
        u->FirstWinDone = false;
        u->WriteBorder = Min(u->MaxWinSize, UNPACK_MAX_WRITE);
    }
    InitFilters(u);
    bits_init(&u->Inp);
    u->WrittenFileSize = 0;
    u->ReadTop = 0;
    u->ReadBorder = 0;
    memset(&u->BlockHeader, 0, sizeof(u->BlockHeader));
    u->BlockHeader.BlockSize = -1;
    UnpInitData20(u, Solid);
    UnpInitData30(u, Solid);
    UnpInitData50(u, Solid);
}

static void MakeDecodeTables(byte *LengthTable, DecodeTable *Dec, uint Size) {
    Dec->MaxNum = Size;
    uint LengthCount[16];
    memset(LengthCount, 0, sizeof(LengthCount));
    for (size_t I = 0; I < Size; I++) LengthCount[LengthTable[I] & 0xf]++;
    LengthCount[0] = 0;
    memset(Dec->DecodeNum, 0, Size * sizeof(*Dec->DecodeNum));
    Dec->DecodePos[0] = 0;
    Dec->DecodeLen[0] = 0;
    uint UpperLimit = 0;
    for (size_t I = 1; I < 16; I++) {
        UpperLimit += LengthCount[I];
        uint LeftAligned = UpperLimit << (16 - I);
        UpperLimit *= 2;
        Dec->DecodeLen[I] = LeftAligned;
        Dec->DecodePos[I] = Dec->DecodePos[I - 1] + LengthCount[I - 1];
    }
    uint CopyDecodePos[ASIZE(Dec->DecodePos)];
    memcpy(CopyDecodePos, Dec->DecodePos, sizeof(CopyDecodePos));
    for (uint I = 0; I < Size; I++) {
        byte CurBitLength = LengthTable[I] & 0xf;
        if (CurBitLength != 0) {
            uint LastPos = CopyDecodePos[CurBitLength];
            Dec->DecodeNum[LastPos] = (ushort)I;
            CopyDecodePos[CurBitLength]++;
        }
    }
    switch (Size) {
        case NC: case NC20: case NC30: Dec->QuickBits = MAX_QUICK_DECODE_BITS; break;
        default: Dec->QuickBits = MAX_QUICK_DECODE_BITS > 3 ? MAX_QUICK_DECODE_BITS - 3 : 0; break;
    }
    uint QuickDataSize = 1u << Dec->QuickBits;
    uint CurBitLength = 1;
    for (uint Code = 0; Code < QuickDataSize; Code++) {
        uint BitField = Code << (16 - Dec->QuickBits);
        while (CurBitLength < ASIZE(Dec->DecodeLen) && BitField >= Dec->DecodeLen[CurBitLength]) CurBitLength++;
        Dec->QuickLen[Code] = (byte)CurBitLength;
        uint Dist = BitField - Dec->DecodeLen[CurBitLength - 1];
        Dist >>= (16 - CurBitLength);
        uint Pos;
        if (CurBitLength < ASIZE(Dec->DecodePos) && (Pos = Dec->DecodePos[CurBitLength] + Dist) < Size)
            Dec->QuickNum[Code] = Dec->DecodeNum[Pos];
        else
            Dec->QuickNum[Code] = 0;
    }
}

static inline void InsertOldDist(Unpack *u, size_t Distance) {
    u->OldDist[3] = u->OldDist[2];
    u->OldDist[2] = u->OldDist[1];
    u->OldDist[1] = u->OldDist[0];
    u->OldDist[0] = Distance;
}

static inline void CopyString(Unpack *u, uint Length, size_t Distance) {
    size_t SrcPtr = u->UnpPtr - Distance;
    if (Distance > u->UnpPtr) {
        SrcPtr += u->MaxWinSize;
        if (Distance > u->MaxWinSize || !u->FirstWinDone) {
            while (Length-- > 0) {
                u->Window[u->UnpPtr] = 0;
                u->UnpPtr = WrapUp(u, u->UnpPtr + 1);
            }
            return;
        }
    }
    if (SrcPtr < u->MaxWinSize - MAX_INC_LZ_MATCH && u->UnpPtr < u->MaxWinSize - MAX_INC_LZ_MATCH) {
        byte *Src = u->Window + SrcPtr;
        byte *Dest = u->Window + u->UnpPtr;
        u->UnpPtr += Length;
        while (Length >= 8) {
            Dest[0] = Src[0]; Dest[1] = Src[1]; Dest[2] = Src[2]; Dest[3] = Src[3];
            Dest[4] = Src[4]; Dest[5] = Src[5]; Dest[6] = Src[6]; Dest[7] = Src[7];
            Src += 8; Dest += 8; Length -= 8;
        }
        for (uint i = 0; i < Length; i++) Dest[i] = Src[i];
    } else {
        while (Length-- > 0) {
            u->Window[u->UnpPtr] = u->Window[WrapUp(u, SrcPtr++)];
            u->UnpPtr = WrapUp(u, u->UnpPtr + 1);
        }
    }
}

static inline uint DecodeNumber(BitInput *Inp, DecodeTable *Dec) {
    uint BitField = getbits(Inp) & 0xfffe;
    if (BitField < Dec->DecodeLen[Dec->QuickBits]) {
        uint Code = BitField >> (16 - Dec->QuickBits);
        addbits(Inp, Dec->QuickLen[Code]);
        return Dec->QuickNum[Code];
    }
    uint Bits = 15;
    for (uint I = Dec->QuickBits + 1; I < 15; I++)
        if (BitField < Dec->DecodeLen[I]) { Bits = I; break; }
    addbits(Inp, Bits);
    uint Dist = BitField - Dec->DecodeLen[Bits - 1];
    Dist >>= (16 - Bits);
    uint Pos = Dec->DecodePos[Bits] + Dist;
    if (Pos >= Dec->MaxNum) Pos = 0;
    return Dec->DecodeNum[Pos];
}

static inline uint SlotToLength(BitInput *Inp, uint Slot) {
    uint LBits, Length = 2;
    if (Slot < 8) { LBits = 0; Length += Slot; }
    else { LBits = Slot / 4 - 1; Length += (4 | (Slot & 3)) << LBits; }
    if (LBits > 0) { Length += getbits(Inp) >> (16 - LBits); addbits(Inp, LBits); }
    return Length;
}

static void UnpWriteData(Unpack *u, byte *Data, size_t Size) {
    if (u->WrittenFileSize >= u->DestUnpSize) return;
    size_t WriteSize = Size;
    int64 LeftToWrite = u->DestUnpSize - u->WrittenFileSize;
    if ((int64)WriteSize > LeftToWrite) WriteSize = (size_t)LeftToWrite;
    UnpIOWrite(u, Data, WriteSize);
    u->WrittenFileSize += (int64)Size;
}

static void UnpWriteArea(Unpack *u, size_t StartPtr, size_t EndPtr) {
    if (EndPtr != StartPtr) u->UnpSomeRead = true;
    if (EndPtr < StartPtr) {
        UnpWriteData(u, u->Window + StartPtr, u->MaxWinSize - StartPtr);
        UnpWriteData(u, u->Window, EndPtr);
    } else {
        UnpWriteData(u, u->Window + StartPtr, EndPtr - StartPtr);
    }
}

/* ---- RAR 5.0 ---- */

static bool UnpReadBuf(Unpack *u) {
    int DataSize = u->ReadTop - u->Inp.InAddr;
    if (DataSize < 0) return false;
    u->BlockHeader.BlockSize -= u->Inp.InAddr - u->BlockHeader.BlockStart;
    if (u->Inp.InAddr > BITINPUT_MAX_SIZE / 2) {
        if (DataSize > 0) memmove(u->Inp.InBuf, u->Inp.InBuf + u->Inp.InAddr, (size_t)DataSize);
        u->Inp.InAddr = 0;
        u->ReadTop = DataSize;
    } else {
        DataSize = u->ReadTop;
    }
    int ReadCode = 0;
    if (BITINPUT_MAX_SIZE != DataSize)
        ReadCode = UnpRead(u, u->Inp.InBuf + DataSize, (size_t)(BITINPUT_MAX_SIZE - DataSize));
    if (ReadCode > 0) u->ReadTop += ReadCode;
    u->ReadBorder = u->ReadTop - 30;
    u->BlockHeader.BlockStart = u->Inp.InAddr;
    if (u->BlockHeader.BlockSize != -1) {
        int b = u->BlockHeader.BlockStart + u->BlockHeader.BlockSize - 1;
        if (b < u->ReadBorder) u->ReadBorder = b;
    }
    return ReadCode != -1;
}

static byte GetChar(Unpack *u) {
    if (u->Inp.InAddr > BITINPUT_MAX_SIZE - 30) {
        UnpReadBuf(u);
        if (u->Inp.InAddr >= BITINPUT_MAX_SIZE) return 0;
    }
    return u->Inp.InBuf[u->Inp.InAddr++];
}

static uint ReadFilterData(BitInput *Inp) {
    uint ByteCount = (fgetbits(Inp) >> 14) + 1;
    addbits(Inp, 2);
    uint Data = 0;
    for (uint I = 0; I < ByteCount; I++) {
        Data += (fgetbits(Inp) >> 8) << (I * 8);
        addbits(Inp, 8);
    }
    return Data;
}

static bool ReadFilter(Unpack *u, BitInput *Inp, UnpackFilter *Filter) {
    if (Inp->InAddr > u->ReadTop - 16)
        if (!UnpReadBuf(u)) return false;
    Filter->BlockStart = ReadFilterData(Inp);
    Filter->BlockLength = ReadFilterData(Inp);
    if (Filter->BlockLength > MAX_FILTER_BLOCK_SIZE) Filter->BlockLength = 0;
    Filter->Type = (byte)(fgetbits(Inp) >> 13);
    faddbits(Inp, 3);
    if (Filter->Type == FILTER_DELTA) {
        Filter->Channels = (byte)((fgetbits(Inp) >> 11) + 1);
        faddbits(Inp, 5);
    }
    return true;
}

static void UnpWriteBuf(Unpack *u);

static bool AddFilter(Unpack *u, UnpackFilter *Filter) {
    if (u->FiltersCount >= MAX_UNPACK_FILTERS) {
        UnpWriteBuf(u);
        if (u->FiltersCount >= MAX_UNPACK_FILTERS) InitFilters(u);
    }
    Filter->NextWindow = u->WrPtr != u->UnpPtr && WrapDown(u, u->WrPtr - u->UnpPtr) <= Filter->BlockStart;
    Filter->BlockStart = (Filter->BlockStart + u->UnpPtr) % u->MaxWinSize;
    if (!grow((void**)&u->Filters, &u->FiltersCap, u->FiltersCount + 1, sizeof(UnpackFilter))) return false;
    u->Filters[u->FiltersCount++] = *Filter;
    return true;
}

static byte *ApplyFilter(Unpack *u, byte *Data, uint DataSize, UnpackFilter *Flt) {
    byte *SrcData = Data;
    switch (Flt->Type) {
        case FILTER_E8:
        case FILTER_E8E9: {
            uint FileOffset = (uint)u->WrittenFileSize;
            const uint FileSize = 0x1000000;
            byte CmpByte2 = Flt->Type == FILTER_E8E9 ? 0xe9 : 0xe8;
            for (uint CurPos = 0; CurPos + 4 < DataSize;) {
                byte CurByte = *(Data++);
                CurPos++;
                if (CurByte == 0xe8 || CurByte == CmpByte2) {
                    uint Offset = (CurPos + FileOffset) % FileSize;
                    uint Addr = RawGet4(Data);
                    if ((Addr & 0x80000000) != 0) {
                        if (((Addr + Offset) & 0x80000000) == 0) RawPut4(Addr + FileSize, Data);
                    } else {
                        if (((Addr - FileSize) & 0x80000000) != 0) RawPut4(Addr - Offset, Data);
                    }
                    Data += 4;
                    CurPos += 4;
                }
            }
            return SrcData;
        }
        case FILTER_ARM: {
            uint FileOffset = (uint)u->WrittenFileSize;
            for (uint CurPos = 0; CurPos + 3 < DataSize; CurPos += 4) {
                byte *D = Data + CurPos;
                if (D[3] == 0xeb) {
                    uint Offset = D[0] + (uint)D[1] * 0x100 + (uint)D[2] * 0x10000;
                    Offset -= (FileOffset + CurPos) / 4;
                    D[0] = (byte)Offset;
                    D[1] = (byte)(Offset >> 8);
                    D[2] = (byte)(Offset >> 16);
                }
            }
            return SrcData;
        }
        case FILTER_DELTA: {
            uint Channels = Flt->Channels, SrcPos = 0;
            if (!reserve_bytes(&u->FilterDstMemory, &u->FilterDstCap, DataSize ? DataSize : 1)) return NULL;
            byte *DstData = u->FilterDstMemory;
            for (uint CurChannel = 0; CurChannel < Channels; CurChannel++) {
                byte PrevByte = 0;
                for (uint DestPos = CurChannel; DestPos < DataSize; DestPos += Channels)
                    DstData[DestPos] = (PrevByte = (byte)(PrevByte - Data[SrcPos++]));
            }
            return DstData;
        }
    }
    return NULL;
}

static void UnpWriteBuf(Unpack *u) {
    size_t WrittenBorder = u->WrPtr;
    size_t FullWriteSize = WrapDown(u, u->UnpPtr - WrittenBorder);
    size_t WriteSizeLeft = FullWriteSize;
    bool NotAllFiltersProcessed = false;
    for (size_t I = 0; I < u->FiltersCount; I++) {
        UnpackFilter *flt = &u->Filters[I];
        if (flt->Type == FILTER_NONE) continue;
        if (flt->NextWindow) {
            if (WrapDown(u, flt->BlockStart - u->WrPtr) <= FullWriteSize) flt->NextWindow = false;
            continue;
        }
        size_t BlockStart = flt->BlockStart;
        uint BlockLength = flt->BlockLength;
        if (WrapDown(u, BlockStart - WrittenBorder) < WriteSizeLeft) {
            if (WrittenBorder != BlockStart) {
                UnpWriteArea(u, WrittenBorder, BlockStart);
                WrittenBorder = BlockStart;
                WriteSizeLeft = WrapDown(u, u->UnpPtr - WrittenBorder);
            }
            if (BlockLength <= WriteSizeLeft) {
                if (BlockLength > 0) {
                    size_t BlockEnd = WrapUp(u, BlockStart + BlockLength);
                    if (!reserve_bytes(&u->FilterSrcMemory, &u->FilterSrcCap, BlockLength)) { u->failed = true; return; }
                    byte *Mem = u->FilterSrcMemory;
                    if (BlockStart < BlockEnd || BlockEnd == 0) {
                        memcpy(Mem, u->Window + BlockStart, BlockLength);
                    } else {
                        size_t FirstPartLength = u->MaxWinSize - BlockStart;
                        memcpy(Mem, u->Window + BlockStart, FirstPartLength);
                        memcpy(Mem + FirstPartLength, u->Window, BlockEnd);
                    }
                    byte *OutMem = ApplyFilter(u, Mem, BlockLength, flt);
                    u->Filters[I].Type = FILTER_NONE;
                    if (OutMem != NULL) UnpIOWrite(u, OutMem, BlockLength);
                    u->UnpSomeRead = true;
                    u->WrittenFileSize += BlockLength;
                    WrittenBorder = BlockEnd;
                    WriteSizeLeft = WrapDown(u, u->UnpPtr - WrittenBorder);
                }
            } else {
                u->WrPtr = WrittenBorder;
                for (size_t J = I; J < u->FiltersCount; J++) {
                    UnpackFilter *f = &u->Filters[J];
                    if (f->Type != FILTER_NONE) f->NextWindow = false;
                }
                NotAllFiltersProcessed = true;
                break;
            }
        }
    }
    size_t EmptyCount = 0;
    for (size_t I = 0; I < u->FiltersCount; I++) {
        if (EmptyCount > 0) u->Filters[I - EmptyCount] = u->Filters[I];
        if (u->Filters[I].Type == FILTER_NONE) EmptyCount++;
    }
    if (EmptyCount > 0) u->FiltersCount -= EmptyCount;
    if (!NotAllFiltersProcessed) {
        UnpWriteArea(u, WrittenBorder, u->UnpPtr);
        u->WrPtr = u->UnpPtr;
    }
    u->WriteBorder = WrapUp(u, u->UnpPtr + Min(u->MaxWinSize, UNPACK_MAX_WRITE));
    if (u->WriteBorder == u->UnpPtr ||
        (u->WrPtr != u->UnpPtr && WrapDown(u, u->WrPtr - u->UnpPtr) < WrapDown(u, u->WriteBorder - u->UnpPtr)))
        u->WriteBorder = u->WrPtr;
}

static bool ReadBlockHeader(Unpack *u, BitInput *Inp, UnpackBlockHeader *Header) {
    Header->HeaderSize = 0;
    if (Inp->InAddr > u->ReadTop - 7)
        if (!UnpReadBuf(u)) return false;
    faddbits(Inp, (uint)((8 - Inp->InBit) & 7));
    byte BlockFlags = (byte)(fgetbits(Inp) >> 8);
    faddbits(Inp, 8);
    uint ByteCount = ((BlockFlags >> 3) & 3) + 1;
    if (ByteCount == 4) return false;
    Header->HeaderSize = (int)(2 + ByteCount);
    Header->BlockBitSize = (BlockFlags & 7) + 1;
    byte SavedCheckSum = (byte)(fgetbits(Inp) >> 8);
    faddbits(Inp, 8);
    int BlockSize = 0;
    for (uint I = 0; I < ByteCount; I++) {
        BlockSize += (int)((fgetbits(Inp) >> 8) << (I * 8));
        addbits(Inp, 8);
    }
    Header->BlockSize = BlockSize;
    byte CheckSum = (byte)(0x5a ^ BlockFlags ^ BlockSize ^ (BlockSize >> 8) ^ (BlockSize >> 16));
    if (CheckSum != SavedCheckSum) return false;
    Header->BlockStart = Inp->InAddr;
    int b = Header->BlockStart + Header->BlockSize - 1;
    if (b < u->ReadBorder) u->ReadBorder = b;
    Header->LastBlockInFile = (BlockFlags & 0x40) != 0;
    Header->TablePresent = (BlockFlags & 0x80) != 0;
    return true;
}

static bool ReadTables(Unpack *u, BitInput *Inp, UnpackBlockHeader *Header, UnpackBlockTables *Tables) {
    if (!Header->TablePresent) return true;
    if (Inp->InAddr > u->ReadTop - 25)
        if (!UnpReadBuf(u)) return false;
    byte BitLength[BC];
    for (uint I = 0; I < BC; I++) {
        uint Length = (byte)(fgetbits(Inp) >> 12);
        faddbits(Inp, 4);
        if (Length == 15) {
            uint ZeroCount = (byte)(fgetbits(Inp) >> 12);
            faddbits(Inp, 4);
            if (ZeroCount == 0) {
                BitLength[I] = 15;
            } else {
                ZeroCount += 2;
                while (ZeroCount-- > 0 && I < ASIZE(BitLength)) BitLength[I++] = 0;
                I--;
            }
        } else {
            BitLength[I] = (byte)Length;
        }
    }
    MakeDecodeTables(BitLength, &Tables->BD, BC);
    byte Table[HUFF_TABLE_SIZEX];
    const uint TableSize = u->ExtraDist ? HUFF_TABLE_SIZEX : HUFF_TABLE_SIZEB;
    for (uint I = 0; I < TableSize;) {
        if (Inp->InAddr > u->ReadTop - 5)
            if (!UnpReadBuf(u)) return false;
        uint Number = DecodeNumber(Inp, &Tables->BD);
        if (Number < 16) {
            Table[I] = (byte)Number;
            I++;
        } else if (Number < 18) {
            uint N;
            if (Number == 16) { N = (fgetbits(Inp) >> 13) + 3; faddbits(Inp, 3); }
            else { N = (fgetbits(Inp) >> 9) + 11; faddbits(Inp, 7); }
            if (I == 0) return false;
            while (N-- > 0 && I < TableSize) { Table[I] = Table[I - 1]; I++; }
        } else {
            uint N;
            if (Number == 18) { N = (fgetbits(Inp) >> 13) + 3; faddbits(Inp, 3); }
            else { N = (fgetbits(Inp) >> 9) + 11; faddbits(Inp, 7); }
            while (N-- > 0 && I < TableSize) Table[I++] = 0;
        }
    }
    u->TablesRead5 = true;
    if (Inp->InAddr > u->ReadTop) return false;
    MakeDecodeTables(&Table[0], &Tables->LD, NC);
    uint DCodes = u->ExtraDist ? DCX : DCB;
    MakeDecodeTables(&Table[NC], &Tables->DD, DCodes);
    MakeDecodeTables(&Table[NC + DCodes], &Tables->LDD, LDC);
    MakeDecodeTables(&Table[NC + DCodes + LDC], &Tables->RD, RC);
    return true;
}

static void Unpack5(Unpack *u, bool Solid) {
    u->FileExtracted = true;
    UnpInitData(u, Solid);
    if (!UnpReadBuf(u)) return;
    if (!ReadBlockHeader(u, &u->Inp, &u->BlockHeader) ||
        !ReadTables(u, &u->Inp, &u->BlockHeader, &u->BlockTables) || !u->TablesRead5)
        return;
    BitInput *Inp = &u->Inp;
    while (!u->write_stopped && !u->failed) {
        u->UnpPtr = WrapUp(u, u->UnpPtr);
        u->FirstWinDone |= (u->PrevPtr > u->UnpPtr);
        u->PrevPtr = u->UnpPtr;
        if (Inp->InAddr >= u->ReadBorder) {
            bool FileDone = false;
            while (Inp->InAddr > u->BlockHeader.BlockStart + u->BlockHeader.BlockSize - 1 ||
                   (Inp->InAddr == u->BlockHeader.BlockStart + u->BlockHeader.BlockSize - 1 &&
                    Inp->InBit >= u->BlockHeader.BlockBitSize)) {
                if (u->BlockHeader.LastBlockInFile) { FileDone = true; break; }
                if (!ReadBlockHeader(u, Inp, &u->BlockHeader) || !ReadTables(u, Inp, &u->BlockHeader, &u->BlockTables))
                    return;
            }
            if (FileDone || !UnpReadBuf(u)) break;
        }
        if (WrapDown(u, u->WriteBorder - u->UnpPtr) <= MAX_INC_LZ_MATCH && u->WriteBorder != u->UnpPtr) {
            UnpWriteBuf(u);
            if (u->WrittenFileSize > u->DestUnpSize) return;
        }
        uint MainSlot = DecodeNumber(Inp, &u->BlockTables.LD);
        if (MainSlot < 256) {
            u->Window[u->UnpPtr++] = (byte)MainSlot;
            continue;
        }
        if (MainSlot >= 262) {
            uint Length = SlotToLength(Inp, MainSlot - 262);
            size_t Distance = 1;
            uint DBits, DistSlot = DecodeNumber(Inp, &u->BlockTables.DD);
            if (DistSlot < 4) {
                DBits = 0;
                Distance += DistSlot;
            } else {
                DBits = DistSlot / 2 - 1;
                Distance += (size_t)(2 | (DistSlot & 1)) << DBits;
            }
            if (DBits > 0) {
                if (DBits >= 4) {
                    if (DBits > 4) {
                        if (DBits > 36) Distance += (((size_t)getbits64(Inp)) >> (68 - DBits)) << 4;
                        else Distance += (((size_t)getbits32(Inp)) >> (36 - DBits)) << 4;
                        addbits(Inp, DBits - 4);
                    }
                    uint LowDist = DecodeNumber(Inp, &u->BlockTables.LDD);
                    Distance += LowDist;
                } else {
                    Distance += getbits(Inp) >> (16 - DBits);
                    addbits(Inp, DBits);
                }
            }
            if (Distance > 0x100) {
                Length++;
                if (Distance > 0x2000) {
                    Length++;
                    if (Distance > 0x40000) Length++;
                }
            }
            InsertOldDist(u, Distance);
            u->LastLength = Length;
            CopyString(u, Length, Distance);
            continue;
        }
        if (MainSlot == 256) {
            UnpackFilter Filter;
            memset(&Filter, 0, sizeof(Filter));
            if (!ReadFilter(u, Inp, &Filter) || !AddFilter(u, &Filter)) break;
            continue;
        }
        if (MainSlot == 257) {
            if (u->LastLength != 0) CopyString(u, u->LastLength, u->OldDist[0]);
            continue;
        }
        if (MainSlot < 262) {
            uint DistNum = MainSlot - 258;
            size_t Distance = u->OldDist[DistNum];
            for (uint I = DistNum; I > 0; I--) u->OldDist[I] = u->OldDist[I - 1];
            u->OldDist[0] = Distance;
            uint LengthSlot = DecodeNumber(Inp, &u->BlockTables.RD);
            uint Length = SlotToLength(Inp, LengthSlot);
            u->LastLength = Length;
            CopyString(u, Length, Distance);
            continue;
        }
    }
    UnpWriteBuf(u);
}

/* ---- RAR 3.x: the standard filters UnRAR recognises instead of running a VM ---- */

static uint32_t crc32_table[256];
static void crc32_init(void) {
    if (crc32_table[1]) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc32_table[i] = c;
    }
}
static uint32_t CRC32(uint32_t crc, const byte *p, size_t n) {
    crc32_init();
    for (size_t i = 0; i < n; i++) crc = crc32_table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return crc;
}

static uint VM_ReadData(BitInput *Inp) {
    uint Data = fgetbits(Inp);
    switch (Data & 0xc000) {
        case 0:
            faddbits(Inp, 6);
            return (Data >> 10) & 0xf;
        case 0x4000:
            if ((Data & 0x3c00) == 0) { Data = 0xffffff00 | ((Data >> 2) & 0xff); faddbits(Inp, 14); }
            else { Data = (Data >> 6) & 0xff; faddbits(Inp, 10); }
            return Data;
        case 0x8000:
            faddbits(Inp, 2);
            Data = fgetbits(Inp);
            faddbits(Inp, 16);
            return Data;
        default:
            faddbits(Inp, 2);
            Data = (fgetbits(Inp) << 16);
            faddbits(Inp, 16);
            Data |= fgetbits(Inp);
            faddbits(Inp, 16);
            return Data;
    }
}

static void VM_Prepare(byte *Code, uint CodeSize, VM_PreparedProgram *Prg) {
    byte XorSum = 0;
    for (uint I = 1; I < CodeSize; I++) XorSum ^= Code[I];
    if (XorSum != Code[0]) return;
    static const struct { uint Length; uint32_t CRC; int Type; } StdList[] = {
        { 53, 0xad576887, VMSF_E8 },
        { 57, 0x3cd7e57e, VMSF_E8E9 },
        { 120, 0x3769893f, VMSF_ITANIUM },
        { 29, 0x0e06077d, VMSF_DELTA },
        { 149, 0x1c2c5dc8, VMSF_RGB },
        { 216, 0xbc85e701, VMSF_AUDIO },
    };
    uint32_t CodeCRC = CRC32(0xffffffff, Code, CodeSize) ^ 0xffffffff;
    for (uint I = 0; I < ASIZE(StdList); I++)
        if (StdList[I].CRC == CodeCRC && StdList[I].Length == CodeSize) { Prg->Type = StdList[I].Type; break; }
}

static void VM_SetMemory(Unpack *u, size_t Pos, byte *Data, size_t DataSize) {
    if (Pos < VM_MEMSIZE && Data != u->VMMem + Pos) {
        size_t CopySize = Min(DataSize, VM_MEMSIZE - Pos);
        if (CopySize != 0) memmove(u->VMMem + Pos, Data, CopySize);
    }
}

static uint FilterItanium_GetBits(byte *Data, uint BitPos, uint BitCount) {
    uint InAddr = BitPos / 8;
    uint InBit = BitPos & 7;
    uint BitField = (uint)Data[InAddr++];
    BitField |= (uint)Data[InAddr++] << 8;
    BitField |= (uint)Data[InAddr++] << 16;
    BitField |= (uint)Data[InAddr] << 24;
    BitField >>= InBit;
    return BitField & (0xffffffffu >> (32 - BitCount));
}
static void FilterItanium_SetBits(byte *Data, uint BitField, uint BitPos, uint BitCount) {
    uint InAddr = BitPos / 8;
    uint InBit = BitPos & 7;
    uint AndMask = 0xffffffffu >> (32 - BitCount);
    AndMask = ~(AndMask << InBit);
    BitField <<= InBit;
    for (uint I = 0; I < 4; I++) {
        Data[InAddr + I] &= (byte)AndMask;
        Data[InAddr + I] |= (byte)BitField;
        AndMask = (AndMask >> 8) | 0xff000000;
        BitField >>= 8;
    }
}

static inline int iabs(int v) { return v < 0 ? -v : v; }

static bool VM_ExecuteStandardFilter(Unpack *u, int FilterType) {
    uint *R = u->VMR;
    byte *Mem = u->VMMem;
    switch (FilterType) {
        case VMSF_E8:
        case VMSF_E8E9: {
            byte *Data = Mem;
            uint DataSize = R[4], FileOffset = R[6];
            if (DataSize > VM_MEMSIZE || DataSize < 4) return false;
            const uint FileSize = 0x1000000;
            byte CmpByte2 = FilterType == VMSF_E8E9 ? 0xe9 : 0xe8;
            for (uint CurPos = 0; CurPos < DataSize - 4;) {
                byte CurByte = *(Data++);
                CurPos++;
                if (CurByte == 0xe8 || CurByte == CmpByte2) {
                    uint Offset = CurPos + FileOffset;
                    uint Addr = RawGet4(Data);
                    if ((Addr & 0x80000000) != 0) {
                        if (((Addr + Offset) & 0x80000000) == 0) RawPut4(Addr + FileSize, Data);
                    } else {
                        if (((Addr - FileSize) & 0x80000000) != 0) RawPut4(Addr - Offset, Data);
                    }
                    Data += 4;
                    CurPos += 4;
                }
            }
            break;
        }
        case VMSF_ITANIUM: {
            byte *Data = Mem;
            uint DataSize = R[4], FileOffset = R[6];
            if (DataSize > VM_MEMSIZE || DataSize < 21) return false;
            uint CurPos = 0;
            FileOffset >>= 4;
            while (CurPos < DataSize - 21) {
                int Byte = (Data[0] & 0x1f) - 0x10;
                if (Byte >= 0) {
                    static const byte Masks[16] = { 4, 4, 6, 6, 0, 0, 7, 7, 4, 4, 0, 0, 4, 4, 0, 0 };
                    byte CmdMask = Masks[Byte];
                    if (CmdMask != 0)
                        for (uint I = 0; I <= 2; I++)
                            if (CmdMask & (1 << I)) {
                                uint StartPos = I * 41 + 5;
                                uint OpType = FilterItanium_GetBits(Data, StartPos + 37, 4);
                                if (OpType == 5) {
                                    uint Offset = FilterItanium_GetBits(Data, StartPos + 13, 20);
                                    FilterItanium_SetBits(Data, (Offset - FileOffset) & 0xfffff, StartPos + 13, 20);
                                }
                            }
                }
                Data += 16;
                CurPos += 16;
                FileOffset++;
            }
            break;
        }
        case VMSF_DELTA: {
            uint DataSize = R[4], Channels = R[0], SrcPos = 0, Border = DataSize * 2;
            if (DataSize > VM_MEMSIZE / 2 || Channels > MAX3_UNPACK_CHANNELS || Channels == 0) return false;
            for (uint CurChannel = 0; CurChannel < Channels; CurChannel++) {
                byte PrevByte = 0;
                for (uint DestPos = DataSize + CurChannel; DestPos < Border; DestPos += Channels)
                    Mem[DestPos] = (PrevByte = (byte)(PrevByte - Mem[SrcPos++]));
            }
            break;
        }
        case VMSF_RGB: {
            uint DataSize = R[4], Width = R[0] - 3, PosR = R[1];
            if (DataSize > VM_MEMSIZE / 2 || DataSize < 3 || Width > DataSize || PosR > 2) return false;
            byte *SrcData = Mem, *DestData = SrcData + DataSize;
            const uint Channels = 3;
            for (uint CurChannel = 0; CurChannel < Channels; CurChannel++) {
                uint PrevByte = 0;
                for (uint I = CurChannel; I < DataSize; I += Channels) {
                    uint Predicted;
                    if (I >= Width + 3) {
                        byte *UpperData = DestData + I - Width;
                        uint UpperByte = *UpperData;
                        uint UpperLeftByte = *(UpperData - 3);
                        Predicted = PrevByte + UpperByte - UpperLeftByte;
                        int pa = iabs((int)(Predicted - PrevByte));
                        int pb = iabs((int)(Predicted - UpperByte));
                        int pc = iabs((int)(Predicted - UpperLeftByte));
                        if (pa <= pb && pa <= pc) Predicted = PrevByte;
                        else if (pb <= pc) Predicted = UpperByte;
                        else Predicted = UpperLeftByte;
                    } else {
                        Predicted = PrevByte;
                    }
                    PrevByte = DestData[I] = (byte)(Predicted - *(SrcData++));
                }
            }
            for (uint I = PosR, Border = DataSize - 2; I < Border; I += 3) {
                byte G = DestData[I + 1];
                DestData[I] = (byte)(DestData[I] + G);
                DestData[I + 2] = (byte)(DestData[I + 2] + G);
            }
            break;
        }
        case VMSF_AUDIO: {
            uint DataSize = R[4], Channels = R[0];
            byte *SrcData = Mem, *DestData = SrcData + DataSize;
            if (DataSize > VM_MEMSIZE / 2 || Channels > 128 || Channels == 0) return false;
            for (uint CurChannel = 0; CurChannel < Channels; CurChannel++) {
                uint PrevByte = 0, PrevDelta = 0, Dif[7];
                int D1 = 0, D2 = 0, D3;
                int K1 = 0, K2 = 0, K3 = 0;
                memset(Dif, 0, sizeof(Dif));
                for (uint I = CurChannel, ByteCount = 0; I < DataSize; I += Channels, ByteCount++) {
                    D3 = D2;
                    D2 = (int)PrevDelta - D1;
                    D1 = (int)PrevDelta;
                    uint Predicted = 8 * PrevByte + (uint)(K1 * D1 + K2 * D2 + K3 * D3);
                    Predicted = (Predicted >> 3) & 0xff;
                    uint CurByte = *(SrcData++);
                    Predicted -= CurByte;
                    DestData[I] = (byte)Predicted;
                    PrevDelta = (uint)(signed char)(Predicted - PrevByte);
                    PrevByte = Predicted;
                    int D = (signed char)CurByte;
                    D = (int)((uint)D << 3);
                    Dif[0] += (uint)iabs(D);
                    Dif[1] += (uint)iabs(D - D1);
                    Dif[2] += (uint)iabs(D + D1);
                    Dif[3] += (uint)iabs(D - D2);
                    Dif[4] += (uint)iabs(D + D2);
                    Dif[5] += (uint)iabs(D - D3);
                    Dif[6] += (uint)iabs(D + D3);
                    if ((ByteCount & 0x1f) == 0) {
                        uint MinDif = Dif[0], NumMinDif = 0;
                        Dif[0] = 0;
                        for (uint J = 1; J < ASIZE(Dif); J++) {
                            if (Dif[J] < MinDif) { MinDif = Dif[J]; NumMinDif = J; }
                            Dif[J] = 0;
                        }
                        switch (NumMinDif) {
                            case 1: if (K1 >= -16) K1--; break;
                            case 2: if (K1 < 16) K1++; break;
                            case 3: if (K2 >= -16) K2--; break;
                            case 4: if (K2 < 16) K2++; break;
                            case 5: if (K3 >= -16) K3--; break;
                            case 6: if (K3 < 16) K3++; break;
                        }
                    }
                }
            }
            break;
        }
    }
    return true;
}

static void VM_Execute(Unpack *u, VM_PreparedProgram *Prg) {
    memcpy(u->VMR, Prg->InitR, sizeof(Prg->InitR));
    Prg->FilteredData = NULL;
    if (Prg->Type != VMSF_NONE) {
        bool Success = VM_ExecuteStandardFilter(u, Prg->Type);
        uint BlockSize = Prg->InitR[4] & VM_MEMMASK;
        Prg->FilteredDataSize = BlockSize;
        if (Prg->Type == VMSF_DELTA || Prg->Type == VMSF_RGB || Prg->Type == VMSF_AUDIO)
            Prg->FilteredData = 2 * BlockSize > VM_MEMSIZE || !Success ? u->VMMem : u->VMMem + BlockSize;
        else
            Prg->FilteredData = u->VMMem;
    }
}

/* ---- RAR 3.x: PPMd through the LZMA SDK's Ppmd7 ---- */

static void *ppmd_alloc(ISzAllocPtr p, size_t size) { (void)p; return malloc(size); }
static void ppmd_free(ISzAllocPtr p, void *address) { (void)p; free(address); }
static const ISzAlloc g_ppmd_alloc = { ppmd_alloc, ppmd_free };

static Byte ppmd_read_byte(IByteInPtr p) {
    const ByteReader *r = (const ByteReader*)(const void*)p;
    return GetChar(r->u);
}

static void PPM_CleanUp(Unpack *u) {
    if (u->PPMAllocated) Ppmd7_Free(&u->PPM, &g_ppmd_alloc);
    u->PPMAllocated = false;
}

/* UnRAR's ModelPPM::DecodeInit, onto Ppmd7 as 7-Zip's Rar3 decoder does:
   the flags, the memory and the escape byte, then the range coder's four
   bytes, then (on a reset) a fresh model. */
static bool PPM_DecodeInit(Unpack *u, int *EscChar) {
    int MaxOrder = GetChar(u);
    bool Reset = (MaxOrder & 0x20) != 0;
    int MaxMB = 0;
    if (Reset) MaxMB = GetChar(u);
    else if (!u->PPMAllocated || u->PPMError) return false;
    if (MaxOrder & 0x40) *EscChar = GetChar(u);
    if (Reset) {
        u->PPMError = true;
        MaxOrder = (MaxOrder & 0x1f) + 1;
        if (MaxOrder > 16) MaxOrder = 16 + (MaxOrder - 16) * 3;
        if (MaxOrder == 1) { PPM_CleanUp(u); return false; }
        uint32_t memSize = (uint32_t)(MaxMB + 1) << 20;
        if (!u->PPMAllocated || u->PPM.Size != memSize) {
            PPM_CleanUp(u);
            Ppmd7_Construct(&u->PPM);
            if (!Ppmd7_Alloc(&u->PPM, memSize, &g_ppmd_alloc)) return false;
            u->PPMAllocated = true;
        }
    }
    u->PPMReader.vt.Read = ppmd_read_byte;
    u->PPMReader.u = u;
    u->PPM.rc.dec.Stream = &u->PPMReader.vt;
    (void)Ppmd7a_RangeDec_Init(&u->PPM.rc.dec);
    if (Reset) {
        Ppmd7_Init(&u->PPM, (unsigned)MaxOrder);
        u->PPMError = false;
    }
    return u->PPMAllocated && !u->PPMError;
}

static int PPM_DecodeChar(Unpack *u) {
    int sym = Ppmd7a_DecodeSymbol(&u->PPM);
    if (sym < 0) { u->PPMError = true; return -1; }
    return sym;
}

static int SafePPMDecodeChar(Unpack *u) {
    int Ch = PPM_DecodeChar(u);
    if (Ch == -1) {
        PPM_CleanUp(u);
        u->UnpBlockType = BLOCK_LZ;
    }
    return Ch;
}

/* ---- RAR 3.x ---- */

static void InitFilters30(Unpack *u, bool Solid) {
    if (!Solid) {
        u->OldFilterLengthsCount = 0;
        u->LastFilter = 0;
        for (size_t I = 0; I < u->Filters30Count; I++) free(u->Filters30[I]);
        u->Filters30Count = 0;
    }
    for (size_t I = 0; I < u->PrgStackCount; I++) free(u->PrgStack[I]);
    u->PrgStackCount = 0;
}

static void UnpInitData30(Unpack *u, bool Solid) {
    if (!Solid) {
        u->TablesRead3 = false;
        memset(u->UnpOldTable, 0, sizeof(u->UnpOldTable));
        u->PPMEscChar = 2;
        u->UnpBlockType = BLOCK_LZ;
    }
    InitFilters30(u, Solid);
}

static bool UnpReadBuf30(Unpack *u) {
    int DataSize = u->ReadTop - u->Inp.InAddr;
    if (DataSize < 0) return false;
    if (u->Inp.InAddr > BITINPUT_MAX_SIZE / 2) {
        if (DataSize > 0) memmove(u->Inp.InBuf, u->Inp.InBuf + u->Inp.InAddr, (size_t)DataSize);
        u->Inp.InAddr = 0;
        u->ReadTop = DataSize;
    } else {
        DataSize = u->ReadTop;
    }
    int ReadCode = UnpRead(u, u->Inp.InBuf + DataSize, (size_t)(BITINPUT_MAX_SIZE - DataSize));
    if (ReadCode > 0) u->ReadTop += ReadCode;
    u->ReadBorder = u->ReadTop - 30;
    return ReadCode != -1;
}

static void ExecuteCode(Unpack *u, VM_PreparedProgram *Prg) {
    Prg->InitR[6] = (uint)u->WrittenFileSize;
    VM_Execute(u, Prg);
}

static void UnpWriteBuf30(Unpack *u) {
    uint WrittenBorder = (uint)u->WrPtr;
    uint WriteSize = (uint)((u->UnpPtr - WrittenBorder) & u->MaxWinMask);
    for (size_t I = 0; I < u->PrgStackCount; I++) {
        UnpackFilter30 *flt = u->PrgStack[I];
        if (flt == NULL) continue;
        if (flt->NextWindow) { flt->NextWindow = false; continue; }
        uint BlockStart = flt->BlockStart;
        uint BlockLength = flt->BlockLength;
        if (((BlockStart - WrittenBorder) & u->MaxWinMask) < WriteSize) {
            if (WrittenBorder != BlockStart) {
                UnpWriteArea(u, WrittenBorder, BlockStart);
                WrittenBorder = BlockStart;
                WriteSize = (uint)((u->UnpPtr - WrittenBorder) & u->MaxWinMask);
            }
            if (BlockLength <= WriteSize) {
                uint BlockEnd = (uint)((BlockStart + BlockLength) & u->MaxWinMask);
                if (BlockStart < BlockEnd || BlockEnd == 0) {
                    VM_SetMemory(u, 0, u->Window + BlockStart, BlockLength);
                } else {
                    uint FirstPartLength = (uint)(u->MaxWinSize - BlockStart);
                    VM_SetMemory(u, 0, u->Window + BlockStart, FirstPartLength);
                    VM_SetMemory(u, FirstPartLength, u->Window, BlockEnd);
                }
                VM_PreparedProgram *Prg = &flt->Prg;
                ExecuteCode(u, Prg);
                byte *FilteredData = Prg->FilteredData;
                uint FilteredDataSize = Prg->FilteredDataSize;
                free(u->PrgStack[I]);
                u->PrgStack[I] = NULL;
                while (I + 1 < u->PrgStackCount) {
                    UnpackFilter30 *NextFilter = u->PrgStack[I + 1];
                    if (NextFilter == NULL || NextFilter->BlockStart != BlockStart ||
                        NextFilter->BlockLength != FilteredDataSize || NextFilter->NextWindow)
                        break;
                    VM_SetMemory(u, 0, FilteredData, FilteredDataSize);
                    VM_PreparedProgram *NextPrg = &NextFilter->Prg;
                    ExecuteCode(u, NextPrg);
                    FilteredData = NextPrg->FilteredData;
                    FilteredDataSize = NextPrg->FilteredDataSize;
                    I++;
                    free(u->PrgStack[I]);
                    u->PrgStack[I] = NULL;
                }
                if (FilteredData) UnpIOWrite(u, FilteredData, FilteredDataSize);
                u->UnpSomeRead = true;
                u->WrittenFileSize += FilteredDataSize;
                WrittenBorder = BlockEnd;
                WriteSize = (uint)((u->UnpPtr - WrittenBorder) & u->MaxWinMask);
            } else {
                for (size_t J = I; J < u->PrgStackCount; J++) {
                    UnpackFilter30 *f = u->PrgStack[J];
                    if (f != NULL && f->NextWindow) f->NextWindow = false;
                }
                u->WrPtr = WrittenBorder;
                return;
            }
        }
    }
    UnpWriteArea(u, WrittenBorder, u->UnpPtr);
    u->WrPtr = u->UnpPtr;
}

static bool AddVMCode(Unpack *u, uint FirstByte, byte *Code, uint CodeSize) {
    BitInput *VMCodeInp = &u->VMCodeInp;
    bits_init(VMCodeInp);
    memcpy(VMCodeInp->InBuf, Code, Min(BITINPUT_MAX_SIZE, CodeSize));
    uint FiltPos;
    if ((FirstByte & 0x80) != 0) {
        FiltPos = VM_ReadData(VMCodeInp);
        if (FiltPos == 0) InitFilters30(u, false);
        else FiltPos--;
    } else {
        FiltPos = (uint)u->LastFilter;
    }
    if (FiltPos > u->Filters30Count || FiltPos > u->OldFilterLengthsCount) return false;
    u->LastFilter = (int)FiltPos;
    bool NewFilter = (FiltPos == u->Filters30Count);
    UnpackFilter30 *StackFilter = (UnpackFilter30*)calloc(1, sizeof(UnpackFilter30));
    if (!StackFilter) return false;
    UnpackFilter30 *Filter;
    if (NewFilter) {
        if (FiltPos > MAX3_UNPACK_FILTERS) { free(StackFilter); return false; }
        StackFilter->ParentFilter = (uint)u->Filters30Count;
        Filter = (UnpackFilter30*)calloc(1, sizeof(UnpackFilter30));
        if (!Filter ||
            !grow((void**)&u->Filters30, &u->Filters30Cap, u->Filters30Count + 1, sizeof(UnpackFilter30*)) ||
            !grow((void**)&u->OldFilterLengths, &u->OldFilterLengthsCap, u->OldFilterLengthsCount + 1, sizeof(int))) {
            free(Filter); free(StackFilter);
            return false;
        }
        u->Filters30[u->Filters30Count++] = Filter;
        u->OldFilterLengths[u->OldFilterLengthsCount++] = 0;
    } else {
        Filter = u->Filters30[FiltPos];
        StackFilter->ParentFilter = FiltPos;
    }
    uint EmptyCount = 0;
    for (uint I = 0; I < u->PrgStackCount; I++) {
        u->PrgStack[I - EmptyCount] = u->PrgStack[I];
        if (u->PrgStack[I] == NULL) EmptyCount++;
        if (EmptyCount > 0) u->PrgStack[I] = NULL;
    }
    if (EmptyCount == 0) {
        if (u->PrgStackCount > MAX3_UNPACK_FILTERS ||
            !grow((void**)&u->PrgStack, &u->PrgStackCap, u->PrgStackCount + 1, sizeof(UnpackFilter30*))) {
            free(StackFilter);
            return false;
        }
        u->PrgStack[u->PrgStackCount++] = NULL;
        EmptyCount = 1;
    }
    size_t StackPos = u->PrgStackCount - EmptyCount;
    u->PrgStack[StackPos] = StackFilter;

    uint BlockStart = VM_ReadData(VMCodeInp);
    if ((FirstByte & 0x40) != 0) BlockStart += 258;
    StackFilter->BlockStart = (uint)((BlockStart + u->UnpPtr) & u->MaxWinMask);
    if ((FirstByte & 0x20) != 0) {
        StackFilter->BlockLength = VM_ReadData(VMCodeInp);
        u->OldFilterLengths[FiltPos] = (int)StackFilter->BlockLength;
    } else {
        StackFilter->BlockLength = FiltPos < u->OldFilterLengthsCount ? (uint)u->OldFilterLengths[FiltPos] : 0;
    }
    StackFilter->NextWindow = u->WrPtr != u->UnpPtr && ((u->WrPtr - u->UnpPtr) & u->MaxWinMask) <= BlockStart;
    memset(StackFilter->Prg.InitR, 0, sizeof(StackFilter->Prg.InitR));
    StackFilter->Prg.InitR[4] = StackFilter->BlockLength;
    if ((FirstByte & 0x10) != 0) {
        uint InitMask = fgetbits(VMCodeInp) >> 9;
        faddbits(VMCodeInp, 7);
        for (uint I = 0; I < 7; I++)
            if (InitMask & (1u << I)) StackFilter->Prg.InitR[I] = VM_ReadData(VMCodeInp);
    }
    if (NewFilter) {
        uint VMCodeSize = VM_ReadData(VMCodeInp);
        if (VMCodeSize >= 0x10000 || VMCodeSize == 0 || (uint)VMCodeInp->InAddr + VMCodeSize > CodeSize) return false;
        byte *VMCode = (byte*)malloc(VMCodeSize);
        if (!VMCode) return false;
        for (uint I = 0; I < VMCodeSize; I++) {
            if (VMCodeInp->InAddr + 3 >= BITINPUT_MAX_SIZE) { free(VMCode); return false; }
            VMCode[I] = (byte)(fgetbits(VMCodeInp) >> 8);
            faddbits(VMCodeInp, 8);
        }
        VM_Prepare(VMCode, VMCodeSize, &Filter->Prg);
        free(VMCode);
    }
    StackFilter->Prg.Type = Filter->Prg.Type;
    return true;
}

static bool ReadVMCode(Unpack *u) {
    BitInput *Inp = &u->Inp;
    uint FirstByte = getbits(Inp) >> 8;
    addbits(Inp, 8);
    uint Length = (FirstByte & 7) + 1;
    if (Length == 7) {
        Length = (getbits(Inp) >> 8) + 7;
        addbits(Inp, 8);
    } else if (Length == 8) {
        Length = getbits(Inp);
        addbits(Inp, 16);
    }
    if (Length == 0) return false;
    byte *VMCode = (byte*)malloc(Length);
    if (!VMCode) return false;
    for (uint I = 0; I < Length; I++) {
        if (Inp->InAddr >= u->ReadTop - 1 && !UnpReadBuf30(u) && I < Length - 1) { free(VMCode); return false; }
        VMCode[I] = (byte)(getbits(Inp) >> 8);
        addbits(Inp, 8);
    }
    bool ok = AddVMCode(u, FirstByte, VMCode, Length);
    free(VMCode);
    return ok;
}

static bool ReadVMCodePPM(Unpack *u) {
    uint FirstByte = (uint)SafePPMDecodeChar(u);
    if ((int)FirstByte == -1) return false;
    uint Length = (FirstByte & 7) + 1;
    if (Length == 7) {
        int B1 = SafePPMDecodeChar(u);
        if (B1 == -1) return false;
        Length = (uint)B1 + 7;
    } else if (Length == 8) {
        int B1 = SafePPMDecodeChar(u);
        if (B1 == -1) return false;
        int B2 = SafePPMDecodeChar(u);
        if (B2 == -1) return false;
        Length = (uint)(B1 * 256 + B2);
    }
    if (Length == 0) return false;
    byte *VMCode = (byte*)malloc(Length);
    if (!VMCode) return false;
    for (uint I = 0; I < Length; I++) {
        int Ch = SafePPMDecodeChar(u);
        if (Ch == -1) { free(VMCode); return false; }
        VMCode[I] = (byte)Ch;
    }
    bool ok = AddVMCode(u, FirstByte, VMCode, Length);
    free(VMCode);
    return ok;
}

static bool ReadTables30(Unpack *u) {
    BitInput *Inp = &u->Inp;
    byte BitLength[BC];
    byte Table[HUFF_TABLE_SIZE30];
    if (Inp->InAddr > u->ReadTop - 25)
        if (!UnpReadBuf30(u)) return false;
    faddbits(Inp, (uint)((8 - Inp->InBit) & 7));
    uint BitField = fgetbits(Inp);
    if (BitField & 0x8000) {
        u->UnpBlockType = BLOCK_PPM;
        return PPM_DecodeInit(u, &u->PPMEscChar);
    }
    u->UnpBlockType = BLOCK_LZ;
    u->PrevLowDist = 0;
    u->LowDistRepCount = 0;
    if (!(BitField & 0x4000)) memset(u->UnpOldTable, 0, sizeof(u->UnpOldTable));
    faddbits(Inp, 2);
    for (uint I = 0; I < BC; I++) {
        uint Length = (byte)(fgetbits(Inp) >> 12);
        faddbits(Inp, 4);
        if (Length == 15) {
            uint ZeroCount = (byte)(fgetbits(Inp) >> 12);
            faddbits(Inp, 4);
            if (ZeroCount == 0) {
                BitLength[I] = 15;
            } else {
                ZeroCount += 2;
                while (ZeroCount-- > 0 && I < ASIZE(BitLength)) BitLength[I++] = 0;
                I--;
            }
        } else {
            BitLength[I] = (byte)Length;
        }
    }
    MakeDecodeTables(BitLength, &u->BlockTables.BD, BC30);
    const uint TableSize = HUFF_TABLE_SIZE30;
    for (uint I = 0; I < TableSize;) {
        if (Inp->InAddr > u->ReadTop - 5)
            if (!UnpReadBuf30(u)) return false;
        uint Number = DecodeNumber(Inp, &u->BlockTables.BD);
        if (Number < 16) {
            Table[I] = (byte)((Number + u->UnpOldTable[I]) & 0xf);
            I++;
        } else if (Number < 18) {
            uint N;
            if (Number == 16) { N = (fgetbits(Inp) >> 13) + 3; faddbits(Inp, 3); }
            else { N = (fgetbits(Inp) >> 9) + 11; faddbits(Inp, 7); }
            if (I == 0) return false;
            while (N-- > 0 && I < TableSize) { Table[I] = Table[I - 1]; I++; }
        } else {
            uint N;
            if (Number == 18) { N = (fgetbits(Inp) >> 13) + 3; faddbits(Inp, 3); }
            else { N = (fgetbits(Inp) >> 9) + 11; faddbits(Inp, 7); }
            while (N-- > 0 && I < TableSize) Table[I++] = 0;
        }
    }
    u->TablesRead3 = true;
    if (Inp->InAddr > u->ReadTop) return false;
    MakeDecodeTables(&Table[0], &u->BlockTables.LD, NC30);
    MakeDecodeTables(&Table[NC30], &u->BlockTables.DD, DC30);
    MakeDecodeTables(&Table[NC30 + DC30], &u->BlockTables.LDD, LDC30);
    MakeDecodeTables(&Table[NC30 + DC30 + LDC30], &u->BlockTables.RD, RC30);
    memcpy(u->UnpOldTable, Table, sizeof(u->UnpOldTable));
    return true;
}

static bool ReadEndOfBlock(Unpack *u) {
    uint BitField = getbits(&u->Inp);
    bool NewTable, NewFile = false;
    if ((BitField & 0x8000) != 0) {
        NewTable = true;
        addbits(&u->Inp, 1);
    } else {
        NewFile = true;
        NewTable = (BitField & 0x4000) != 0;
        addbits(&u->Inp, 2);
    }
    u->TablesRead3 = !NewTable;
    if (NewFile) return false;
    return ReadTables30(u);
}

static void Unpack29(Unpack *u, bool Solid) {
    static const unsigned char LDecode[] = { 0,1,2,3,4,5,6,7,8,10,12,14,16,20,24,28,32,40,48,56,64,80,96,112,128,160,192,224 };
    static const unsigned char LBits[] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5 };
    static int DDecode[DC30];
    static byte DBits[DC30];
    static const int DBitLengthCounts[] = { 4,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,14,0,12 };
    static const unsigned char SDDecode[] = { 0,4,8,16,32,64,128,192 };
    static const unsigned char SDBits[] = { 2,2,3,4,5,6,6,6 };
    uint Bits;
    if (DDecode[1] == 0) {
        int Dist = 0, BitLength = 0, Slot = 0;
        for (int I = 0; I < (int)ASIZE(DBitLengthCounts); I++, BitLength++)
            for (int J = 0; J < DBitLengthCounts[I]; J++, Slot++, Dist += (1 << BitLength)) {
                DDecode[Slot] = Dist;
                DBits[Slot] = (byte)BitLength;
            }
    }
    u->FileExtracted = true;
    UnpInitData(u, Solid);
    if (!UnpReadBuf30(u)) return;
    if ((!Solid || !u->TablesRead3) && !ReadTables30(u)) return;
    BitInput *Inp = &u->Inp;
    while (!u->write_stopped && !u->failed) {
        u->UnpPtr &= u->MaxWinMask;
        u->FirstWinDone |= (u->PrevPtr > u->UnpPtr);
        u->PrevPtr = u->UnpPtr;
        if (Inp->InAddr > u->ReadBorder)
            if (!UnpReadBuf30(u)) break;
        if (((u->WrPtr - u->UnpPtr) & u->MaxWinMask) <= MAX3_INC_LZ_MATCH && u->WrPtr != u->UnpPtr) {
            UnpWriteBuf30(u);
            if (u->WrittenFileSize > u->DestUnpSize) return;
        }
        if (u->UnpBlockType == BLOCK_PPM) {
            int Ch = PPM_DecodeChar(u);
            if (Ch == -1) {
                PPM_CleanUp(u);
                u->UnpBlockType = BLOCK_LZ;
                break;
            }
            if (Ch == u->PPMEscChar) {
                int NextCh = SafePPMDecodeChar(u);
                if (NextCh == 0) {
                    if (!ReadTables30(u)) break;
                    continue;
                }
                if (NextCh == -1) break;
                if (NextCh == 2) break;
                if (NextCh == 3) {
                    if (!ReadVMCodePPM(u)) break;
                    continue;
                }
                if (NextCh == 4) {
                    uint Distance = 0, Length = 0;
                    bool Failed = false;
                    for (int I = 0; I < 4 && !Failed; I++) {
                        int C = SafePPMDecodeChar(u);
                        if (C == -1) Failed = true;
                        else if (I == 3) Length = (byte)C;
                        else Distance = (Distance << 8) + (byte)C;
                    }
                    if (Failed) break;
                    CopyString(u, Length + 32, Distance + 2);
                    continue;
                }
                if (NextCh == 5) {
                    int Length = SafePPMDecodeChar(u);
                    if (Length == -1) break;
                    CopyString(u, (uint)Length + 4, 1);
                    continue;
                }
            }
            u->Window[u->UnpPtr++] = (byte)Ch;
            continue;
        }
        uint Number = DecodeNumber(Inp, &u->BlockTables.LD);
        if (Number < 256) {
            u->Window[u->UnpPtr++] = (byte)Number;
            continue;
        }
        if (Number >= 271) {
            uint Length = LDecode[Number -= 271] + 3;
            if ((Bits = LBits[Number]) > 0) {
                Length += getbits(Inp) >> (16 - Bits);
                addbits(Inp, Bits);
            }
            uint DistNumber = DecodeNumber(Inp, &u->BlockTables.DD);
            uint Distance = (uint)DDecode[DistNumber] + 1;
            if ((Bits = DBits[DistNumber]) > 0) {
                if (DistNumber > 9) {
                    if (Bits > 4) {
                        Distance += ((getbits(Inp) >> (20 - Bits)) << 4);
                        addbits(Inp, Bits - 4);
                    }
                    if (u->LowDistRepCount > 0) {
                        u->LowDistRepCount--;
                        Distance += (uint)u->PrevLowDist;
                    } else {
                        uint LowDist = DecodeNumber(Inp, &u->BlockTables.LDD);
                        if (LowDist == 16) {
                            u->LowDistRepCount = LOW_DIST_REP_COUNT - 1;
                            Distance += (uint)u->PrevLowDist;
                        } else {
                            Distance += LowDist;
                            u->PrevLowDist = (int)LowDist;
                        }
                    }
                } else {
                    Distance += getbits(Inp) >> (16 - Bits);
                    addbits(Inp, Bits);
                }
            }
            if (Distance >= 0x2000) {
                Length++;
                if (Distance >= 0x40000) Length++;
            }
            InsertOldDist(u, Distance);
            u->LastLength = Length;
            CopyString(u, Length, Distance);
            continue;
        }
        if (Number == 256) {
            if (!ReadEndOfBlock(u)) break;
            continue;
        }
        if (Number == 257) {
            if (!ReadVMCode(u)) break;
            continue;
        }
        if (Number == 258) {
            if (u->LastLength != 0) CopyString(u, u->LastLength, u->OldDist[0]);
            continue;
        }
        if (Number < 263) {
            uint DistNum = Number - 259;
            uint Distance = (uint)u->OldDist[DistNum];
            for (uint I = DistNum; I > 0; I--) u->OldDist[I] = u->OldDist[I - 1];
            u->OldDist[0] = Distance;
            uint LengthNumber = DecodeNumber(Inp, &u->BlockTables.RD);
            int Length = LDecode[LengthNumber] + 2;
            if ((Bits = LBits[LengthNumber]) > 0) {
                Length += (int)(getbits(Inp) >> (16 - Bits));
                addbits(Inp, Bits);
            }
            u->LastLength = (uint)Length;
            CopyString(u, (uint)Length, Distance);
            continue;
        }
        if (Number < 272) {
            uint Distance = SDDecode[Number -= 263] + 1;
            if ((Bits = SDBits[Number]) > 0) {
                Distance += getbits(Inp) >> (16 - Bits);
                addbits(Inp, Bits);
            }
            InsertOldDist(u, Distance);
            u->LastLength = 2;
            CopyString(u, 2, Distance);
            continue;
        }
    }
    UnpWriteBuf30(u);
}

/* ---- RAR 2.x ---- */

static void UnpInitData20(Unpack *u, bool Solid) {
    if (!Solid) {
        u->TablesRead2 = false;
        u->UnpAudioBlock = false;
        u->UnpChannelDelta = 0;
        u->UnpCurChannel = 0;
        u->UnpChannels = 1;
        memset(u->AudV, 0, sizeof(u->AudV));
        memset(u->UnpOldTable20, 0, sizeof(u->UnpOldTable20));
        memset(u->MD, 0, sizeof(u->MD));
    }
}

static void CopyString20(Unpack *u, uint Length, uint Distance) {
    u->LastDist = Distance;
    u->OldDist[u->OldDistPtr++] = Distance;
    u->OldDistPtr = u->OldDistPtr & 3;
    u->LastLength = Length;
    u->DestUnpSize -= Length;
    CopyString(u, Length, Distance);
}

static void UnpWriteBuf20(Unpack *u) {
    if (u->UnpPtr != u->WrPtr) u->UnpSomeRead = true;
    if (u->UnpPtr < u->WrPtr) {
        UnpIOWrite(u, &u->Window[u->WrPtr], (size_t)(-(int64)u->WrPtr) & u->MaxWinMask);
        UnpIOWrite(u, u->Window, u->UnpPtr);
    } else {
        UnpIOWrite(u, &u->Window[u->WrPtr], u->UnpPtr - u->WrPtr);
    }
    u->WrPtr = u->UnpPtr;
}

static bool ReadTables20(Unpack *u) {
    BitInput *Inp = &u->Inp;
    byte BitLength[BC20];
    byte Table[MC20 * 4];
    if (Inp->InAddr > u->ReadTop - 25)
        if (!UnpReadBuf(u)) return false;
    uint BitField = getbits(Inp);
    u->UnpAudioBlock = (BitField & 0x8000) != 0;
    if (!(BitField & 0x4000)) memset(u->UnpOldTable20, 0, sizeof(u->UnpOldTable20));
    addbits(Inp, 2);
    uint TableSize;
    if (u->UnpAudioBlock) {
        u->UnpChannels = ((BitField >> 12) & 3) + 1;
        if (u->UnpCurChannel >= u->UnpChannels) u->UnpCurChannel = 0;
        addbits(Inp, 2);
        TableSize = MC20 * u->UnpChannels;
    } else {
        TableSize = NC20 + DC20 + RC20;
    }
    for (uint I = 0; I < BC20; I++) {
        BitLength[I] = (byte)(getbits(Inp) >> 12);
        addbits(Inp, 4);
    }
    MakeDecodeTables(BitLength, &u->BlockTables.BD, BC20);
    for (uint I = 0; I < TableSize;) {
        if (Inp->InAddr > u->ReadTop - 5)
            if (!UnpReadBuf(u)) return false;
        uint Number = DecodeNumber(Inp, &u->BlockTables.BD);
        if (Number < 16) {
            Table[I] = (byte)((Number + u->UnpOldTable20[I]) & 0xf);
            I++;
        } else if (Number == 16) {
            uint N = (getbits(Inp) >> 14) + 3;
            addbits(Inp, 2);
            if (I == 0) return false;
            while (N-- > 0 && I < TableSize) { Table[I] = Table[I - 1]; I++; }
        } else {
            uint N;
            if (Number == 17) { N = (getbits(Inp) >> 13) + 3; addbits(Inp, 3); }
            else { N = (getbits(Inp) >> 9) + 11; addbits(Inp, 7); }
            while (N-- > 0 && I < TableSize) Table[I++] = 0;
        }
    }
    u->TablesRead2 = true;
    if (Inp->InAddr > u->ReadTop) return true;
    if (u->UnpAudioBlock) {
        for (uint I = 0; I < u->UnpChannels; I++) MakeDecodeTables(&Table[I * MC20], &u->MD[I], MC20);
    } else {
        MakeDecodeTables(&Table[0], &u->BlockTables.LD, NC20);
        MakeDecodeTables(&Table[NC20], &u->BlockTables.DD, DC20);
        MakeDecodeTables(&Table[NC20 + DC20], &u->BlockTables.RD, RC20);
    }
    memcpy(u->UnpOldTable20, Table, TableSize);
    return true;
}

static void ReadLastTables(Unpack *u) {
    if (u->ReadTop >= u->Inp.InAddr + 5) {
        if (u->UnpAudioBlock) {
            if (DecodeNumber(&u->Inp, &u->MD[u->UnpCurChannel]) == 256) ReadTables20(u);
        } else {
            if (DecodeNumber(&u->Inp, &u->BlockTables.LD) == 269) ReadTables20(u);
        }
    }
}

static byte DecodeAudio(Unpack *u, int Delta) {
    AudioVariables *V = &u->AudV[u->UnpCurChannel];
    V->ByteCount++;
    V->D4 = V->D3;
    V->D3 = V->D2;
    V->D2 = V->LastDelta - V->D1;
    V->D1 = V->LastDelta;
    int PCh = 8 * V->LastChar + V->K1 * V->D1 + V->K2 * V->D2 + V->K3 * V->D3 + V->K4 * V->D4 + V->K5 * u->UnpChannelDelta;
    PCh = (PCh >> 3) & 0xFF;
    uint Ch = (uint)(PCh - Delta);
    int D = (signed char)Delta;
    D = (int)((uint)D << 3);
    V->Dif[0] += (uint)iabs(D);
    V->Dif[1] += (uint)iabs(D - V->D1);
    V->Dif[2] += (uint)iabs(D + V->D1);
    V->Dif[3] += (uint)iabs(D - V->D2);
    V->Dif[4] += (uint)iabs(D + V->D2);
    V->Dif[5] += (uint)iabs(D - V->D3);
    V->Dif[6] += (uint)iabs(D + V->D3);
    V->Dif[7] += (uint)iabs(D - V->D4);
    V->Dif[8] += (uint)iabs(D + V->D4);
    V->Dif[9] += (uint)iabs(D - u->UnpChannelDelta);
    V->Dif[10] += (uint)iabs(D + u->UnpChannelDelta);
    u->UnpChannelDelta = V->LastDelta = (signed char)(Ch - (uint)V->LastChar);
    V->LastChar = (int)Ch;
    if ((V->ByteCount & 0x1F) == 0) {
        uint MinDif = V->Dif[0], NumMinDif = 0;
        V->Dif[0] = 0;
        for (uint I = 1; I < ASIZE(V->Dif); I++) {
            if (V->Dif[I] < MinDif) { MinDif = V->Dif[I]; NumMinDif = I; }
            V->Dif[I] = 0;
        }
        switch (NumMinDif) {
            case 1: if (V->K1 >= -16) V->K1--; break;
            case 2: if (V->K1 < 16) V->K1++; break;
            case 3: if (V->K2 >= -16) V->K2--; break;
            case 4: if (V->K2 < 16) V->K2++; break;
            case 5: if (V->K3 >= -16) V->K3--; break;
            case 6: if (V->K3 < 16) V->K3++; break;
            case 7: if (V->K4 >= -16) V->K4--; break;
            case 8: if (V->K4 < 16) V->K4++; break;
            case 9: if (V->K5 >= -16) V->K5--; break;
            case 10: if (V->K5 < 16) V->K5++; break;
        }
    }
    return (byte)Ch;
}

static void Unpack20(Unpack *u, bool Solid) {
    static const unsigned char LDecode[] = { 0,1,2,3,4,5,6,7,8,10,12,14,16,20,24,28,32,40,48,56,64,80,96,112,128,160,192,224 };
    static const unsigned char LBits[] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5 };
    static const uint DDecode[] = { 0,1,2,3,4,6,8,12,16,24,32,48,64,96,128,192,256,384,512,768,1024,1536,2048,3072,4096,6144,8192,12288,16384,24576,32768U,49152U,65536,98304,131072,196608,262144,327680,393216,458752,524288,589824,655360,720896,786432,851968,917504,983040 };
    static const unsigned char DBits[] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13,14,14,15,15,16,16,16,16,16,16,16,16,16,16,16,16,16,16 };
    static const unsigned char SDDecode[] = { 0,4,8,16,32,64,128,192 };
    static const unsigned char SDBits[] = { 2,2,3,4,5,6,6,6 };
    uint Bits;
    UnpInitData(u, Solid);
    if (!UnpReadBuf(u)) return;
    if ((!Solid || !u->TablesRead2) && !ReadTables20(u)) return;
    --u->DestUnpSize;
    BitInput *Inp = &u->Inp;
    while (u->DestUnpSize >= 0 && !u->write_stopped && !u->failed) {
        u->UnpPtr &= u->MaxWinMask;
        u->FirstWinDone |= (u->PrevPtr > u->UnpPtr);
        u->PrevPtr = u->UnpPtr;
        if (Inp->InAddr > u->ReadTop - 30)
            if (!UnpReadBuf(u)) break;
        if (((u->WrPtr - u->UnpPtr) & u->MaxWinMask) < 270 && u->WrPtr != u->UnpPtr) UnpWriteBuf20(u);
        if (u->UnpAudioBlock) {
            uint AudioNumber = DecodeNumber(Inp, &u->MD[u->UnpCurChannel]);
            if (AudioNumber == 256) {
                if (!ReadTables20(u)) break;
                continue;
            }
            u->Window[u->UnpPtr++] = DecodeAudio(u, (int)AudioNumber);
            if (++u->UnpCurChannel == u->UnpChannels) u->UnpCurChannel = 0;
            --u->DestUnpSize;
            continue;
        }
        uint Number = DecodeNumber(Inp, &u->BlockTables.LD);
        if (Number < 256) {
            u->Window[u->UnpPtr++] = (byte)Number;
            --u->DestUnpSize;
            continue;
        }
        if (Number > 269) {
            uint Length = LDecode[Number -= 270] + 3;
            if ((Bits = LBits[Number]) > 0) {
                Length += getbits(Inp) >> (16 - Bits);
                addbits(Inp, Bits);
            }
            uint DistNumber = DecodeNumber(Inp, &u->BlockTables.DD);
            if (DistNumber >= ASIZE(DDecode)) DistNumber = 0;   /* DC20 codes only */
            uint Distance = DDecode[DistNumber] + 1;
            if ((Bits = DBits[DistNumber]) > 0) {
                Distance += getbits(Inp) >> (16 - Bits);
                addbits(Inp, Bits);
            }
            if (Distance >= 0x2000) {
                Length++;
                if (Distance >= 0x40000L) Length++;
            }
            CopyString20(u, Length, Distance);
            continue;
        }
        if (Number == 269) {
            if (!ReadTables20(u)) break;
            continue;
        }
        if (Number == 256) {
            CopyString20(u, u->LastLength, u->LastDist);
            continue;
        }
        if (Number < 261) {
            uint Distance = (uint)u->OldDist[(u->OldDistPtr - (Number - 256)) & 3];
            uint LengthNumber = DecodeNumber(Inp, &u->BlockTables.RD);
            uint Length = LDecode[LengthNumber] + 2;
            if ((Bits = LBits[LengthNumber]) > 0) {
                Length += getbits(Inp) >> (16 - Bits);
                addbits(Inp, Bits);
            }
            if (Distance >= 0x101) {
                Length++;
                if (Distance >= 0x2000) {
                    Length++;
                    if (Distance >= 0x40000) Length++;
                }
            }
            CopyString20(u, Length, Distance);
            continue;
        }
        if (Number < 270) {
            uint Distance = SDDecode[Number -= 261] + 1;
            if ((Bits = SDBits[Number]) > 0) {
                Distance += getbits(Inp) >> (16 - Bits);
                addbits(Inp, Bits);
            }
            CopyString20(u, 2, Distance);
            continue;
        }
    }
    ReadLastTables(u);
    UnpWriteBuf20(u);
}

/* ---- RAR 1.5 ---- */

#define STARTL1  2
static const uint DecL1[] = { 0x8000,0xa000,0xc000,0xd000,0xe000,0xea00,0xee00,0xf000,0xf200,0xf200,0xffff };
static const uint PosL1[] = { 0,0,0,2,3,5,7,11,16,20,24,32,32 };
#define STARTL2  3
static const uint DecL2[] = { 0xa000,0xc000,0xd000,0xe000,0xea00,0xee00,0xf000,0xf200,0xf240,0xffff };
static const uint PosL2[] = { 0,0,0,0,5,7,9,13,18,22,26,34,36 };
#define STARTHF0  4
static const uint DecHf0[] = { 0x8000,0xc000,0xe000,0xf200,0xf200,0xf200,0xf200,0xf200,0xffff };
static const uint PosHf0[] = { 0,0,0,0,0,8,16,24,33,33,33,33,33 };
#define STARTHF1  5
static const uint DecHf1[] = { 0x2000,0xc000,0xe000,0xf000,0xf200,0xf200,0xf7e0,0xffff };
static const uint PosHf1[] = { 0,0,0,0,0,0,4,44,60,76,80,80,127 };
#define STARTHF2  5
static const uint DecHf2[] = { 0x1000,0x2400,0x8000,0xc000,0xfa00,0xffff,0xffff,0xffff };
static const uint PosHf2[] = { 0,0,0,0,0,0,2,7,53,117,233,0,0 };
#define STARTHF3  6
static const uint DecHf3[] = { 0x800,0x2400,0xee00,0xfe80,0xffff,0xffff,0xffff };
static const uint PosHf3[] = { 0,0,0,0,0,0,0,2,16,218,251,0,0 };
#define STARTHF4  8
static const uint DecHf4[] = { 0xff00,0xffff,0xffff,0xffff,0xffff,0xffff };
static const uint PosHf4[] = { 0,0,0,0,0,0,0,0,0,255,0,0,0 };

static uint DecodeNum(Unpack *u, uint Num, uint StartPos, const uint *DecTab, const uint *PosTab) {
    int I;
    for (Num &= 0xfff0, I = 0; DecTab[I] <= Num; I++) StartPos++;
    faddbits(&u->Inp, StartPos);
    return ((Num - (I ? DecTab[I - 1] : 0)) >> (16 - StartPos)) + PosTab[StartPos];
}

static void CorrHuff(ushort *CharSet, byte *NumToPlace) {
    int I, J;
    for (I = 7; I >= 0; I--)
        for (J = 0; J < 32; J++, CharSet++) *CharSet = (ushort)((*CharSet & ~0xff) | I);
    memset(NumToPlace, 0, 256);
    for (I = 6; I >= 0; I--) NumToPlace[I] = (byte)((7 - I) * 32);
}

static void InitHuff(Unpack *u) {
    for (uint I = 0; I < 256; I++) {
        u->ChSet[I] = u->ChSetB[I] = (ushort)(I << 8);
        u->ChSetA[I] = (ushort)I;
        u->ChSetC[I] = (ushort)(((~I + 1) & 0xff) << 8);
    }
    memset(u->NToPl, 0, sizeof(u->NToPl));
    memset(u->NToPlB, 0, sizeof(u->NToPlB));
    memset(u->NToPlC, 0, sizeof(u->NToPlC));
    CorrHuff(u->ChSetB, u->NToPlB);
}

static void UnpInitData15(Unpack *u, bool Solid) {
    if (!Solid) {
        u->AvrPlcB = u->AvrLn1 = u->AvrLn2 = u->AvrLn3 = 0;
        u->NumHuf = u->Buf60 = 0;
        u->AvrPlc = 0x3500;
        u->MaxDist3 = 0x2001;
        u->Nhfb = u->Nlzb = 0x80;
    }
    u->FlagsCnt = 0;
    u->FlagBuf = 0;
    u->StMode = 0;
    u->LCount = 0;
    u->ReadTop = 0;
}

static void CopyString15(Unpack *u, uint Distance, uint Length) {
    u->DestUnpSize -= Length;
    if ((!u->FirstWinDone && Distance > u->UnpPtr) || Distance > u->MaxWinSize || Distance == 0) {
        while (Length-- > 0) {
            u->Window[u->UnpPtr] = 0;
            u->UnpPtr = (u->UnpPtr + 1) & u->MaxWinMask;
        }
    } else {
        while (Length-- > 0) {
            u->Window[u->UnpPtr] = u->Window[(u->UnpPtr - Distance) & u->MaxWinMask];
            u->UnpPtr = (u->UnpPtr + 1) & u->MaxWinMask;
        }
    }
}

static void GetFlagsBuf(Unpack *u) {
    uint Flags, NewFlagsPlace;
    uint FlagsPlace = DecodeNum(u, fgetbits(&u->Inp), STARTHF2, DecHf2, PosHf2);
    if (FlagsPlace >= ASIZE(u->ChSetC)) return;
    while (1) {
        Flags = u->ChSetC[FlagsPlace];
        u->FlagBuf = Flags >> 8;
        NewFlagsPlace = u->NToPlC[Flags++ & 0xff]++;
        if ((Flags & 0xff) != 0) break;
        CorrHuff(u->ChSetC, u->NToPlC);
    }
    u->ChSetC[FlagsPlace] = u->ChSetC[NewFlagsPlace];
    u->ChSetC[NewFlagsPlace] = (ushort)Flags;
}

static void ShortLZ(Unpack *u) {
    static const uint ShortLen1[] = { 1,3,4,4,5,6,7,8,8,4,4,5,6,6,4,0 };
    static const uint ShortXor1[] = { 0,0xa0,0xd0,0xe0,0xf0,0xf8,0xfc,0xfe,0xff,0xc0,0x80,0x90,0x98,0x9c,0xb0 };
    static const uint ShortLen2[] = { 2,3,3,3,4,4,5,6,6,4,4,5,6,6,4,0 };
    static const uint ShortXor2[] = { 0,0x40,0x60,0xa0,0xd0,0xe0,0xf0,0xf8,0xfc,0xc0,0x80,0x90,0x98,0x9c,0xb0 };
#define GetShortLen1(pos) ((pos) == 1 ? (uint)u->Buf60 + 3 : ShortLen1[pos])
#define GetShortLen2(pos) ((pos) == 3 ? (uint)u->Buf60 + 3 : ShortLen2[pos])
    uint Length, SaveLength, LastDistance, Distance;
    int DistancePlace;
    u->NumHuf = 0;
    uint BitField = fgetbits(&u->Inp);
    if (u->LCount == 2) {
        faddbits(&u->Inp, 1);
        if (BitField >= 0x8000) {
            CopyString15(u, u->LastDist, u->LastLength);
            return;
        }
        BitField <<= 1;
        u->LCount = 0;
    }
    BitField >>= 8;
    if (u->AvrLn1 < 37) {
        for (Length = 0; Length < 15; Length++)
            if (((BitField ^ ShortXor1[Length]) & (~(0xffu >> GetShortLen1(Length)))) == 0) break;
        faddbits(&u->Inp, GetShortLen1(Length));
    } else {
        for (Length = 0; Length < 15; Length++)
            if (((BitField ^ ShortXor2[Length]) & (~(0xffu >> GetShortLen2(Length)))) == 0) break;
        faddbits(&u->Inp, GetShortLen2(Length));
    }
    if (Length >= 9) {
        if (Length == 9) {
            u->LCount++;
            CopyString15(u, u->LastDist, u->LastLength);
            return;
        }
        if (Length == 14) {
            u->LCount = 0;
            Length = DecodeNum(u, fgetbits(&u->Inp), STARTL2, DecL2, PosL2) + 5;
            Distance = (fgetbits(&u->Inp) >> 1) | 0x8000;
            faddbits(&u->Inp, 15);
            u->LastLength = Length;
            u->LastDist = Distance;
            CopyString15(u, Distance, Length);
            return;
        }
        u->LCount = 0;
        SaveLength = Length;
        Distance = (uint)u->OldDist[(u->OldDistPtr - (Length - 9)) & 3];
        Length = DecodeNum(u, fgetbits(&u->Inp), STARTL1, DecL1, PosL1) + 2;
        if (Length == 0x101 && SaveLength == 10) {
            u->Buf60 ^= 1;
            return;
        }
        if (Distance > 256) Length++;
        if (Distance >= u->MaxDist3) Length++;
        u->OldDist[u->OldDistPtr++] = Distance;
        u->OldDistPtr = u->OldDistPtr & 3;
        u->LastLength = Length;
        u->LastDist = Distance;
        CopyString15(u, Distance, Length);
        return;
    }
    u->LCount = 0;
    u->AvrLn1 += Length;
    u->AvrLn1 -= u->AvrLn1 >> 4;
    DistancePlace = (int)(DecodeNum(u, fgetbits(&u->Inp), STARTHF2, DecHf2, PosHf2) & 0xff);
    Distance = u->ChSetA[DistancePlace];
    if (--DistancePlace != -1) {
        LastDistance = u->ChSetA[DistancePlace];
        u->ChSetA[DistancePlace + 1] = (ushort)LastDistance;
        u->ChSetA[DistancePlace] = (ushort)Distance;
    }
    Length += 2;
    u->OldDist[u->OldDistPtr++] = ++Distance;
    u->OldDistPtr = u->OldDistPtr & 3;
    u->LastLength = Length;
    u->LastDist = Distance;
    CopyString15(u, Distance, Length);
#undef GetShortLen1
#undef GetShortLen2
}

static void LongLZ(Unpack *u) {
    uint Length, Distance, DistancePlace, NewDistancePlace, OldAvr2, OldAvr3;
    u->NumHuf = 0;
    u->Nlzb += 16;
    if (u->Nlzb > 0xff) {
        u->Nlzb = 0x90;
        u->Nhfb >>= 1;
    }
    OldAvr2 = u->AvrLn2;
    uint BitField = fgetbits(&u->Inp);
    if (u->AvrLn2 >= 122) {
        Length = DecodeNum(u, BitField, STARTL2, DecL2, PosL2);
    } else if (u->AvrLn2 >= 64) {
        Length = DecodeNum(u, BitField, STARTL1, DecL1, PosL1);
    } else if (BitField < 0x100) {
        Length = BitField;
        faddbits(&u->Inp, 16);
    } else {
        for (Length = 0; ((BitField << Length) & 0x8000) == 0; Length++)
            ;
        faddbits(&u->Inp, Length + 1);
    }
    u->AvrLn2 += Length;
    u->AvrLn2 -= u->AvrLn2 >> 5;
    BitField = fgetbits(&u->Inp);
    if (u->AvrPlcB > 0x28ff) DistancePlace = DecodeNum(u, BitField, STARTHF2, DecHf2, PosHf2);
    else if (u->AvrPlcB > 0x6ff) DistancePlace = DecodeNum(u, BitField, STARTHF1, DecHf1, PosHf1);
    else DistancePlace = DecodeNum(u, BitField, STARTHF0, DecHf0, PosHf0);
    u->AvrPlcB += DistancePlace;
    u->AvrPlcB -= u->AvrPlcB >> 8;
    while (1) {
        Distance = u->ChSetB[DistancePlace & 0xff];
        NewDistancePlace = u->NToPlB[Distance++ & 0xff]++;
        if (!(Distance & 0xff)) CorrHuff(u->ChSetB, u->NToPlB);
        else break;
    }
    u->ChSetB[DistancePlace & 0xff] = u->ChSetB[NewDistancePlace];
    u->ChSetB[NewDistancePlace] = (ushort)Distance;
    Distance = ((Distance & 0xff00) | (fgetbits(&u->Inp) >> 8)) >> 1;
    faddbits(&u->Inp, 7);
    OldAvr3 = u->AvrLn3;
    if (Length != 1 && Length != 4) {
        if (Length == 0 && Distance <= u->MaxDist3) {
            u->AvrLn3++;
            u->AvrLn3 -= u->AvrLn3 >> 8;
        } else if (u->AvrLn3 > 0) {
            u->AvrLn3--;
        }
    }
    Length += 3;
    if (Distance >= u->MaxDist3) Length++;
    if (Distance <= 256) Length += 8;
    if (OldAvr3 > 0xb0 || (u->AvrPlc >= 0x2a00 && OldAvr2 < 0x40)) u->MaxDist3 = 0x7f00;
    else u->MaxDist3 = 0x2001;
    u->OldDist[u->OldDistPtr++] = Distance;
    u->OldDistPtr = u->OldDistPtr & 3;
    u->LastLength = Length;
    u->LastDist = Distance;
    CopyString15(u, Distance, Length);
}

static void HuffDecode(Unpack *u) {
    uint CurByte, NewBytePlace, Length, Distance;
    int BytePlace;
    uint BitField = fgetbits(&u->Inp);
    if (u->AvrPlc > 0x75ff) BytePlace = (int)DecodeNum(u, BitField, STARTHF4, DecHf4, PosHf4);
    else if (u->AvrPlc > 0x5dff) BytePlace = (int)DecodeNum(u, BitField, STARTHF3, DecHf3, PosHf3);
    else if (u->AvrPlc > 0x35ff) BytePlace = (int)DecodeNum(u, BitField, STARTHF2, DecHf2, PosHf2);
    else if (u->AvrPlc > 0x0dff) BytePlace = (int)DecodeNum(u, BitField, STARTHF1, DecHf1, PosHf1);
    else BytePlace = (int)DecodeNum(u, BitField, STARTHF0, DecHf0, PosHf0);
    BytePlace &= 0xff;
    if (u->StMode) {
        if (BytePlace == 0 && BitField > 0xfff) BytePlace = 0x100;
        if (--BytePlace == -1) {
            BitField = fgetbits(&u->Inp);
            faddbits(&u->Inp, 1);
            if (BitField & 0x8000) {
                u->NumHuf = u->StMode = 0;
                return;
            } else {
                Length = (BitField & 0x4000) ? 4 : 3;
                faddbits(&u->Inp, 1);
                Distance = DecodeNum(u, fgetbits(&u->Inp), STARTHF2, DecHf2, PosHf2);
                Distance = (Distance << 5) | (fgetbits(&u->Inp) >> 11);
                faddbits(&u->Inp, 5);
                CopyString15(u, Distance, Length);
                return;
            }
        }
    } else if (u->NumHuf++ >= 16 && u->FlagsCnt == 0) {
        u->StMode = 1;
    }
    u->AvrPlc += (uint)BytePlace;
    u->AvrPlc -= u->AvrPlc >> 8;
    u->Nhfb += 16;
    if (u->Nhfb > 0xff) {
        u->Nhfb = 0x90;
        u->Nlzb >>= 1;
    }
    u->Window[u->UnpPtr++] = (byte)(u->ChSet[BytePlace] >> 8);
    --u->DestUnpSize;
    while (1) {
        CurByte = u->ChSet[BytePlace];
        NewBytePlace = u->NToPl[CurByte++ & 0xff]++;
        if ((CurByte & 0xff) > 0xa1) CorrHuff(u->ChSet, u->NToPl);
        else break;
    }
    u->ChSet[BytePlace] = u->ChSet[NewBytePlace];
    u->ChSet[NewBytePlace] = (ushort)CurByte;
}

static void Unpack15(Unpack *u, bool Solid) {
    UnpInitData(u, Solid);
    UnpInitData15(u, Solid);
    UnpReadBuf(u);
    if (!Solid) {
        InitHuff(u);
        u->UnpPtr = 0;
    } else {
        u->UnpPtr = u->WrPtr;
    }
    --u->DestUnpSize;
    if (u->DestUnpSize >= 0) {
        GetFlagsBuf(u);
        u->FlagsCnt = 8;
    }
    while (u->DestUnpSize >= 0 && !u->write_stopped && !u->failed) {
        u->UnpPtr &= u->MaxWinMask;
        u->FirstWinDone |= (u->PrevPtr > u->UnpPtr);
        u->PrevPtr = u->UnpPtr;
        if (u->Inp.InAddr > u->ReadTop - 30 && !UnpReadBuf(u)) break;
        if (((u->WrPtr - u->UnpPtr) & u->MaxWinMask) < 270 && u->WrPtr != u->UnpPtr) UnpWriteBuf20(u);
        if (u->StMode) {
            HuffDecode(u);
            continue;
        }
        if (--u->FlagsCnt < 0) {
            GetFlagsBuf(u);
            u->FlagsCnt = 7;
        }
        if (u->FlagBuf & 0x80) {
            u->FlagBuf <<= 1;
            if (u->Nlzb > u->Nhfb) LongLZ(u);
            else HuffDecode(u);
        } else {
            u->FlagBuf <<= 1;
            if (--u->FlagsCnt < 0) {
                GetFlagsBuf(u);
                u->FlagsCnt = 7;
            }
            if (u->FlagBuf & 0x80) {
                u->FlagBuf <<= 1;
                if (u->Nlzb > u->Nhfb) HuffDecode(u);
                else LongLZ(u);
            } else {
                u->FlagBuf <<= 1;
                ShortLZ(u);
            }
        }
    }
    UnpWriteBuf20(u);
}

/* ---- the object ---- */

rar_unpack_t *rar_unpack_create(void) {
    Unpack *u = (Unpack*)calloc(1, sizeof(Unpack));
    if (!u) return NULL;
    /* getbits*() read up to 8 bytes past the position: the buffers carry that slack. */
    u->Inp.InBuf = (byte*)calloc(BITINPUT_MAX_SIZE + 8, 1);
    u->VMCodeInp.InBuf = (byte*)calloc(BITINPUT_MAX_SIZE + 8, 1);
    u->VMMem = (byte*)calloc(VM_MEMSIZE + 4, 1);
    if (!u->Inp.InBuf || !u->VMCodeInp.InBuf || !u->VMMem) { rar_unpack_destroy(u); return NULL; }
    Ppmd7_Construct(&u->PPM);
    UnpInitData(u, false);
    UnpInitData15(u, false);
    InitHuff(u);
    return u;
}

void rar_unpack_destroy(rar_unpack_t *u) {
    if (!u) return;
    InitFilters30(u, false);
    PPM_CleanUp(u);
    free(u->Filters30);
    free(u->PrgStack);
    free(u->OldFilterLengths);
    free(u->Filters);
    free(u->FilterSrcMemory);
    free(u->FilterDstMemory);
    free(u->Window);
    free(u->Inp.InBuf);
    free(u->VMCodeInp.InBuf);
    free(u->VMMem);
    free(u);
}

/* Unpack::Init: the window for this file, the same one for a solid stream. */
static bool unpack_init(Unpack *u, uint64_t WinSize, bool Solid) {
    const size_t MinAllocSize = 0x40000;
    if (WinSize < MinAllocSize) WinSize = MinAllocSize;
    if (WinSize > RAR_UNPACK_MAX_DICT) return false;
    if (!Solid || u->Window == NULL) {
        u->MaxWinSize = (size_t)WinSize;
        u->MaxWinMask = u->MaxWinSize - 1;
    }
    if (WinSize <= u->AllocWinSize) return true;
    if (Solid && u->Window != NULL) return false;
    free(u->Window);
    u->Window = (byte*)calloc((size_t)WinSize, 1);
    if (!u->Window) { u->AllocWinSize = 0; return false; }
    u->AllocWinSize = WinSize;
    return true;
}

bool rar_unpack_file(rar_unpack_t *u, unsigned method, bool solid, uint64_t dict_size, uint64_t dest_size,
                     rar_read_fn read, void *read_ctx, rar_write_fn write, void *write_ctx) {
    if (!u || !read || !write) return false;
    /* Methods before 5.0 address the window by mask: a power of two. */
    if (method < 50) {
        uint64_t p = 0x10000;
        while (p < dict_size && p < RAR_UNPACK_MAX_DICT) p <<= 1;
        dict_size = p;
    }
    if (!unpack_init(u, dict_size, solid)) return false;
    u->read = read; u->read_ctx = read_ctx;
    u->write = write; u->write_ctx = write_ctx;
    u->write_stopped = false;
    u->failed = false;
    u->DestUnpSize = (int64)dest_size;
    switch (method) {
        case 15: Unpack15(u, solid); break;
        case 20: case 26: Unpack20(u, solid); break;
        case 29: case 36: Unpack29(u, solid); break;
        case 50: case 70:
            u->ExtraDist = method == 70;
            Unpack5(u, solid);
            break;
        default: return false;
    }
    return !u->failed;
}
