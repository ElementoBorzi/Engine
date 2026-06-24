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

#pragma once

#include <cstdint>

// The Client renders every terrain layer at one fixed UV tiling; modern tiles author a per-texture scale
// (so a texture covers more ground and repeats less) the Client ignores, leaving those layers visibly
// tiled. The host ships the per-texture scale exponents as a trailing ATSC table on the merged tile; this
// consumes it: it records the scales from each served ADT and, at terrain draw, divides each drawn layer's
// UV-tiling constant by 1<<exponent.
namespace wxl::runtime::adt
{
    /**
     * @brief Records a served ADT's trailing ATSC scale table and returns the bytes to actually serve.
     *
     * Parses the trailing ATSC chunk into the global texture-scale map, then returns the byte count up to
     * that chunk, so the native loader is served the ADT alone and never sees the table.
     * @param name    served file name (only .adt files carry the table).
     * @param buffer  served file bytes.
     * @param size    served byte count.
     * @return the byte count to serve (size when there is no table to trim).
     */
    uint32_t IngestAdtBytes(const char* name, const uint8_t* buffer, uint32_t size);

    /**
     * @brief Installs the terrain-constant post-hook that rescales each layer's UV tiling by its scale.
     */
    void Install();
}
