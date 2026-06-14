// WraithHost.exe (64-bit): holds the FileDataID->path tables and the MPQ archive set, and serves resolution
// and file requests to Wraith.dll over a shared-memory mailbox.

#include "Features/Ipc/Protocol.hpp"
#include "Features/DB2/DB2File.hpp"
#include "Core/Logger.hpp"
#include "MpqStore.hpp"

#include <flatbuffers/flexbuffers.h>

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

using namespace wraith;
using namespace wraith::ipc;
using wraith::features::db2::DB2File;
using wraith::features::db2::DB2Table;
using wraith::host::MpqStore;

namespace
{
    // Both *FilePath.db2 decode to {uint32 id; int32 path}.
    struct PathRow { uint32_t id; int32_t path; };

    DB2Table<PathRow> g_tex;
    DB2Table<PathRow> g_model;

    // texturefiledata.db2 decodes to {FileDataID(id), MaterialResourcesID, textureType, relMRID}.
    struct TexDataRow { uint32_t fileDataId; uint32_t materialResId; uint32_t textureType; uint32_t rel; };
    DB2Table<TexDataRow> g_texData;
    // MaterialResourcesID -> [(textureType, FileDataID)] (a MRID can have several entries).
    std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> g_mridIndex;

    std::string g_dataDir; // defaults to the host's own exe directory (set in main)
    DWORD g_clientPid = 0;  // the game process to shadow; host exits when it closes

    // MPQ archive set + open-file table. Large files are kept OPEN (lazy) and read by range on demand, so the
    // host never holds whole files in RAM; small files are served inline at open and keep no handle.
    wraith::host::MpqStore g_mpq;
    std::unordered_map<uint32_t, wraith::host::MpqStore::LazyFile> g_lazyHandles; // handleId -> open file
    uint32_t g_nextHandle = 0;

    // Waits for the client process to exit, then terminates the host.
    DWORD WINAPI ClientWatcher(LPVOID clientHandle)
    {
        WaitForSingleObject(static_cast<HANDLE>(clientHandle), INFINITE);
        ExitProcess(0);
        return 0;
    }

    std::string ExeDir()
    {
        char p[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, p, MAX_PATH);
        std::string s(p, n);
        size_t slash = s.find_last_of("\\/");
        return (slash == std::string::npos) ? std::string(".") : s.substr(0, slash);
    }

