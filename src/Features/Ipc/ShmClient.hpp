#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Client side of the WraithHost shared-memory mailbox: sends a FlexBuffers request, reads the response.
// Resolve results are cached. Thread-safe. Returns empty/false when the host is absent.
namespace wraith::features::ipc
{
    // Launch WraithHost.exe (in this module's Utils folder) if not already running. Non-blocking.
    void EnsureHostRunning();

    // Block until the host mailbox exists or timeoutMs elapses. Returns true if the host is ready.
    bool WaitForHost(uint32_t timeoutMs);

    // (Re)open the host mailbox. Returns true if connected.
    bool Connect();
    bool IsConnected();

    // Resolve via the host, or "" if absent/unknown. op = wraith::ipc::Op*. For Material, arg = MRID,
    // arg2 = textureType hint.
    std::string Resolve(uint32_t op, uint32_t arg, uint32_t arg2 = 0);

    std::string TexturePath(uint32_t fileDataId);
    std::string ModelPath(uint32_t fileDataId);
    // MaterialResourcesID -> .blp path. textureType = sex hint.
    std::string MaterialPath(uint32_t materialResId, uint32_t textureType = 0);

    // File ops served from the host MPQ set.
    // ok=false: not found. ok && id==0: inline, bytes in inlineData. ok && id!=0: read via FileReadChunk then FileClose.
    struct FileOpenResult { bool ok; uint32_t id; uint32_t size; std::vector<uint8_t> inlineData; };
    FileOpenResult FileOpen(const std::string& name, uint32_t flags);
    // Read up to cap bytes at off into dst (one round trip, capped at kFileChunkMax). Returns bytes copied.
    uint32_t FileReadChunk(uint32_t id, uint32_t off, void* dst, uint32_t cap);
    void FileClose(uint32_t id);
    bool FileExists(const std::string& name);
}
