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

#include "runtime/storage/ShmClient.hpp"

#include "events/Event.hpp"
#include "host/ipc/Protocol.hpp"

#include <flatbuffers/flexbuffers.h>

#include <windows.h>
#include <atomic>
#include <mutex>

using namespace wxl::ipc;

namespace
{
    // --- connection state (set once at Connect, read-only afterwards) ---
    std::mutex g_connectMutex;          // guards the one-time connect/disconnect of the shared objects
    std::atomic<bool> g_connected{ false };
    HANDLE g_shm = nullptr;
    uint8_t* g_base = nullptr;
    HANDLE g_reqEvent[kChannels]  = {};
    HANDLE g_respEvent[kChannels] = {};

    // --- channel pool: a free channel is acquired per request, then released ---
    std::atomic<bool> g_channelBusy[kChannels] = {}; // false = free

    constexpr uint32_t kRequestTimeoutMs = 2000;

    // Directory of this module (the DLL), i.e. the client root.
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

    void DisconnectLocked()
    {
        g_connected.store(false);
        for (uint32_t i = 0; i < kChannels; ++i)
        {
            if (g_reqEvent[i])  { CloseHandle(g_reqEvent[i]);  g_reqEvent[i]  = nullptr; }
            if (g_respEvent[i]) { CloseHandle(g_respEvent[i]); g_respEvent[i] = nullptr; }
        }
        if (g_base) { UnmapViewOfFile(g_base); g_base = nullptr; }
        if (g_shm)  { CloseHandle(g_shm); g_shm = nullptr; }
    }

    // Open the shared window and all kChannels event pairs once. Guarded by g_connectMutex.
    bool ConnectInner()
    {
        if (g_connected.load()) return true;
        std::lock_guard<std::mutex> lock(g_connectMutex);
        if (g_connected.load()) return true;

        g_shm = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, kShmName);
        if (!g_shm) return false;
        g_base = static_cast<uint8_t*>(MapViewOfFile(g_shm, FILE_MAP_ALL_ACCESS, 0, 0, kShmSize));
        if (!g_base) { CloseHandle(g_shm); g_shm = nullptr; return false; }

        // The host stamps magic/version into channel 0's header.
        auto* hdr0 = ChannelHeader(g_base, 0);
        if (hdr0->magic != kMagic || hdr0->version != kVersion)
        {
            UnmapViewOfFile(g_base); g_base = nullptr;
            CloseHandle(g_shm); g_shm = nullptr;
            return false;
        }

        for (uint32_t i = 0; i < kChannels; ++i)
        {
            char rn[64], sn[64];
            ReqEventName(rn, sizeof(rn), i);
            RespEventName(sn, sizeof(sn), i);
            g_reqEvent[i]  = OpenEventA(EVENT_ALL_ACCESS, FALSE, rn);
            g_respEvent[i] = OpenEventA(EVENT_ALL_ACCESS, FALSE, sn);
            if (!g_reqEvent[i] || !g_respEvent[i]) { DisconnectLocked(); return false; }
        }

        g_connected.store(true);
        return true;
    }

    // Acquire a free channel index (atomic compare-exchange); spin/yield briefly on a full pool.
    uint32_t AcquireChannel()
    {
        for (;;)
        {
            for (uint32_t i = 0; i < kChannels; ++i)
            {
                bool expected = false;
                if (g_channelBusy[i].compare_exchange_strong(expected, true,
                        std::memory_order_acquire, std::memory_order_relaxed))
                    return i;
            }
            SwitchToThread();
        }
    }

    void ReleaseChannel(uint32_t i)
    {
        g_channelBusy[i].store(false, std::memory_order_release);
    }

    // Run one request on channel ch: write payload, bump reqSeq, signal, wait for response.
    bool SendOnChannel(uint32_t ch, const std::vector<uint8_t>& req)
    {
        if (req.size() > kChannelPayload) return false;
        auto* hdr = ChannelHeader(g_base, ch);
        uint8_t* payload = ChannelPayload(g_base, ch);
        memcpy(payload, req.data(), req.size());
        hdr->reqLen = static_cast<uint32_t>(req.size());
        ++hdr->reqSeq;
        SetEvent(g_reqEvent[ch]);
        return WaitForSingleObject(g_respEvent[ch], kRequestTimeoutMs) == WAIT_OBJECT_0;
    }

    // Full request round-trip: connect, acquire a free channel, send, parse the response while the channel
    // is still held (the payload lives in the shared window and is reused once released).
    template <class Fn>
    bool Transact(const std::vector<uint8_t>& req, Fn&& onResponse)
    {
        if (!ConnectInner()) return false;
        uint32_t ch = AcquireChannel();
        bool ok = SendOnChannel(ch, req);
        if (ok)
        {
            auto* hdr = ChannelHeader(g_base, ch);
            const uint8_t* payload = ChannelPayload(g_base, ch);
            if (hdr->respLen && hdr->respLen <= kChannelPayload)
                onResponse(flexbuffers::GetRoot(payload, hdr->respLen).AsVector());
        }
        ReleaseChannel(ch);
        return ok;
    }
}

