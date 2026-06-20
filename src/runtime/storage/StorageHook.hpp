// Storage I/O hook: launch the host, then forward archive file opens to it (asset-agnostic).
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

// Launches the asset host (if installed) and hooks the client archive file-I/O primitives so a file the
// host serves is read from the host's bytes instead of the native archives; everything else runs native.
// The hooks are harmless with no host (every open falls through). Call once at startup, BEFORE EnableAll.
namespace wxl::runtime::storage
{
    void Install();
}
