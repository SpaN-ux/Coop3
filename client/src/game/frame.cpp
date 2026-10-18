#include "frame.h"

#include "addresses.h"
#include "verify.h"

#include "hook/hook.h"
#include "log.h"

#include <atomic>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace coopvc::game {

namespace {

using RsEventHandlerFn = int(__cdecl *)(int event, void *param);

struct Callback {
	FrameCallback fn;
	void         *user;
};

std::vector<Callback>  g_callbacks;
Detour                 g_detour;
const volatile int    *g_state  = nullptr;
std::atomic<uint64_t>  g_frames{0};
std::atomic<bool>      g_warned{false};

// Everything that happens after the game's own handling. A callback that
// throws would unwind into the game's main loop, which has no idea what to do
// with it, so none gets out: it is logged once and the frame carries on.
void RunCallbacks() {
	for (const Callback &c : g_callbacks) {
		try {
			c.fn(c.user);
		} catch (...) {
			if (!g_warned.exchange(true))
				Log("frame: a callback threw. Caught, and the game carries on. "
				    "Further throws are not logged.");
		}
	}
}

int __cdecl DetouredRsEventHandler(int event, void *param) {
	const auto original = g_detour.Original<RsEventHandlerFn>();
	const int  result   = original(event, param);

	if (g_state && IsFrameEvent(event, *g_state)) {
		g_frames.fetch_add(1, std::memory_order_relaxed);
		RunCallbacks();
	}
	return result;
}

} // namespace

bool IsFrameEvent(int event, int gameState) {
	return event == EVENT_IDLE && gameState == GS_PLAYING_GAME;
}

void AddFrameCallback(FrameCallback callback, void *user) {
	if (callback)
		g_callbacks.push_back({callback, user});
}

void ClearFrameCallbacks() {
	g_callbacks.clear();
}

bool InstallFrameHook(const FrameHookTarget &target) {
	if (g_detour.IsInstalled()) {
		Log("frame: the hook is already installed");
		return false;
	}
	if (!target.function || !target.gameState) {
		Log("frame: no target function or game state to attach to");
		return false;
	}
	if (!HookInit()) {
		Log("frame: the hook engine did not start");
		return false;
	}

	g_state = target.gameState;
	g_frames.store(0);
	g_warned.store(false);

	if (!g_detour.Install("RsEventHandler", target.function,
	                      reinterpret_cast<void *>(&DetouredRsEventHandler))) {
		for (const HookFailure &f : HookFailures())
			Log("frame: hook failure - %s: %s", f.name.c_str(), f.reason.c_str());
		g_state = nullptr;
		HookShutdown();
		return false;
	}
	return true;
}

bool InstallFrameHookForGame() {
	// The bytes are the proof the address is the function the proof says it
	// is. A mismatch is not "another mod hooked it first" - that would still
	// start with its own jump - it means this is not the build addresses.h is
	// for, and a detour here would corrupt whatever is really at that address.
	if (!GameBytesMatch(RsEventHandler, PROLOGUE_RS_EVENT_HANDLER,
	                    sizeof(PROLOGUE_RS_EVENT_HANDLER))) {
		Log("frame: RsEventHandler (0x%08X) does not start with the expected bytes. Not "
		    "hooking. Either another mod got there first or this is not the supported build.",
		    static_cast<unsigned>(RsEventHandler));
		return false;
	}

	FrameHookTarget target;
	target.function  = reinterpret_cast<void *>(RsEventHandler);
	target.gameState = reinterpret_cast<const volatile int *>(game_state);
	return InstallFrameHook(target);
}

bool WaitForGameStartup(uint32_t timeoutMs) {
#ifdef _WIN32
	const DWORD start = GetTickCount();
	for (;;) {
		int state = -1;
		if (ReadGameInt(game_state, &state) && state >= GS_INIT_FRONTEND)
			return true;
		if (GetTickCount() - start >= timeoutMs)
			return false;
		Sleep(50);
	}
#else
	(void)timeoutMs;
	return false;
#endif
}

void RemoveFrameHook() {
	if (!g_detour.IsInstalled())
		return;
	g_detour.Remove();
	g_state = nullptr;
	HookShutdown();
}

uint64_t FrameCount() {
	return g_frames.load(std::memory_order_relaxed);
}

} // namespace coopvc::game
