#pragma once

#include <cstdint>

// Shared IPC contract between Client (Wraith.dll, 32-bit) and WraithHost.exe (64-bit). The transport is a
// raw Win32 shared-memory window plus two auto-reset events (a single-slot request/response mailbox). The
// payload is a FlexBuffers buffer (arch/endian-neutral, so 32-bit and 64-bit agree on the bytes).
//
// Control header layout is fixed-width with no pointers, identical across 32/64. The host owns/creates the
// objects; the client opens them. One request is in flight at a time (client serialises with a mutex).
namespace wraith::ipc
{
    // Win32 object names (per-session "Local\" scope; host and client run in the same session).
    constexpr const char* kShmName   = "Local\\WraithShm";
    constexpr const char* kReqEvent  = "Local\\WraithReqEvent";
    constexpr const char* kRespEvent = "Local\\WraithRespEvent";

    constexpr uint32_t kMagic      = 0x4D485357; // 'WSHM'
    constexpr uint32_t kVersion    = 2;          // protocol version (client rejects a mismatched host)
    constexpr uint32_t kShmSize    = 1u << 20;   // 1 MiB shared window
    constexpr uint32_t kHeaderSize = 64;
    constexpr uint32_t kPayloadMax = kShmSize - kHeaderSize;

    // Carried inside the FlexBuffers request/response (not in the fixed header).
    //  OpResolveTexture/Model: arg = FileDataID.
    //  OpResolveMaterial: arg = MaterialResourcesID, arg2 = textureType hint (sex), -> .blp path
    //                     (host does MRID -> FileDataID via TextureFileData -> path via TextureFilePath).
    //
    // File ops (host reads the MPQ archive set via StormLib and serves bytes back):
    //  OpFileOpen   req=[op, string name, uint flags]
    //               -> small file: resp=[status, 0, size, blob bytes]   (served INLINE, no handle kept)
    //               -> large file: resp=[status, handleId(!=0), size]   (pull via OpFileRead)
    //               handleId==0 with StOk means the bytes are inline (blob present, may be empty for 0-byte).
    //  OpFileRead   req=[op, uint handleId, uint off, uint len] -> resp=[status, blob bytes]
    //  OpFileClose  req=[op, uint handleId]                     -> resp=[status]
    //  OpFileExists req=[op, string name]                       -> resp=[status]  (StOk = present)
    enum Op : uint32_t
    {
        OpResolveTexture  = 1,
        OpResolveModel    = 2,
        OpResolveMaterial = 3,
        OpFileOpen        = 4,
        OpFileRead        = 5,
        OpFileClose       = 6,
        OpFileExists      = 7,
    };
    enum Status : uint32_t { StOk = 0, StNotFound = 1, StBadRequest = 2 };

    // Max file bytes returned by one OpFileRead. Kept well under kPayloadMax to leave room for the
    // FlexBuffers blob framing; large files are pulled in successive chunks by the client.
    constexpr uint32_t kFileChunkMax = 512u * 1024u;

    // Files this size or smaller are returned inline in the OpFileOpen response (1 round trip instead of
    // open+read+close). Must fit the response payload with FlexBuffers framing, so well under kPayloadMax.
    constexpr uint32_t kInlineMax = 512u * 1024u;

#pragma pack(push, 4)
    struct ControlHeader
    {
        uint32_t magic;        // kMagic, set by host
        uint32_t version;      // kVersion
        uint32_t reqSeq;       // client bumps after writing a request payload
        uint32_t respSeq;      // host sets == reqSeq after writing the response payload
        uint32_t reqLen;       // request payload length (FlexBuffers bytes at offset kHeaderSize)
        uint32_t respLen;      // response payload length (overwrites the request area)
        uint32_t reserved[10]; // pad to kHeaderSize
    };
#pragma pack(pop)

    static_assert(sizeof(ControlHeader) == kHeaderSize, "ControlHeader must be 64 bytes");
}
