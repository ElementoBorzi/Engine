// ShmServer: the host side of the shared-memory transport (create window + per-channel events).
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
#include <span>
#include <vector>

#include "ipc/Protocol.hpp"

// Creates the shared-memory window + per-channel events, then reads requests / writes responses. The
// host owns (creates) the objects; the client opens them.
namespace wxl::host::ipc
{
    // Create and map the shared window + events. Host owns the objects.
    bool Create();

    // Block until channel `i` has a request; returns the request payload bytes.
    bool WaitRequest(uint32_t i, std::vector<uint8_t>& reqOut);

    // Write the response payload to channel `i` and signal the client.
    bool PostResponse(uint32_t i, std::span<const uint8_t> resp);
}
