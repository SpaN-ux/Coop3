// Reading the game's world: the player, the pools, the clock.
//
// Read-only by design. Nothing here writes to the game; spawning and moving
// things is a later file, and it gets its own proofs. Every read goes through
// a reader that cannot fault: a wrong address yields `false`, never a crash.
//
// The offsets and layouts are addresses.h's, and tools/vcexe/check.py proves
// them against the exe. This code assumes they are right; tools/worldtest
// proves it uses them consistently, against a world built from the same
// numbers.
#pragma once

#include <cstddef>
#include <cstdint>

namespace coopvc::game {

struct Vec3 {
	float x = 0, y = 0, z = 0;
};

// How memory is read. The default is the real game's (verify.h's ReadGameBytes);
// tests install their own so the logic runs against a world they built.
using MemoryReader = bool (*)(uintptr_t va, void *out, size_t len);
void SetMemoryReader(MemoryReader reader);   // null restores the default

// A snapshot of a pool's header. Cheap to take; take a fresh one each frame
// rather than keeping one, because the pool is the game's and it may be rebuilt
// (a new game builds new pools).
struct Pool {
	uintptr_t entries   = 0;
	uintptr_t flags     = 0;
	int32_t   size      = 0;
	uint32_t  entrySize = 0;

	// Slot `index` holds a live object: in range, and the free bit is clear.
	bool     InUse(int index) const;
	uintptr_t Entry(int index) const;     // address of slot `index`, whether or not it is in use
	uint32_t Handle(int index) const;     // (index << 8) | flag byte; 0 for a slot that is not in use
	// CPool::GetAt: the object for a handle, or 0 when the handle is stale or free.
	uintptr_t GetAt(int32_t handle) const;
	int      CountInUse() const;
};

// `poolGlobal` is one of addresses.h's ms_p*Pool: the address of the variable
// that holds the CPool pointer.
bool ReadPool(uintptr_t poolGlobal, uint32_t entrySize, Pool *out);
bool ReadPedPool(Pool *out);
bool ReadVehiclePool(Pool *out);

// The local player. All of these are false until a game is running, and for
// as long as the player has no ped.
bool PlayerPed(uintptr_t *ped);

struct PlayerState {
	uintptr_t ped      = 0;
	uintptr_t vehicle  = 0;     // the car under the player, when in one
	bool      inVehicle = false;
	Vec3      position;         // the vehicle's when in one, as FindPlayerCoors says
};
bool ReadPlayer(PlayerState *out);

// The game clock. `ms` advances only while the game is unpaused; `step` is the
// frame's time step (0 while paused).
struct GameClock {
	uint32_t ms     = 0;
	uint32_t prevMs = 0;
	float    step   = 0;
};
bool ReadClock(GameClock *out);

} // namespace coopvc::game
