#include "Features/DB2/SFile.hpp"

#include "Engine/Offsets.hpp"

using namespace wraith;

namespace
{
    using FileOpenFn  = int(__stdcall*)(void* /*0*/, const char* path, uint32_t flag, void** handle);
    using FileSizeFn  = uint32_t(__stdcall*)(void* handle, uint32_t* sizeHigh);
    using FileReadFn  = int(__stdcall*)(void* handle, void* dst, uint32_t len, uint32_t* read, uint32_t* ovl, uint32_t unk);
    using FileCloseFn = void(__stdcall*)(void* handle);
}

namespace wraith::features::db2
{
    bool FileOpen(const char* path, void** handle)
    {
        auto fn = reinterpret_cast<FileOpenFn>(offsets::Storage_FileOpen);
        return fn(nullptr, path, offsets::Storage_OpenFlag, handle) != 0;
    }

    bool FileRead(void* handle, void* dst, uint32_t bytesToRead)
    {
        auto fn = reinterpret_cast<FileReadFn>(offsets::Storage_FileRead);
        return fn(handle, dst, bytesToRead, nullptr, nullptr, 0) != 0;
    }

    void FileClose(void* handle)
    {
        reinterpret_cast<FileCloseFn>(offsets::Storage_FileClose)(handle);
    }

    bool ReadWholeFile(const char* dbFilesClientName, std::vector<uint8_t>& dst)
    {
        dst.clear();

        char path[260];
        // "DBFilesClient\<name>" is where the client keeps DB2 tables inside the archives.
        const char* prefix = "DBFilesClient\\";
        size_t p = 0;
        for (const char* s = prefix; *s && p < sizeof(path) - 1; ++s) path[p++] = *s;
        for (const char* s = dbFilesClientName; *s && p < sizeof(path) - 1; ++s) path[p++] = *s;
        path[p] = '\0';

        void* handle = nullptr;
        if (!FileOpen(path, &handle) || !handle)
            return false;

        uint32_t sizeHigh = 0;
        uint32_t size = reinterpret_cast<FileSizeFn>(offsets::Storage_FileSize)(handle, &sizeHigh);
        if (size == 0)
        {
            FileClose(handle);
            return false;
        }

        dst.resize(size);
        bool ok = FileRead(handle, dst.data(), size);
        FileClose(handle);
        if (!ok) { dst.clear(); return false; }
        return true;
    }
}
