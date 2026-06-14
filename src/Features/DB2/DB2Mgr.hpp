#pragma once

#include "Features/DB2/DB2File.hpp"

#include <vector>

// DB2 subsystem entry point. Tables load lazily on first access (so they do not depend on when the
// archives are mounted); this manager only tracks the live tables so they can be unloaded/reloaded in bulk.
namespace wraith::features::db2
{
    void Install();

    // Register a table so UnloadAll/ReloadAll reach it. A Definition singleton calls this once.
    void Track(DB2File* table, const char* fileName);

    void UnloadAll();
    void ReloadAll();
}
