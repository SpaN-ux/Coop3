// CoopVC.asi - entry point.
//
// Loaded by whatever ASI loader the player has (docs/compat.md). One rule
// here: DllMain does almost nothing. It starts a thread and returns, because
// DllMain runs under the loader lock, before the other mods the player chose
// have patched anything. Installing a detour there would mean racing them
// over the same bytes.
//
// MILESTONE 3a (read-only): starts up safely, hooks the game's once-a-frame call
// and, from it, reads the player, the pools and the clock into the log. It
// reads the config, opens the log, refuses to run unless the game is the exact
// gta-vc.exe the addresses were derived from, waits for the game to finish
// starting up, and installs one detour. Nothing is written to the game and no
// socket is opened.
#include "clock.h"
#include "config.h"
#include "game/frame.h"
#include "game/verify.h"
#include "game/world.h"
#include "log.h"

#include <coopvc/protocol.h>
#include <coopvc/version.h>

#include <cstdio>
#include <cstdlib>

#include <windows.h>

namespace coopvc {

Config g_config;

namespace {

// Proof the hook is firing and the world can be read, at the right rate: the
// first frame, then every 300th (ten seconds at 30 fps). Read-only; cheap
// enough to leave on. Compare the position with what the game shows and the
// pool counts with what a game of that size should hold.
void Heartbeat(void *) {
	const uint64_t n = game::FrameCount();
	if (n != 1 && n % 300 != 0)
		return;

	game::PlayerState player;
	const bool havePlayer = game::ReadPlayer(&player);

	game::Pool peds, vehicles;
	const bool havePeds     = game::ReadPedPool(&peds);
	const bool haveVehicles = game::ReadVehiclePool(&vehicles);

	game::GameClock clock;
	const bool haveClock = game::ReadClock(&clock);

	char where[96] = "no player";
	if (havePlayer)
		std::snprintf(where, sizeof(where), "player %s at (%.1f, %.1f, %.1f)",
		              player.inVehicle ? "driving" : "on foot", player.position.x,
		              player.position.y, player.position.z);

	Log("frame %llu: %s; peds %d/%d; vehicles %d/%d; game time %u ms, step %.2f",
	    static_cast<unsigned long long>(n), where, havePeds ? peds.CountInUse() : -1,
	    havePeds ? peds.size : -1, haveVehicles ? vehicles.CountInUse() : -1,
	    haveVehicles ? vehicles.size : -1, haveClock ? clock.ms : 0u,
	    haveClock ? static_cast<double>(clock.step) : 0.0);
}

DWORD WINAPI Boot(LPVOID) {
	WallClock::Start();

	g_config.LoadFromFile(Config::IniPath());
	g_config.ApplyEnvOverrides();
	if (g_config.logToFile)
		LogOpen(Config::PathNextToModule("CoopVC.log"));

	Log("CoopVC " COOPVC_VERSION " starting (pid %lu, protocol %u)", GetCurrentProcessId(),
	    static_cast<unsigned>(PROTOCOL_VERSION));

	// Dropping CoopVC.asi into the game folder must not change single player.
	// The mod activates only when the game was started by the launcher, which
	// puts this marker in the child's environment (inherited by CreateProcess
	// children, so it needs no IPC and cannot be set by accident).
	if (const char *marker = std::getenv("COOPVC_LAUNCHED"); !marker || marker[0] == '\0') {
		Log("not started from the CoopVC launcher, so CoopVC is standing down. Nothing was "
		    "hooked and the game is untouched.");
		Log("To play co-op, start the game from coopvc-launcher.exe.");
		return 0;
	}

	if (g_config.logToFile && LogPath() != Config::PathNextToModule("CoopVC.log"))
		Log("log: another instance already holds CoopVC.log, so this one is writing to "
		    "\"%s\".", LogPath().c_str());
	Log("config: server %s:%u, nick \"%s\"%s (from %s)", g_config.host.c_str(), g_config.port,
	    g_config.nick.c_str(), g_config.password.empty() ? "" : ", with a password",
	    Config::IniPath().c_str());

	const game::VerifyResult image = game::VerifyGameImage();
	Log("image: %s", image.detail.c_str());
	if (!image.ok) {
		Log("CoopVC will not load. No hooks were installed and the game is untouched.");
		return 0;
	}

	// The other mods the player installed are still patching the executable
	// while the game starts up. Waiting until it is up means never racing them.
	Log("waiting for the game to finish starting up");
	if (!game::WaitForGameStartup(120000)) {
		Log("the game did not finish starting up in two minutes. CoopVC stands down "
		    "without hooking anything.");
		return 0;
	}

	game::AddFrameCallback(&Heartbeat, nullptr);
	if (!game::InstallFrameHookForGame()) {
		Log("the frame hook did not install. CoopVC stands down; see the lines above.");
		return 0;
	}
	Log("frame hook installed. While a game is being played the player, pools and clock are "
	    "logged every ten seconds.");
	return 0;
}

} // namespace
} // namespace coopvc

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(module);
		if (const HANDLE thread = CreateThread(nullptr, 0, &coopvc::Boot, nullptr, 0, nullptr))
			CloseHandle(thread);
	} else if (reason == DLL_PROCESS_DETACH && reserved == nullptr) {
		// An explicit unload. A process that is exiting (reserved != null) is
		// left alone: by then the game has torn itself down, other threads are
		// gone wherever they stood, and putting the bytes back helps nobody.
		coopvc::game::RemoveFrameHook();
	}
	return TRUE;
}
