#pragma once

#include <cstdint>

// Shared IPC contract between Wow.exe (Wraith.dll, 32-bit) and WraithHost.exe (64-bit). The transport is a
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
    constexpr uint32_t kVersion    = 1;
    constexpr uint32_t kShmSize    = 1u << 20;   // 1 MiB shared window (data lives in the host, not here)
    constexpr uint32_t kHeaderSize = 64;
    constexpr uint32_t kPayloadMax = kShmSize - kHeaderSize;

    // Carried inside the FlexBuffers request/response (not in the fixed header).
    //  OpResolveTexture/Model: arg = FileDataID.
    //  OpResolveMaterial: arg = MaterialResourcesID, arg2 = textureType hint (sex), -> .blp path
    //                     (host does MRID -> FileDataID via TextureFileData -> path via TextureFilePath).
    enum Op : uint32_t { OpResolveTexture = 1, OpResolveModel = 2, OpResolveMaterial = 3 };
    enum Status : uint32_t { StOk = 0, StNotFound = 1, StBadRequest = 2 };

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

    // Request payload  = FlexBuffers vector [ uint op, uint arg, uint arg2 ].
    // Response payload = FlexBuffers vector [ uint status, string path ].
}
