// The once-a-frame hook: everything CoopVC does that has to happen in step
// with the game starts here.
//
// How it works: the game's main loop, while a game is being played, calls
// RsEventHandler(0x1A, 1) once per frame (addresses.h has the proof). We put
// a detour on RsEventHandler, let the game's own handling run, and then, when
// the event was that per-frame one and the game really is in the playing
// state, run our callbacks. After the game's work rather than before, so a
// callback sees the world the frame has just finished.
//
// Everything is on the game's own thread: callbacks run inside the game's
// main loop. They must be quick, must not block, and must not touch the
// network - that has a thread of its own, and hands work over through a queue.
#pragma once

#include <cstdint>

namespace coopvc::game {

using FrameCallback = void (*)(void *user);

// True for the one event that means "a frame of the game was just played".
// Pure, so the rule can be tested without a game.
bool IsFrameEvent(int event, int gameState);

// Callbacks run in the order they were added. Add them before installing the
// hook: the list is read on the game's thread and is not locked.
void AddFrameCallback(FrameCallback callback, void *user);
void ClearFrameCallbacks();

// What the hook needs to know about the game it is attached to. The real game
// is InstallFrameHookForGame(); the tests hand in their own function and state
// variable, which is how the real detour gets exercised without the game.
struct FrameHookTarget {
	void               *function  = nullptr;   // int __cdecl (int event, void *param)
	const volatile int *gameState = nullptr;   // the variable IsFrameEvent is given
};

bool InstallFrameHook(const FrameHookTarget &target);

// Installs on gta-vc.exe's RsEventHandler. Refuses unless the bytes there are
// the ones addresses.h describes.
bool InstallFrameHookForGame();

// Blocks until the game has finished starting up (logos, one-time init), or
// the timeout passes. Hooking earlier would mean racing the other mods the
// player installed, which are still patching the same executable.
bool WaitForGameStartup(uint32_t timeoutMs);

void RemoveFrameHook();

// Frames the callbacks have been run for since the hook went in.
uint64_t FrameCount();

} // namespace coopvc::game
