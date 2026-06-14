#pragma once

#include <cstdint>
#include <string>

// Client side of the WraithHost shared-memory mailbox. Opens the mailbox the host created, 
// sends a FlexBuffers request and reads the response. Resolution results are
// cached locally so only the first lookup of a given id makes an IPC round-trip. If the host is not
// running, every call returns "" (the caller falls back to native behaviour). Thread-safe.
namespace wraith::features::ipc
{
    // Launch WraithHost.exe (sitting next to this module) if it is not already running. Non-blocking:
    // the host loads its tables in parallel; connection happens lazily on the first Resolve.
    void EnsureHostRunning();

    // Try to (re)open the host mailbox. Returns true if connected. Safe to call repeatedly.
    bool Connect();
    bool IsConnected();

    // Resolve via the host, or "" if the host is absent / unknown. op = wraith::ipc::Op*.
    // For Material, arg = MaterialResourcesID and arg2 = textureType hint (sex).
    std::string Resolve(uint32_t op, uint32_t arg, uint32_t arg2 = 0);

    std::string TexturePath(uint32_t fileDataId);
    std::string ModelPath(uint32_t fileDataId);
    // MaterialResourcesID -> .blp path (host does MRID -> FileDataID -> path). textureType = sex hint.
    std::string MaterialPath(uint32_t materialResId, uint32_t textureType = 0);
}
