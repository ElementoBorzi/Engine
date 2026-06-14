#pragma once

// Client-side takeover of the engine Storm/MPQ read path. Hooks the file primitives (open/size/read/seek/
// close) and serves the bytes from WraithHost. Files the host lacks, or any call while the host is absent,
// fall through to the native archives.
namespace wraith::features::storage
{
    // Register the Storm primitive hooks (enable with hook::EnableAll()).
    void Install();
}
