#include <windows.h>

#include "Core/Logger.hpp"
#include "Core/Hook.hpp"
#include "Features/M2/MD21.hpp"
#include "Features/Anim/Anim.hpp"
#include "Features/Ribbon/Ribbon.hpp"
#include "Features/DB2/DB2Mgr.hpp"
#include "Features/Storage/StorageHook.hpp"
#include "Features/Ipc/ShmClient.hpp"

extern "C" __declspec(dllexport) void Wraith() {}

namespace
{
    // Remaining engine feature hooks, installed off the loader lock.
    DWORD WINAPI Bootstrap(LPVOID)
    {
        wraith::features::m2::Install();
        wraith::features::anim::Install();
        wraith::features::ribbon::Install();
        wraith::features::db2::Install();
        wraith::hook::EnableAll();
        WLOG_INFO("WRAITH features ready.");
        return 0;
    }

    // Max wait for the host at launch; on timeout the client boots on its native archives.
    constexpr uint32_t kHostReadyTimeoutMs = 15000;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);

        wraith::log::Init();
        WLOG_INFO("WRAITH starting (build %s %s)", __DATE__, __TIME__);

        if (wraith::hook::Init())
        {
            // Storm hooks must be live before the engine opens its first file.
            wraith::features::storage::Install();
            wraith::hook::EnableAll();

            // Hold the loader here until the host is ready, so every file open is served by the host from the
            // start and the client never reads its own MPQs (mounted archives remain only as a fallback).
            wraith::features::ipc::EnsureHostRunning();
            wraith::features::ipc::WaitForHost(kHostReadyTimeoutMs);

            CloseHandle(CreateThread(nullptr, 0, &Bootstrap, nullptr, 0, nullptr));
        }
    }
    return TRUE;
}
