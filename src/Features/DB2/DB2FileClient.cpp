#include "Features/DB2/DB2File.hpp"

#include "Features/DB2/SFile.hpp"
#include "Core/Logger.hpp"

#include <vector>

// Client-only: DB2File::Load reads the table from the mounted archives via the client storage API (SFile).
// The host builds tables with LoadBytes after a plain disk read, so this TU is excluded from the host build.
namespace wraith::features::db2
{
    bool DB2File::Load(const char* fileName)
    {
        if (m_loaded) return true;

        std::vector<uint8_t> buf;
        if (!ReadWholeFile(fileName, buf))
        {
            WLOG_WARN("DB2: cannot read %s", fileName);
            return false;
        }
        return LoadBytes(buf.data(), static_cast<uint32_t>(buf.size()), fileName);
    }
}
