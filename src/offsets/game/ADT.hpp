// Terrain tile/chunk lookups, the tile-slot grid, and runtime in-memory chunk field offsets.
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
#include <cstddef>

// INTERNAL to the core. Terrain tile/chunk lookups, the tile-slot grid, and runtime in-memory chunk
// field offsets. Modules never include this; they use wxl::game / wxl::events.
namespace wxl::offsets::game::adt
{
    // --- lookups ---
    // Chunk lookup (pos) -> runtime chunk object, or null when that chunk is not parsed yet. A non-null
    // result means the chunk heightmap + collision are resident.
    constexpr uintptr_t kGetChunk = 0x007B49C0;
    // CMapChunk::Build (this=CMapChunk in ECX): turns one raw MCNK into a live chunk (sub-chunk pointers,
    // bbox, texture-layer units, ref spawn). The "a terrain chunk was built" point, distinct from the
    // per-frame terrain draw.
    constexpr uintptr_t kChunkBuild = 0x007C64B0;
    // Near-tile placed-object counter (chunk, &progress, total) -> count of placed-object children still
    // loading that overlap the chunk box.
    constexpr uintptr_t kNearObjectCount = 0x007B50B0;

    // --- tile-slot grid ---
    // Tile-slot grid base: a 64x64 array of tile-area pointers (stride 4). Slot index is X-major
    // (tileX * 64 + tileY).
    constexpr uintptr_t kTileSlots   = 0x00CE48D0;
    constexpr uint32_t  kTileGridDim = 64;   // tiles per axis
    constexpr size_t    kTileSlotStride = 0x04;
    // Detailed/streaming-path selector (u32).
    constexpr uintptr_t kStreamingPathSelector = 0x00CE0494;

    // --- tile-area object fields ---
    constexpr size_t kOffTileAsyncRead = 0x70; // non-zero while the tile root read is in flight
    constexpr size_t kOffTileFileBuffer = 0x80; // non-zero once the tile file buffer is allocated

    // --- runtime chunk object fields ---
    constexpr size_t kOffChunkNodeLayerCount = 0x09; // draw-node layer count
    // CMapChunk -> MCNK 128-byte data header (= raw MCNK ptr + 8-byte tag). The authoritative texture-layer
    // count (SMChunk.nLayers, 0..4) lives at header + 0x0C.
    constexpr size_t kOffChunkMcnkHeader = 0x110;
    constexpr size_t kOffMcnkNLayers     = 0x0C;
    // Raw on-disk MCLY/MCAL base pointers (point into the resident MCNK block, all physical entries, not
    // just the 4 materialized layers). The 4-byte field right before the MCLY payload is its sub-chunk
    // size, so physical-layer-count = *(mclyBase - 4) / 0x10.
    constexpr size_t kOffChunkMcly       = 0x12C;
    constexpr size_t kOffChunkMcal       = 0x130;
    // Primitive/draw-batch descriptor (the 145-vertex MCVT grid VB/IB) passed to the device Draw method.
    constexpr size_t kOffChunkDrawBatch  = 0x90;
    // Source of the tile tex-owner object: (*(chunkObj+0x20) & ~1) + 8.
    constexpr size_t kOffChunkTexOwnerSrc = 0x20;
    // Per-layer record array (4 slots, stride 0x14): +0x00 flags, +0x04 diffuse CGxTex*, +0x0C alpha
    // CGxTex*, +0x10 back-ptr. Only the first nLayers (<=4) records exist.
    constexpr size_t kOffChunkLayerRecords   = 0x34;
    constexpr size_t kChunkLayerRecordStride = 0x14;

