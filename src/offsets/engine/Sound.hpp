// Sound system addresses: the master-volume control and its init guard.
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
#include <cstddef>

// INTERNAL to the core. The client sound-system master-volume control. Modules never include this; they
// use wxl::game::sound.
namespace wxl::offsets::engine::sound
{
    // Master-volume setter (volume in 0..1): writes the value to the sound group and flags it dirty;
    // the mixer applies it on the next audio tick. No-ops while the sound system is not up.
    constexpr uintptr_t kSetMasterVolume = 0x00879460;
    using SetMasterVolumeFn = void(__cdecl*)(float volume);

    // Non-zero once the sound system is initialized.
    constexpr uintptr_t kSoundActiveFlag = 0x00D43814;

    // Sound-group array pointer; the first group's field at +0x08 holds the live master-volume float.
    constexpr uintptr_t kSoundGroupArrayPtr  = 0x00D438FC;
    constexpr size_t    kOffGroupMasterVolume = 0x08;
}
