#include "MpqStore.hpp"

#include "Core/Logger.hpp"

#include <StormLib.h>

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>

using namespace wraith;

namespace
{
    bool FileExistsOnDisk(const std::string& path)
    {
        DWORD a = GetFileAttributesA(path.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool DirExistsOnDisk(const std::string& path)
    {
        DWORD a = GetFileAttributesA(path.c_str());
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    }

    std::string ToLower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // Archive-internal names use backslashes. Accept either separator from the client and normalise.
    std::string NormalizeName(const std::string& name)
    {
        std::string n = name;
        for (char& c : n) if (c == '/') c = '\\';
        // strip a leading separator
        size_t i = 0;
        while (i < n.size() && (n[i] == '\\')) ++i;
        return n.substr(i);
    }
}

namespace wraith::host
{
    MpqStore::~MpqStore()
    {
        for (void* h : m_archives) if (h) SFileCloseArchive(static_cast<HANDLE>(h));
        m_archives.clear();
    }

    bool MpqStore::Mount(const std::string& clientRoot, const std::string& locale)
    {
        std::string root = clientRoot;
        if (!root.empty() && (root.back() == '\\' || root.back() == '/')) root.pop_back();
        const std::string data = root + "\\Data";

        // --- locale: explicit, else the Data\<loc> folder that carries locale-<loc>.MPQ ---
        m_locale = locale;
        if (m_locale.empty())
        {
            WIN32_FIND_DATAA fd{};
            HANDLE h = FindFirstFileA((data + "\\*").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                    std::string d = fd.cFileName;
                    if (d == "." || d == "..") continue;
                    if (FileExistsOnDisk(data + "\\" + d + "\\locale-" + d + ".MPQ")) { m_locale = d; break; }
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
        }
        const std::string loc = m_locale;

        // --- loose override folders: Data\Patch*.MPQ that are DIRECTORIES (highest priority) ---
        {
            WIN32_FIND_DATAA fd{};
            HANDLE h = FindFirstFileA((data + "\\*").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE)
            {
                std::vector<std::string> dirs;
                do
                {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                    std::string d = fd.cFileName;
                    std::string dl = ToLower(d);
                    if (dl.rfind("patch", 0) == 0 && dl.size() > 4 && dl.substr(dl.size() - 4) == ".mpq")
                        dirs.push_back(d);
                } while (FindNextFileA(h, &fd));
                FindClose(h);
                std::sort(dirs.begin(), dirs.end(), [](const std::string& a, const std::string& b) {
                    return ToLower(a) > ToLower(b); // Patch-5 before Patch-4 ...
                });
                for (const std::string& d : dirs) m_looseRoots.push_back(data + "\\" + d + "\\");
            }
        }

        // Archive set, highest priority first (search order).
        std::vector<std::string> candidates = {
            // patches, descending (locale variant above its base-number sibling)
            "Data\\" + loc + "\\patch-" + loc + "-3.MPQ", "Data\\patch-3.MPQ",
            "Data\\" + loc + "\\patch-" + loc + "-2.MPQ", "Data\\patch-2.MPQ",
            "Data\\" + loc + "\\patch-" + loc + ".MPQ",   "Data\\patch.MPQ",
            // locale archives
            "Data\\" + loc + "\\locale-" + loc + ".MPQ",
            "Data\\" + loc + "\\base-" + loc + ".MPQ",
            "Data\\" + loc + "\\expansion-locale-" + loc + ".MPQ",
            "Data\\" + loc + "\\lichking-locale-" + loc + ".MPQ",
            "Data\\" + loc + "\\speech-" + loc + ".MPQ",
            "Data\\" + loc + "\\expansion-speech-" + loc + ".MPQ",
            "Data\\" + loc + "\\lichking-speech-" + loc + ".MPQ",
            "Data\\" + loc + "\\backup-" + loc + ".MPQ",
            // base / expansion
            "Data\\lichking.MPQ",
            "Data\\expansion.MPQ",
            "Data\\common-2.MPQ",
            "Data\\common.MPQ",
        };

        // Resolve by exact name only (never enumerate); skip the internal (listfile) and (attributes).
        const DWORD openFlags = MPQ_OPEN_READ_ONLY | MPQ_OPEN_NO_LISTFILE | MPQ_OPEN_NO_ATTRIBUTES;

        const ULONGLONG t0 = GetTickCount64();
        for (const std::string& rel : candidates)
        {
            std::string full = root + "\\" + rel;
            if (!FileExistsOnDisk(full)) continue;
            HANDLE hMpq = nullptr;
            if (SFileOpenArchive(full.c_str(), 0, openFlags, &hMpq) && hMpq)
            {
                m_archives.push_back(hMpq);
                m_archiveNames.push_back(rel);
            }
            else
            {
                WLOG_ERROR("mpq: open failed (%lu) %s", GetLastError(), rel.c_str());
            }
        }
        const ULONGLONG mountMs = GetTickCount64() - t0;

        WLOG_INFO("mpq: locale=%s, %zu archives, %zu loose roots, mounted in %llu ms",
                  m_locale.empty() ? "(none)" : m_locale.c_str(), m_archives.size(), m_looseRoots.size(),
                  static_cast<unsigned long long>(mountMs));
        for (size_t i = 0; i < m_archiveNames.size(); ++i)
            WLOG_INFO("mpq:   [%zu] %s", i, m_archiveNames[i].c_str());
        for (const std::string& lr : m_looseRoots)
            WLOG_INFO("mpq:   loose <- %s", lr.c_str());

        return !m_archives.empty() || !m_looseRoots.empty();
    }

    bool MpqStore::Exists(const std::string& rawName)
    {
        const std::string name = NormalizeName(rawName);
        for (const std::string& lr : m_looseRoots)
            if (FileExistsOnDisk(lr + name)) return true;
        for (void* a : m_archives)
            if (SFileHasFile(static_cast<HANDLE>(a), name.c_str())) return true;
        return false;
    }

    bool MpqStore::OpenLazy(const std::string& rawName, LazyFile& out)
    {
        const std::string name = NormalizeName(rawName);

        for (const std::string& lr : m_looseRoots)
        {
            std::ifstream f(lr + name, std::ios::binary | std::ios::ate);
            if (!f) continue;
            out.isLoose = true;
            out.size = static_cast<uint32_t>(f.tellg());
            f.seekg(0);
            out.loose = std::move(f);
            return true;
        }

        for (void* a : m_archives)
        {
            HANDLE hFile = nullptr;
            if (!SFileOpenFileEx(static_cast<HANDLE>(a), name.c_str(), 0, &hFile) || !hFile) continue;
            DWORD high = 0;
            DWORD sz = SFileGetFileSize(hFile, &high);
            if (sz == SFILE_INVALID_SIZE) { SFileCloseFile(hFile); continue; }
            out.isLoose = false;
            out.mpqFile = hFile;
            out.size = sz;
            return true;
        }
        return false;
    }

    uint32_t MpqStore::ReadRange(LazyFile& f, uint32_t off, void* dst, uint32_t len)
    {
        if (off >= f.size) return 0;
        if (len > f.size - off) len = f.size - off;
        if (len == 0) return 0;

        if (f.isLoose)
        {
            f.loose.clear();
            f.loose.seekg(off);
            f.loose.read(static_cast<char*>(dst), len);
            return static_cast<uint32_t>(f.loose.gcount());
        }

        SFileSetFilePointer(static_cast<HANDLE>(f.mpqFile), static_cast<LONG>(off), nullptr, FILE_BEGIN);
        DWORD read = 0;
        SFileReadFile(static_cast<HANDLE>(f.mpqFile), dst, len, &read, nullptr); // FALSE at exact EOF is fine
        return read;
    }

    void MpqStore::CloseLazy(LazyFile& f)
    {
        if (f.isLoose) { if (f.loose.is_open()) f.loose.close(); }
        else if (f.mpqFile) { SFileCloseFile(static_cast<HANDLE>(f.mpqFile)); f.mpqFile = nullptr; }
    }
}