    bool LoadDisk(DB2File& table, const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) { WLOG_ERROR("host: cannot open %s", path.c_str()); return false; }
        std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return table.LoadBytes(buf.data(), static_cast<uint32_t>(buf.size()), path.c_str());
    }

    // Resolve a db2 by name. We run from the client's Utils folder, so the client root is our parent.
    std::string FindDb2(const std::string& name)
    {
        std::string parent = g_dataDir;
        size_t slash = parent.find_last_of("\\/");
        if (slash != std::string::npos) parent = parent.substr(0, slash);

        std::string cands[] = {
            parent + "\\Data\\Patch-4.MPQ\\DBFilesClient\\" + name,
            parent + "\\" + name,
            g_dataDir + "\\" + name,
        };
        for (const std::string& p : cands)
        {
            std::ifstream f(p, std::ios::binary);
            if (f) return p;
        }
        return cands[0];
    }

    void BuildMridIndex()
    {
        for (uint32_t i = 0; i < g_texData.RowCount(); ++i)
        {
            const TexDataRow* r = g_texData.At(i);
            if (r) g_mridIndex[r->materialResId].push_back({ r->textureType, r->fileDataId });
        }
    }

    // MaterialResourcesID -> FileDataID, preferring textureType == want, then 2 (both), then any.
    uint32_t MridToFdid(uint32_t mrid, uint32_t want)
    {
        auto it = g_mridIndex.find(mrid);
        if (it == g_mridIndex.end()) return 0;
        for (uint32_t target : { want, 2u })
            for (const auto& c : it->second)
                if (c.first == target) return c.second;
        return it->second[0].second;
    }

    const char* Resolve(uint32_t op, uint32_t arg, uint32_t arg2)
    {
        if (op == OpResolveMaterial)
        {
            uint32_t fdid = MridToFdid(arg, arg2);
            if (!fdid) return nullptr;
            const PathRow* p = g_tex.Find(static_cast<int32_t>(fdid));
            return p ? g_tex.Str(static_cast<uint32_t>(p->path)) : nullptr;
        }
        DB2Table<PathRow>& t = (op == OpResolveModel) ? g_model : g_tex;
        const PathRow* r = t.Find(static_cast<int32_t>(arg));
        return r ? t.Str(static_cast<uint32_t>(r->path)) : nullptr;
    }

    int Serve()
    {
        // One host per session: if another instance already owns the mailbox, exit quietly.
        HANDLE singleton = CreateMutexA(nullptr, FALSE, "Local\\WraithHostSingleton");
        if (singleton && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            WLOG_INFO("host: another instance is running, exiting");
            return 0;
        }

        // Exit when the game process that spawned us closes.
        if (g_clientPid)
        {
            HANDLE client = OpenProcess(SYNCHRONIZE, FALSE, g_clientPid);
            if (client) CreateThread(nullptr, 0, ClientWatcher, client, 0, nullptr);
        }

        LoadDisk(g_tex,     FindDb2("TextureFilePath.db2"));
        LoadDisk(g_model,   FindDb2("ModelFilePath.db2"));
        LoadDisk(g_texData, FindDb2("texturefiledata.db2"));
        BuildMridIndex();
        WLOG_INFO("host: loaded texpath=%u model=%u texdata=%u (MRID index=%zu)",
                  g_tex.RowCount(), g_model.RowCount(), g_texData.RowCount(), g_mridIndex.size());

        // Mount the client's MPQ archive set (client root = parent of the Utils folder).
        {
            std::string root = g_dataDir;
            size_t slash = root.find_last_of("\\/");
            if (slash != std::string::npos) root = root.substr(0, slash);
            g_mpq.Mount(root);
        }

        HANDLE shm = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kShmSize, kShmName);
        if (!shm) { WLOG_ERROR("host: CreateFileMapping failed %lu", GetLastError()); return 1; }
        auto* base = static_cast<uint8_t*>(MapViewOfFile(shm, FILE_MAP_ALL_ACCESS, 0, 0, kShmSize));
        if (!base) { WLOG_ERROR("host: MapViewOfFile failed %lu", GetLastError()); return 1; }

        auto* hdr = reinterpret_cast<ControlHeader*>(base);
        ZeroMemory(hdr, sizeof(*hdr));
        hdr->magic = kMagic;
        hdr->version = kVersion;
        uint8_t* payload = base + kHeaderSize;

        HANDLE reqEv  = CreateEventA(nullptr, FALSE, FALSE, kReqEvent);
        HANDLE respEv = CreateEventA(nullptr, FALSE, FALSE, kRespEvent);
        if (!reqEv || !respEv) { WLOG_ERROR("host: CreateEvent failed %lu", GetLastError()); return 1; }

        SetConsoleTitleA("WraithHost  -  client <-> host IPC");
        printf("WraithHost serving (tex=%u rows, model=%u rows). Waiting for requests...\n\n",
               g_tex.RowCount(), g_model.RowCount());
        WLOG_INFO("host: serving");
        uint32_t served = 0;

        for (;;)
        {
            if (WaitForSingleObject(reqEv, INFINITE) != WAIT_OBJECT_0) break;

            flexbuffers::Builder fbb;
            uint32_t op = 0;
            if (hdr->reqLen && hdr->reqLen <= kPayloadMax)
            {
                auto vec = flexbuffers::GetRoot(payload, hdr->reqLen).AsVector();
                op = vec[0].AsUInt32();

                switch (op)
                {
                case OpFileOpen:
                {
                    std::string name = vec[1].AsString().str();
                    MpqStore::LazyFile lf;
                    bool ok = g_mpq.OpenLazy(name, lf);
                    uint32_t size = lf.size;
                    if (!ok)
                    {
                        fbb.Vector([&]() { fbb.UInt(StNotFound); fbb.UInt(0); fbb.UInt(0); });
                        printf("[#%u] open  %-40s -> MISS\n", ++served, name.c_str());
                    }
                    else if (size <= kInlineMax)
                    {
                        // Small file: read it whole and send the bytes inline (id=0); keep no handle.
                        std::vector<uint8_t> bytes(size);
                        if (size) g_mpq.ReadRange(lf, 0, bytes.data(), size);
                        g_mpq.CloseLazy(lf);
                        fbb.Vector([&]() {
                            fbb.UInt(StOk); fbb.UInt(0); fbb.UInt(size);
                            fbb.Blob(bytes.data(), bytes.size());
                        });
                        printf("[#%u] open  %-40s -> OK inline (%u B)\n", ++served, name.c_str(), size);
                    }
                    else
                    {
                        // Large file: keep the file OPEN (no whole-file read); client pulls ranges on demand.
                        uint32_t id = ++g_nextHandle;
                        g_lazyHandles.emplace(id, std::move(lf));
                        fbb.Vector([&]() { fbb.UInt(StOk); fbb.UInt(id); fbb.UInt(size); });
                        printf("[#%u] open  %-40s -> OK lazy id=%u (%u B)\n", ++served, name.c_str(), id, size);
                    }
                    break;
                }
                case OpFileRead:
                {
                    uint32_t id = vec[1].AsUInt32(), off = vec[2].AsUInt32(), len = vec[3].AsUInt32();
                    auto it = g_lazyHandles.find(id);
                    if (it == g_lazyHandles.end())
                    {
                        fbb.Vector([&]() { fbb.UInt(StBadRequest); fbb.Blob(nullptr, 0); });
                    }
                    else
                    {
                        if (len > kFileChunkMax) len = kFileChunkMax;
                        std::vector<uint8_t> tmp(len);
                        uint32_t n = g_mpq.ReadRange(it->second, off, tmp.data(), len);
                        fbb.Vector([&]() { fbb.UInt(StOk); fbb.Blob(tmp.data(), n); });
                    }
                    break;
                }
                case OpFileClose:
                {
                    uint32_t id = vec[1].AsUInt32();
                    auto it = g_lazyHandles.find(id);
                    if (it != g_lazyHandles.end()) { g_mpq.CloseLazy(it->second); g_lazyHandles.erase(it); }
                    fbb.Vector([&]() { fbb.UInt(StOk); });
                    break;
                }
                case OpFileExists:
                {
                    std::string name = vec[1].AsString().str();
                    bool ok = g_mpq.Exists(name);
                    fbb.Vector([&]() { fbb.UInt(ok ? StOk : StNotFound); });
                    break;
                }
                default: // resolve ops (texture/model/material)
                {
                    uint32_t arg = vec[1].AsUInt32();
                    uint32_t arg2 = vec.size() > 2 ? vec[2].AsUInt32() : 0;
                    const char* path = Resolve(op, arg, arg2);
                    const char* opn = (op == OpResolveMaterial) ? "mat" : (op == OpResolveModel) ? "model" : "tex";
                    printf("[#%u] %-5s arg=%-9u -> %s\n", ++served, opn, arg, path ? path : "(not found)");
                    fbb.Vector([&]() { fbb.UInt(path ? StOk : StNotFound); fbb.String(path ? path : ""); });
                    break;
                }
                }
            }
            else
            {
                fbb.Vector([&]() { fbb.UInt(StBadRequest); });
            }
            fflush(stdout);

            fbb.Finish();
            const std::vector<uint8_t>& buf = fbb.GetBuffer();

            uint32_t len = static_cast<uint32_t>(buf.size());
            if (len > kPayloadMax) len = 0;
            memcpy(payload, buf.data(), len);
            hdr->respLen = len;
            hdr->respSeq = hdr->reqSeq;
            SetEvent(respEv);
        }
        return 0;
    }

}

int main(int argc, char** argv)
{
    wraith::log::Init();
    g_dataDir = ExeDir(); // --data overrides

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) g_dataDir = argv[++i];
        else if (a == "--client-pid" && i + 1 < argc) g_clientPid = static_cast<DWORD>(strtoul(argv[++i], nullptr, 10));
    }

    return Serve();
}
