// MpqStore: asset-agnostic archive I/O (StormLib). Serves raw bytes; not a translator.
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
#include <string_view>
#include <vector>

// Asset-AGNOSTIC archive I/O. It serves raw bytes; whether a file needs reshaping is the asset handlers'
// decision (the host stays format-blind). Per-worker instance: StormLib handles are single-thread.
namespace wxl::host::mpq
{
    class MpqStore
    {
    public:
        // Mount the client MPQ set (locale + base archives + loose Patch*.MPQ override folders).
        bool Mount(std::string_view dataDir);

        // True if `name` exists in any mounted archive or loose root.
        bool Exists(std::string_view name) const;

        // Read the whole file into `out`. Returns false if absent.
        bool ReadAll(std::string_view name, std::vector<uint8_t>& out) const;

        // Read a byte range (for streaming large files).
        bool ReadRange(std::string_view name, uint32_t off, uint32_t len, std::vector<uint8_t>& out) const;

        ~MpqStore();

    private:
        // Highest priority first (search order). StormLib handles still mutate on read, so mutable.
        mutable std::vector<void*> m_archives;     // StormLib HANDLEs
        std::vector<std::string>   m_archiveNames; // parallel, for logging
        std::vector<std::string>   m_looseRoots;   // absolute folder paths, trailing slash
        std::string                m_locale;
    };
}
