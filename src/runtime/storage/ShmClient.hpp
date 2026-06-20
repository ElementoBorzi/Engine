// DLL IPC client: launch + connect to the asset host, run file ops over the shared-memory mailbox.
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
#include <string>
#include <vector>

// Client side of the host shared-memory mailbox: sends a FlexBuffers request, reads the response.
// Thread-safe. Returns empty/false when the host is absent (the caller falls back to native I/O).
namespace wxl::runtime::ipc
{
    // Launch the asset host (this module's Utils\WarcraftXLHost.exe) if not already running. Fires
    // OnBeforeHostLaunch first; a subscriber can cancel the auto-launch. Non-blocking.
    void EnsureHostRunning();

    // Block until the host mailbox exists or timeoutMs elapses. Returns true if the host is ready.
    bool WaitForHost(uint32_t timeoutMs);

    // (Re)open the host mailbox. Returns true if connected.
    bool Connect();
    bool IsConnected();

    // File ops served from the host archive set.
    // ok=false: not found. ok && id==0: inline, bytes in inlineData. ok && id!=0: a shared section the
    // client maps with MapBlob and reads directly; FileReadChunk is the fallback when mapping fails.
    struct FileOpenResult { bool ok; uint32_t id; uint32_t size; std::vector<uint8_t> inlineData; };
    FileOpenResult FileOpen(const std::string& name, uint32_t flags);

    // Map the host's blob section for `id` read-only. On success sets outView/outHandle and returns true.
    bool MapBlob(uint32_t id, uint32_t size, void*& outView, void*& outHandle);
    // Release a mapping from MapBlob (null-safe).
    void UnmapBlob(void* view, void* handle);
    // Read up to cap bytes at off into dst (one round trip, capped at kFileChunkMax). Returns bytes copied.
    uint32_t FileReadChunk(uint32_t id, uint32_t off, void* dst, uint32_t cap);
    void FileClose(uint32_t id);
    bool FileExists(const std::string& name);
}
