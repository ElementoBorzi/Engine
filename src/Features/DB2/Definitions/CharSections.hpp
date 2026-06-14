#pragma once

#include "Features/DB2/DB2File.hpp"
#include "Features/DB2/DB2Mgr.hpp"

// CharSections.db2.
namespace wraith::features::db2::defs
{
    struct CharSectionsRow
    {
        uint32_t id;
        uint32_t textureName[3];   // material/texture file references
        uint16_t flags;
        uint8_t  raceID;
        uint8_t  sexID;
        uint8_t  baseSection;
        uint8_t  variationIndex;
        uint8_t  colorIndex;
    };

    class CharSections : public DB2Table<CharSectionsRow>
    {
    public:
        static CharSections& Get()
        {
            static CharSections instance;
            if (!instance.IsLoaded())
            {
                instance.Load("CharSections.db2");
                Track(&instance, "CharSections.db2");
            }
            return instance;
        }

        // Native CharSections.dbc override target.
        // Native record = 10 columns, 0x28 bytes: id, race(+4), sex(+8), baseSection(+0xc), TextureName[3]
        // (+0x10/+0x14/+0x18 = char* paths), Flags(+0x1c), VariationIndex(+0x20), ColorIndex(+0x24).
        // The render path reads rows through the consumer accessor, NOT the DBC object directly, so the override hooks the accessor.
        const NativeOverride* Override() const override
        {
            static const NativeOverride o {
                "CharSections.dbc",
                0x00AD332C,   // WowClientDB global object
                0x00AD3334,   // recordCount field
                0x00AD3348,   // recordData pointer field
                10,           // native columns
                0x28,         // native row size
                0x004F3BA0,   // consumer accessor (hook point)
                0x004F3DD0,   // cache builder
                0x00B6B864,   // consumer cache root
            };
            return &o;
        }
    };
}
