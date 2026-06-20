// IPC protocol: the single versioned source of truth for the host <-> DLL contract.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>
#include <cstdio>

// Compiled into BOTH processes (32-bit DLL + 64-bit host). The shared-memory window is split into
// kChannels independent channels, each with its own control header and payload region. Payloads are
// FlexBuffers (endian/arch neutral so the 32-bit DLL and 64-bit host agree on bytes). Any wire change
// bumps kVersion; the DLL rejects a host whose version differs.
namespace wxl::ipc
{
    // The client serializes its opens onto one request slot, so a single channel is enough; the host
    // serves it on its main thread.
    constexpr uint32_t kChannels = 1;

    constexpr const char* kShmName      = "Local\\WarcraftXLShm";
    constexpr const char* kReqEventFmt  = "Local\\WarcraftXLReq_%u";
    constexpr const char* kRespEventFmt = "Local\\WarcraftXLResp_%u";
    // Per-file zero-copy blob: the host puts a served file's whole bytes in a shared section named with
    // its blob id; the client maps that section directly (no chunking, no copy through a channel).
    constexpr const char* kBlobNameFmt  = "Local\\WarcraftXLBlob_%u";

    constexpr uint32_t kMagic   = 0x4C585857; // 'WXXL'
    constexpr uint32_t kVersion = 1;

    constexpr uint32_t kHeaderSize     = 64;
    constexpr uint32_t kFileChunkMax   = 512u * 1024u;
    // Files at or below this size come back inline in the open response (one small copy). Larger files
    // are served zero-copy via a shared blob section the client maps directly.
    constexpr uint32_t kInlineMax      = 64u * 1024u;
    constexpr uint32_t kChannelPayload = 768u * 1024u;
    constexpr uint32_t kChannelStride  = kHeaderSize + kChannelPayload;
    constexpr uint32_t kShmSize        = kChannels * kChannelStride;

    // Request kind, carried inside the FlexBuffers payload.
    enum Op : uint32_t
    {
        OpFileOpen   = 4, // name, flags     -> inline blob (small) OR blob id + size (zero-copy)
        OpFileRead   = 5, // blobId, off, len -> status, blob (fallback; sectioned files map direct)
        OpFileClose  = 6, // blobId          -> status (releases the blob section)
        OpFileExists = 7, // name            -> status (StOk = present)
    };

    enum Status : uint32_t { StOk = 0, StNotFound = 1, StBadRequest = 2 };

#pragma pack(push, 4)
    // Fixed-width, no pointers, identical across 32/64. Host creates; client opens.
    struct ControlHeader
    {
        uint32_t magic;        // kMagic
        uint32_t version;      // kVersion
        uint32_t reqSeq;       // client bumps after writing a request
        uint32_t respSeq;      // host sets == reqSeq after writing the response
        uint32_t reqLen;       // request payload length
        uint32_t respLen;      // response payload length
        uint32_t reserved[10]; // pad to kHeaderSize
    };
#pragma pack(pop)

    static_assert(sizeof(ControlHeader) == kHeaderSize, "ControlHeader must be 64 bytes");

    // Channel layout helpers: window is [header|payload] * kChannels, contiguous.
    inline uint32_t ChannelHeaderOffset(uint32_t i)  { return i * kChannelStride; }
    inline uint32_t ChannelPayloadOffset(uint32_t i) { return i * kChannelStride + kHeaderSize; }

    inline ControlHeader* ChannelHeader(uint8_t* base, uint32_t i)
    {
        return reinterpret_cast<ControlHeader*>(base + ChannelHeaderOffset(i));
    }
    inline uint8_t* ChannelPayload(uint8_t* base, uint32_t i)
    {
        return base + ChannelPayloadOffset(i);
    }

    // Format the per-channel request/response/blob names into the caller's buffer (>= 32 bytes).
    inline void ReqEventName(char* out, size_t cap, uint32_t i)  { _snprintf_s(out, cap, _TRUNCATE, kReqEventFmt, i); }
    inline void RespEventName(char* out, size_t cap, uint32_t i) { _snprintf_s(out, cap, _TRUNCATE, kRespEventFmt, i); }
    inline void BlobName(char* out, size_t cap, uint32_t id)     { _snprintf_s(out, cap, _TRUNCATE, kBlobNameFmt, id); }
}
