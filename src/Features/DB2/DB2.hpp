#pragma once

#include <cstdint>
#include <vector>

// DB2 (WDC1 / WDC2 / WDC3) decoder. Reads a .db2 file and normalises every record into a
// flat array of 4-byte columns. Implements the field storage model from wowdev.wiki/DB2:
//   - storage type 0 (none):                   field bytes are inline in the record.
//   - storage type 1 (bitpacked):              field is a bitfield in the record.
//   - storage type 2 (common data):            field is a default value, overridden per-id in common_data.
//   - storage type 3 (bitpacked indexed):      field is a bitpacked index into pallet_data.
//   - storage type 4 (bitpacked indexed array):field is one index that expands to an array in pallet_data.
// It also resolves the id list / inline id, copy table (duplicated rows), relationship map (appended as a
// trailing column) and multiple sections.
//
// Each output column is 4 bytes. 8/16-bit inline fields are zero-extended; a 64-bit inline field becomes
// two columns. Strings stay as offsets into the reconstructed string table (see DB2File::Str).
namespace wraith::features::db2
{
    struct DB2Decoded
    {
        uint32_t             rowSize = 0;  // decoded bytes per record (column count * 4, plus relationship)
        std::vector<char>    records;      // rowCount * rowSize
        std::vector<int32_t> ids;          // rowCount
        std::vector<char>    strings;      // reconstructed string table
        bool                 hasRelationship = false; // a trailing relationship column was appended
    };

    // Decode the file bytes into out. stringColumns/stringColumnCount may be null/0 (see DB2File).
    // Returns false (and leaves out cleared) if the data is not a supported DB2 or is malformed.
    bool DecodeDB2(const uint8_t* data, uint32_t size, DB2Decoded& out,
                   const uint32_t* stringColumns, uint32_t stringColumnCount);
}