namespace wxl::runtime::ipc
{
    void EnsureHostRunning()
    {
        HANDLE existing = OpenFileMappingA(FILE_MAP_READ, FALSE, kShmName);
        if (existing) { CloseHandle(existing); return; }

        std::string root = ModuleDir();
        std::string dir = root + "\\Utils";
        std::string exe = dir + "\\WarcraftXLHost.exe";

        // Let a module observe / veto the launch (e.g. it manages the host itself).
        bool cancel = false;
        wxl::events::HostLaunchArgs a{ exe.c_str(), &cancel };
        wxl::events::Emit(wxl::events::Event::OnBeforeHostLaunch, &a);
        if (cancel) return;

        // A "WarcraftXLConsole.flag" file next to the client turns the host console on (opt-in at runtime).
        bool console = GetFileAttributesA((root + "\\WarcraftXLConsole.flag").c_str()) != INVALID_FILE_ATTRIBUTES;

        // --client-pid lets the host exit when this client closes; --console enables its console output.
        char cmd[160];
        wsprintfA(cmd, "WarcraftXLHost.exe --client-pid %lu%s", GetCurrentProcessId(), console ? " --console" : "");

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        DWORD creationFlags = console ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW;
        if (CreateProcessA(exe.c_str(), cmd, nullptr, nullptr, FALSE,
                           creationFlags, nullptr, dir.c_str(), &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    bool WaitForHost(uint32_t timeoutMs)
    {
        for (uint32_t waited = 0; waited < timeoutMs; waited += 50)
        {
            HANDLE h = OpenFileMappingA(FILE_MAP_READ, FALSE, kShmName);
            if (h) { CloseHandle(h); return true; }
            Sleep(50);
        }
        return false;
    }

    bool Connect()     { return ConnectInner(); }
    bool IsConnected() { return g_connected.load(); }

    FileOpenResult FileOpen(const std::string& name, uint32_t flags)
    {
        flexbuffers::Builder fbb;
        fbb.Vector([&]() { fbb.UInt(OpFileOpen); fbb.String(name); fbb.UInt(flags); });
        fbb.Finish();

        FileOpenResult r{ false, 0, 0, {} };
        Transact(fbb.GetBuffer(), [&](const flexbuffers::Vector& vec) {
            if (vec[0].AsUInt32() != StOk) return;
            r = { true, vec[1].AsUInt32(), vec[2].AsUInt32(), {} };
            if (r.id == 0 && vec.size() > 3) // inline: copy bytes out of the shared window
            {
                auto blob = vec[3].AsBlob();
                r.inlineData.assign(blob.data(), blob.data() + blob.size());
            }
        });
        return r;
    }

    bool MapBlob(uint32_t id, uint32_t size, void*& outView, void*& outHandle)
    {
        char nm[64];
        BlobName(nm, sizeof(nm), id);
        HANDLE h = OpenFileMappingA(FILE_MAP_READ, FALSE, nm);
        if (!h) return false;
        void* v = MapViewOfFile(h, FILE_MAP_READ, 0, 0, size);
        if (!v) { CloseHandle(h); return false; }
        outView = v;
        outHandle = h;
        return true;
    }

    void UnmapBlob(void* view, void* handle)
    {
        if (view)   UnmapViewOfFile(view);
        if (handle) CloseHandle(static_cast<HANDLE>(handle));
    }

    uint32_t FileReadChunk(uint32_t id, uint32_t off, void* dst, uint32_t cap)
    {
        if (cap > kFileChunkMax) cap = kFileChunkMax;

        flexbuffers::Builder fbb;
        fbb.Vector([&]() { fbb.UInt(OpFileRead); fbb.UInt(id); fbb.UInt(off); fbb.UInt(cap); });
        fbb.Finish();

        uint32_t n = 0;
        Transact(fbb.GetBuffer(), [&](const flexbuffers::Vector& vec) {
            if (vec[0].AsUInt32() != StOk) return;
            auto blob = vec[1].AsBlob();
            n = static_cast<uint32_t>(blob.size());
            if (n > cap) n = cap;
            memcpy(dst, blob.data(), n);
        });
        return n;
    }

    void FileClose(uint32_t id)
    {
        flexbuffers::Builder fbb;
        fbb.Vector([&]() { fbb.UInt(OpFileClose); fbb.UInt(id); });
        fbb.Finish();
        Transact(fbb.GetBuffer(), [](const flexbuffers::Vector&) {}); // fire-and-forget release
    }

    bool FileExists(const std::string& name)
    {
        flexbuffers::Builder fbb;
        fbb.Vector([&]() { fbb.UInt(OpFileExists); fbb.String(name); });
        fbb.Finish();

        bool exists = false;
        Transact(fbb.GetBuffer(), [&](const flexbuffers::Vector& vec) {
            exists = vec[0].AsUInt32() == StOk;
        });
        return exists;
    }
}
