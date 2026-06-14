// MpqStore (host-only, 64-bit): opens the client's MPQ archive set with StormLib in the same priority
// order the native client mount, plus the loose // "Patch-N.MPQ\" override folders, 
// and resolves a file by its archive-internal name. This is the host
// side of the "Wraith owns the MPQ read path" work: the client forwards its Storm file IO here.
#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace wraith::host
{
    class MpqStore
    {
    public:
        ~MpqStore();

        // Mount every archive that exists under <clientRoot>\Data (and \Data\<locale>) in priority order.
        // locale empty -> auto-detect the 4-letter locale folder. Returns true if at least one archive opened.
        bool Mount(const std::string& clientRoot, const std::string& locale = "");

        // True if the file resolves in any mounted source.
        bool Exists(const std::string& name);

        // Lazy (open-and-keep) access: resolve a file and keep it open so ranges can be read on demand
        // without holding the whole file in RAM. Used to serve large files chunk by chunk.
        struct LazyFile
        {
            bool          isLoose = false;
            void*         mpqFile = nullptr; // StormLib file HANDLE when !isLoose
            std::ifstream loose;             // when isLoose
            uint32_t      size = 0;
        };
        // Resolve + open by name. Fills out (incl. size). Returns true on hit.
        bool OpenLazy(const std::string& name, LazyFile& out);
        // Read len bytes at offset off into dst (clamped to file size). Returns bytes read.
        uint32_t ReadRange(LazyFile& f, uint32_t off, void* dst, uint32_t len);
        // Close a LazyFile opened by OpenLazy.
        void CloseLazy(LazyFile& f);

        uint32_t ArchiveCount() const { return static_cast<uint32_t>(m_archives.size()); }
        uint32_t LooseRootCount() const { return static_cast<uint32_t>(m_looseRoots.size()); }
        const std::string& Locale() const { return m_locale; }

    private:
        // Highest priority first (search order).
        std::vector<void*>       m_archives;     // StormLib HANDLEs
        std::vector<std::string> m_archiveNames; // parallel, for logging
        std::vector<std::string> m_looseRoots;   // absolute folder paths, trailing slash
        std::string m_locale;
    };
}