    // --- terrain surface render (per-chunk draw + multi-pass extension) ---
    // Per-chunk surface draw leaf (chunkObj is a stack arg, __cdecl): binds {layer diffuse @ sampler
    // 0x15, layer alpha @ sampler 0x16} and issues one DrawIndexedPrimitive per layer (layer 0 opaque,
    // 1..n alpha-over). The active variant is held in kSurfaceDrawFnPtr; this is the default body.
    constexpr uintptr_t kSurfaceChunkDraw  = 0x007D0D70;
    // Per-chunk draw fn-ptr the surface render dispatches through (selected by the draw-variant selector).
    constexpr uintptr_t kSurfaceDrawFnPtr  = 0x00D25098;
    // Every per-chunk surface-draw body the selector may install into kSurfaceDrawFnPtr: default, shadow,
    // and the shader/hi-detail bodies, picked by the active graphics config. These are called through the
    // indirect dispatch (__cdecl, chunkObj on the stack).
    constexpr uintptr_t kSurfaceChunkDrawVariants[] = {
        0x007D0D70, 0x007D0760, 0x007D13F0, 0x007D1AD0, 0x007D20A0, 0x007D2520,
    };
    // The shader-path per-chunk surface draw, called DIRECTLY by the surface driver (not via the indirect
    // dispatch) when the pixel-shader terrain path is active. Convention is __thiscall (chunkObj in ECX).
    // It draws one chunk per call with a single DIP: diffuse layer i at stage 0x15+i, a 4-channel combined
    // alpha RT (chunkObj+0x84) at stage 0x15+nLayers, and a Terrain1/2/3 pixel shader indexed by nLayers.
    constexpr uintptr_t kSurfaceChunkDrawShader = 0x007D2D70;
    // CGxDevice singleton; vtable + 0xA8 = the Draw (DrawIndexedPrimitive) method (batch ptr + flag).
    constexpr uintptr_t kGxDeviceSingleton = 0x00C5DF88;
    constexpr size_t    kGxDeviceDrawVtbl  = 0xA8;
    // CGxTex -> GxTex GPU handle resolve.
    constexpr uintptr_t kTexResolve        = 0x004B6CB0;
    // GxRsSet / SetTexture for a sampler slot (0x15 = diffuse stage, 0x16 = alpha stage).
    constexpr uintptr_t kSetSamplerTexture = 0x00685F50;
    // Sampler addr/filter state for the just-bound texture.
    constexpr uintptr_t kSetSamplerState   = 0x00681450;
    // Lazy texture loader for one tex-owner handle slot: slot[+4] = Load(slot[+0]).
    constexpr uintptr_t kLazyLoadTexSlot   = 0x007D6980;
    // Builds the per-layer alpha texture from a layer record's MCAL into record + 0x0C.
    constexpr uintptr_t kBuildLayerAlpha   = 0x007B9DE0;
    constexpr uint32_t  kSamplerDiffuse    = 0x15;
    constexpr uint32_t  kSamplerAlpha      = 0x16;
    // Tile tex-owner: per-tile texture-handle array, indexed by MCLY.textureId, stride 8
    // ([+0] = MTEX filename ptr, [+4] = loaded CGxTex*). Covers the whole tile MTEX set.
    constexpr size_t kOffTexOwnerHandleArray = 0x60;
    constexpr size_t kTexOwnerHandleStride   = 0x08;

    // --- signatures ---
    // Chunk lookup (pos on stack) -> chunk object.
    using Map_GetChunkFn = void*(__cdecl*)(float* pos);
    // Near-object counter (chunk, progressOut, total) -> count.
    using Map_NearObjectCountFn = int(__cdecl*)(void* chunk, int* progressOut, int total);
    // CMapChunk::Build: native this-in-ECX (__thiscall, ret 8). Declared __fastcall with a dummy EDX so the
    // trampoline routes the chunk into the this-register and keeps the two stack args.
    using Map_ChunkBuildFn = void(__fastcall*)(void* chunk, void* edx, void* rawMcnk, int param2);

    // --- typed views over the objects above ---
    // The constants are the curated landmarks; these structs give named, typed access to the same fields,
    // with every member offset checked against a constant at compile time (a wrong padding fails the build).
    // Only RE'd fields are named; the gaps are explicit padding. Pointers are 4 bytes on the 32-bit client.
#pragma pack(push, 1)
    /** @brief Tile-area object (one per resident map tile): async-read state and file buffer slots. */
    struct TileArea
    {
        uint8_t  _pad00[kOffTileAsyncRead];
        uint32_t asyncRead;        // kOffTileAsyncRead (non-zero while the root read is in flight)
        uint8_t  _pad74[kOffTileFileBuffer - (kOffTileAsyncRead + sizeof(uint32_t))];
        void*    fileBuffer;       // kOffTileFileBuffer (non-zero once the file buffer is allocated)
    };
    static_assert(offsetof(TileArea, asyncRead)  == kOffTileAsyncRead,  "TileArea.asyncRead");
    static_assert(offsetof(TileArea, fileBuffer) == kOffTileFileBuffer, "TileArea.fileBuffer");

    /** @brief Runtime chunk object (CMapChunk): draw-node layer count and the MCNK data-header pointer. */
    struct MapChunk
    {
        uint8_t  _pad00[kOffChunkNodeLayerCount];
        uint8_t  nodeLayerCount;   // kOffChunkNodeLayerCount (draw-node layer count)
        uint8_t  _pad0a[kOffChunkMcnkHeader - (kOffChunkNodeLayerCount + sizeof(uint8_t))];
        void*    mcnkHeader;       // kOffChunkMcnkHeader -> McnkHeader (raw MCNK ptr + 8-byte tag)
    };
    static_assert(offsetof(MapChunk, nodeLayerCount) == kOffChunkNodeLayerCount, "MapChunk.nodeLayerCount");
    static_assert(offsetof(MapChunk, mcnkHeader)     == kOffChunkMcnkHeader,     "MapChunk.mcnkHeader");

    /** @brief MCNK 128-byte data header (chunk->mcnkHeader): the authoritative texture-layer count. */
    struct McnkHeader
    {
        uint8_t  _pad00[kOffMcnkNLayers];
        uint32_t nLayers;          // kOffMcnkNLayers (SMChunk.nLayers, 0..4)
    };
    static_assert(offsetof(McnkHeader, nLayers) == kOffMcnkNLayers, "McnkHeader.nLayers");
#pragma pack(pop)
}
