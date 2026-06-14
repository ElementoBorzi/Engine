#include "Features/DB2/DB2Mgr.hpp"

#include "Core/Logger.hpp"

namespace wraith::features::db2
{
    namespace
    {
        struct Entry { DB2File* table; const char* name; };
        std::vector<Entry> g_tables;
    }

    void Install()
    {
        // Tables load lazily via the Definition singletons. The probe observes CharSections lookups.
        InstallCharSectionsProbe();
        WLOG_INFO("DB2: subsystem ready");
    }

    void Track(DB2File* table, const char* fileName)
    {
        for (const auto& e : g_tables) if (e.table == table) return;
        g_tables.push_back({ table, fileName });
    }

    void UnloadAll()
    {
        for (auto& e : g_tables) e.table->Unload();
    }

    void ReloadAll()
    {
        for (auto& e : g_tables)
        {
            e.table->Unload();
            e.table->Load(e.name);
        }
    }
}
