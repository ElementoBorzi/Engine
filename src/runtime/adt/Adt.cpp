// Terrain per-layer UV-scale consumer: applies the host's ATSC texture-scale table at terrain draw.
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

#include "runtime/adt/Adt.hpp"

#include "core/Hook.hpp"
#include "core/Logger.hpp"
#include "offsets/engine/Gx.hpp"
#include "offsets/game/ADT.hpp"

// The ATSC format is owned by the modern-adt module (the host emits it); reuse the single definition.
#include "../../../scripts/wxl-modern-adt/shared/AdtTexScale.hpp"

#include <windows.h>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace off = wxl::offsets::game::adt;
namespace gx  = wxl::offsets::engine::gx;
namespace sa  = wxl::modern::adt;

namespace wxl::runtime::adt
{
    namespace
    {
        // Normalized texture name -> UV-scale exponent (1<<n). Accumulated across all served ADTs; scale is
        // a per-asset property so duplicates across tiles are idempotent.
        std::unordered_map<std::string, uint8_t> g_texScale;

        off::Map_BuildTerrainConstantsFn g_origBuild = nullptr;

        std::string Normalize(const char* s)
        {
            std::string r(s);
            for (char& c : r) c = (c == '/') ? '\\' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
            // The runtime texture name carries no extension; the host table key keeps ".blp". Strip it on
            // both sides so the keys match.
            if (r.size() >= 4 && r.compare(r.size() - 4, 4, ".blp") == 0)
                r.erase(r.size() - 4);
            return r;
        }

        bool EndsWithCI(const char* s, const char* suffix)
        {
            const size_t ls = strlen(s), lf = strlen(suffix);
            if (lf > ls) return false;
            for (size_t i = 0; i < lf; ++i)
                if (tolower(static_cast<unsigned char>(s[ls - lf + i])) != suffix[i]) return false;
            return true;
        }

        // SEH-read the node's layer count and each layer's resolved-texture name into local buffers (no C++
        // objects, so it is safe inside __try).
        bool ReadLayerNames(void* node, uint8_t& count, char names[4][260])
        {
            __try
            {
                count = *reinterpret_cast<uint8_t*>(static_cast<char*>(node) + off::kOffChunkNodeLayerCount);
                if (count > 4) count = 4;
                for (uint8_t i = 0; i < count; ++i)
                {
                    names[i][0] = '\0';
                    void* ctex = *reinterpret_cast<void**>(static_cast<char*>(node) +
                        off::kOffChunkLayerRecords + i * off::kChunkLayerRecordStride + off::kOffLayerSlotTexture);
                    if (!ctex)
                        continue;
                    const char* nm = reinterpret_cast<const char*>(static_cast<char*>(ctex) + off::kOffTextureName);
                    size_t j = 0;
                    for (; j + 1 < 260 && nm[j]; ++j) names[i][j] = nm[j];
                    names[i][j] = '\0';
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // SEH-apply: divide c18+i.xy by 1<<exp[i] (a bigger texture covers more ground, so it tiles less),
        // then re-upload c18..c(18+count-1) so the GPU sees it.
        void ApplyScale(uint8_t count, const uint8_t exp[4])
        {
            __try
            {
                for (uint8_t i = 0; i < count; ++i)
                {
                    if (!exp[i])
                        continue;
                    float* c = reinterpret_cast<float*>(off::kVsConstC18 + i * 0x10);
                    const float f = 1.0f / static_cast<float>(1u << exp[i]);
                    c[0] *= f;
                    c[1] *= f;
                }
                void* device = *reinterpret_cast<void**>(gx::kGxDevicePtr);
                if (!device)
                    return;
                void** vt = *reinterpret_cast<void***>(device);
                auto setConst = reinterpret_cast<gx::Gx_SetShaderConstantFn>(vt[gx::kVtSetShaderConstant]);
                setConst(device, nullptr, 0, off::kVsConstC18Reg,
                         reinterpret_cast<const float*>(off::kVsConstC18), count);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        void __cdecl BuildConstantsDetour(void* node, uint32_t a1, uint32_t a2)
        {
            g_origBuild(node, a1, a2); // native build + upload of the per-chunk constant block

            if (g_texScale.empty())
                return;

            uint8_t count = 0;
            char names[4][260];
            if (!ReadLayerNames(node, count, names) || count == 0)
                return;

            uint8_t exp[4] = { 0, 0, 0, 0 };
            bool any = false;
            for (uint8_t i = 0; i < count; ++i)
            {
                if (!names[i][0])
                    continue;
                auto it = g_texScale.find(Normalize(names[i]));
                if (it != g_texScale.end() && it->second) { exp[i] = it->second; any = true; }
            }
            if (any)
                ApplyScale(count, exp);
        }
    }

    uint32_t IngestAdtBytes(const char* name, const uint8_t* buffer, uint32_t size)
    {
        if (!name || !buffer || size < 8 || !EndsWithCI(name, ".adt"))
            return size;

        sa::iff::Reader reader(std::span<const uint8_t>(buffer, size));
        uint32_t trim = size;

        sa::iff::Chunk atsc{};
        if (reader.Find(sa::kATSC, atsc))
        {
            std::vector<sa::TexScaleEntry> entries;
            if (sa::ParseTexScales(atsc.data, atsc.size, entries))
                for (sa::TexScaleEntry& e : entries)
                    if (e.exponent)
                        g_texScale[Normalize(e.name.c_str())] = e.exponent;
            if (atsc.pos - 8 < trim) trim = atsc.pos - 8; // hide the table from the native loader
        }

        return trim;
    }

    void Install()
    {
        wxl::core::hook::Install("Adt::BuildTerrainConstants", off::kBuildTerrainConstants,
                                 reinterpret_cast<void*>(&BuildConstantsDetour),
                                 reinterpret_cast<void**>(&g_origBuild));
        WLOG_INFO("ADT: terrain UV-scale hook installed");
    }
}
