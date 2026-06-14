#pragma once

#include <cstdint>
#include <vector>

// Thin wrapper over the client's storage-file API. Used by the DB2 loader to read a DBFilesClient
// file out of the mounted archives. Storage-file access ported from WotLKExtensions by Alyst3r (MIT License).
namespace wraith::features::db2
{
    // Open a storage file by name across the loaded archives. Returns true and fills handle on success.
    bool FileOpen(const char* path, void** handle);
    // Read bytesToRead bytes from handle into dst. Returns true on success.
    bool FileRead(void* handle, void* dst, uint32_t bytesToRead);
    // Close a handle from FileOpen.
    void FileClose(void* handle);

    // Open + read the whole file into dst (resized). Returns true on success; dst is left empty on failure.
    bool ReadWholeFile(const char* dbFilesClientName, std::vector<uint8_t>& dst);
}
