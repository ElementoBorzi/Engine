#include "Features/Ipc/ShmClient.hpp"

#include "Features/Ipc/Protocol.hpp"

#include <flatbuffers/flexbuffers.h>

#include <windows.h>
#include <mutex>
#include <unordered_map>

using namespace wraith::ipc;

namespace
{
    std::mutex g_mutex;
    bool   g_connected = false;
    HANDLE g_shm = nullptr;
    HANDLE g_reqEvent = nullptr;
    HANDLE g_respEvent = nullptr;
    uint8_t* g_base = nullptr;
    std::unordered_map<uint64_t, std::string> g_cache; // (op<<32 | fdid) -> path ("" = known-absent)

    constexpr uint32_t kRequestTimeoutMs = 2000;

    // Directory of this module (Wraith.dll), where WraithHost.exe is deployed alongside it.
    std::string ModuleDir()
    {
        HMODULE hm = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&ModuleDir), &hm);
        char path[MAX_PATH];
        DWORD n = GetModuleFileNameA(hm, path, MAX_PATH);
        std::string s(path, n);
        size_t slash = s.find_last_of("\\/");
        return (slash == std::string::npos) ? std::string(".") : s.substr(0, slash);
    }

    // Assumes g_mutex is held.
    bool ConnectLocked()
    {
        if (g_connected) return true;

        g_shm = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, kShmName);
        if (!g_shm) return false;
        g_base = static_cast<uint8_t*>(MapViewOfFile(g_shm, FILE_MAP_ALL_ACCESS, 0, 0, kShmSize));
        if (!g_base) { CloseHandle(g_shm); g_shm = nullptr; return false; }

        auto* hdr = reinterpret_cast<ControlHeader*>(g_base);
        if (hdr->magic != kMagic || hdr->version != kVersion)
        {
            UnmapViewOfFile(g_base); g_base = nullptr;
            CloseHandle(g_shm); g_shm = nullptr;
            return false;
        }

        g_reqEvent  = OpenEventA(EVENT_ALL_ACCESS, FALSE, kReqEvent);
        g_respEvent = OpenEventA(EVENT_ALL_ACCESS, FALSE, kRespEvent);
        if (!g_reqEvent || !g_respEvent)
        {
            UnmapViewOfFile(g_base); g_base = nullptr;
            CloseHandle(g_shm); g_shm = nullptr;
            return false;
        }

        g_connected = true;
        return true;
    }

    void DisconnectLocked()
    {
        g_connected = false;
        if (g_base) { UnmapViewOfFile(g_base); g_base = nullptr; }
        if (g_shm) { CloseHandle(g_shm); g_shm = nullptr; }
        if (g_reqEvent) { CloseHandle(g_reqEvent); g_reqEvent = nullptr; }
        if (g_respEvent) { CloseHandle(g_respEvent); g_respEvent = nullptr; }
    }
}

namespace wraith::features::ipc
{
    void EnsureHostRunning()
    {
        // Already up? (the host created the mailbox)
        HANDLE existing = OpenFileMappingA(FILE_MAP_READ, FALSE, kShmName);
        if (existing) { CloseHandle(existing); return; }

        // The host and its data live in the client's "Utils\" subfolder.
        std::string dir = ModuleDir() + "\\Utils";
        std::string exe = dir + "\\WraithHost.exe";

        // Pass our PID so the host can exit when this client closes.
        char cmd[128];
        wsprintfA(cmd, "WraithHost.exe --client-pid %lu", GetCurrentProcessId());

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        // Own console window (shows the live client<->host IPC); cwd = host dir (data + logs resolve there).
        if (CreateProcessA(exe.c_str(), cmd, nullptr, nullptr, FALSE,
                           CREATE_NEW_CONSOLE, nullptr, dir.c_str(), &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    bool Connect()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return ConnectLocked();
    }

    bool IsConnected()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_connected;
    }

    std::string Resolve(uint32_t op, uint32_t arg, uint32_t arg2)
    {
        const uint64_t key = (static_cast<uint64_t>(op) << 48) |
                             (static_cast<uint64_t>(arg2 & 0xFFFF) << 32) | arg;

        std::lock_guard<std::mutex> lock(g_mutex);

        auto it = g_cache.find(key);
        if (it != g_cache.end()) return it->second;

        if (!ConnectLocked()) return ""; // host absent: do not cache, so a later host start is picked up

        auto* hdr = reinterpret_cast<ControlHeader*>(g_base);
        uint8_t* payload = g_base + kHeaderSize;

        flexbuffers::Builder fbb;
        fbb.Vector([&]() { fbb.UInt(op); fbb.UInt(arg); fbb.UInt(arg2); });
        fbb.Finish();
        const std::vector<uint8_t>& buf = fbb.GetBuffer();
        if (buf.size() > kPayloadMax) return "";
        memcpy(payload, buf.data(), buf.size());
        hdr->reqLen = static_cast<uint32_t>(buf.size());
        ++hdr->reqSeq;
        SetEvent(g_reqEvent);

        if (WaitForSingleObject(g_respEvent, kRequestTimeoutMs) != WAIT_OBJECT_0)
        {
            DisconnectLocked(); // host stalled/died; reconnect next time
            return "";
        }

        std::string result;
        if (hdr->respLen && hdr->respLen <= kPayloadMax)
        {
            auto vec = flexbuffers::GetRoot(payload, hdr->respLen).AsVector();
            if (vec[0].AsUInt32() == StOk) result = vec[1].AsString().str();
        }

        g_cache[key] = result; // cache connected results (including known-absent "")
        return result;
    }

    std::string TexturePath(uint32_t fileDataId) { return Resolve(OpResolveTexture, fileDataId); }
    std::string ModelPath(uint32_t fileDataId)   { return Resolve(OpResolveModel, fileDataId); }
    std::string MaterialPath(uint32_t materialResId, uint32_t textureType)
    {
        return Resolve(OpResolveMaterial, materialResId, textureType);
    }
}
