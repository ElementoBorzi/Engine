// WMO game bindings: typed inline wrappers over the client's map-object functions.
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

#include "game/Binding.hpp"
#include "offsets/game/WMO.hpp"

/**
 * @brief Typed inline wrappers over the client map-object functions, exposed as the WMO binding catalog.
 */
namespace wxl::game::wmo
{
    namespace off = wxl::offsets::game::wmo;

    /**
     * @brief Resolves a material's texture-name offsets. The native does not bounds-check materialIndex.
     * @param model          Map-object model.
     * @param materialIndex  Material index to resolve.
     */
    inline void ResolveMaterialTexture(void* model, int materialIndex)
    {
        Native<off::Wmo_ResolveMaterialTextureFn>(off::kResolveMaterialTexture)(model, nullptr, materialIndex);
    }

    /**
     * @brief Queries the resident state of a group, optionally forcing residency.
     * @param model       Map-object model.
     * @param groupIndex  Group index to query.
     * @param force       Nonzero forces the group resident.
     * @return The group's resident state.
     */
    inline unsigned int GroupResident(void* model, unsigned int groupIndex, unsigned int force)
    {
        return Native<off::Wmo_GroupResidentFn>(off::kGroupResidentAccessor)(model, nullptr, groupIndex, force);
    }

    /**
     * @brief Reads the root buffer pointer.
     * @param root  Map-object root.
     * @return The root buffer pointer, or null on a null root.
     */
    inline void* RootBuffer(void* root)
    {
        if (!root)
            return nullptr;
        return static_cast<off::Root*>(root)->rootBuffer;
    }

    /**
     * @brief Reads the root buffer byte size (the bound the native chunk walk reads to).
     * @param root  Map-object root.
     * @return The root buffer byte size, or 0 on a null root.
     */
    inline uint32_t RootSize(void* root)
    {
        if (!root)
            return 0;
        return static_cast<off::Root*>(root)->rootSize;
    }

    /**
     * @brief Sets the root buffer byte size after reshaping the buffer in place.
     * @param root  Map-object root.
     * @param size  New byte size the native chunk walk should read to.
     */
    inline void SetRootSize(void* root, uint32_t size)
    {
        if (root)
            static_cast<off::Root*>(root)->rootSize = size;
    }

    /**
     * @brief Reads the group buffer pointer.
     * @param group  Map-object group.
     * @return The group buffer pointer, or null on a null group.
     */
    inline void* GroupBuffer(void* group)
    {
        if (!group)
            return nullptr;
        return static_cast<off::Group*>(group)->groupBuffer;
    }

    /**
     * @brief Reads the group buffer byte size (the bound the native sub-chunk walk reads to).
     * @param group  Map-object group.
     * @return The group buffer byte size, or 0 on a null group.
     */
    inline uint32_t GroupSize(void* group)
    {
        if (!group)
            return 0;
        return static_cast<off::Group*>(group)->groupSize;
    }

    /**
     * @brief Sets the group buffer byte size after reshaping the buffer in place.
     * @param group  Map-object group.
     * @param size   New byte size the native sub-chunk walk should read to.
     */
    inline void SetGroupSize(void* group, uint32_t size)
    {
        if (group)
            static_cast<off::Group*>(group)->groupSize = size;
    }

    /**
     * @brief Reads the group count (the group-array bound).
     * @param root  Map-object root.
     * @return The group count, or 0 on a null root.
     */
    inline uint32_t GroupCount(void* root)
    {
        if (!root)
            return 0;
        return static_cast<off::Root*>(root)->groupCount;
    }

    /**
     * @brief Reads the group runtime object at an index.
     * @param root  Map-object root.
     * @param i     Group index.
     * @return The group object, or null when out of range or on a null root.
     */
    inline void* GroupAt(void* root, uint32_t i)
    {
        if (!root || i >= GroupCount(root))
            return nullptr;
        return static_cast<off::Root*>(root)->groupArray[i];
    }

    /**
     * @brief Returns the inline file path of the map-object the camera is currently inside.
     *
     * The client keeps a pointer to the interior instance the camera is in (null when outdoors). That
     * instance points to its root, which carries an inline NUL-terminated path. Reads both hops with
     * null checks so an outdoor camera or a half-built instance yields null rather than a fault.
     * @return The root path, or null when the camera is not inside any map-object.
     */
    inline const char* CurrentInteriorPath()
    {
        void* instance = *reinterpret_cast<void**>(off::kCurrentInteriorInstance);
        if (!instance)
            return nullptr;
        void* root = *reinterpret_cast<void**>(static_cast<char*>(instance) + off::kOffInstanceRoot);
        if (!root)
            return nullptr;
        return static_cast<char*>(root) + off::kOffNameInline;
    }

    /** @brief Adds the WMO bindings to the enumerable catalog. */
    inline void RegisterCatalog()
    {
        Register({ "WMO::ResolveMaterialTexture", off::kResolveMaterialTexture,  "void(model, int materialIndex)" });
        Register({ "WMO::GroupResident",          off::kGroupResidentAccessor,   "uint(model, groupIndex, force)" });
    }
}
