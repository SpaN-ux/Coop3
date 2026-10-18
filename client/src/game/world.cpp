#include "world.h"

#include "addresses.h"
#include "verify.h"

#include <cmath>
#include <cstring>

namespace coopvc::game {

namespace {

MemoryReader g_reader = nullptr;

bool Read(uintptr_t va, void *out, size_t len) {
	return g_reader ? g_reader(va, out, len) : ReadGameBytes(va, out, len);
}

template <class T>
bool ReadT(uintptr_t va, T *out) {
	return Read(va, out, sizeof(T));
}

bool Finite(const Vec3 &v) {
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

void SetMemoryReader(MemoryReader reader) {
	g_reader = reader;
}

// ---- pools ------------------------------------------------------------------

bool Pool::InUse(int index) const {
	if (index < 0 || index >= size || !flags)
		return false;
	uint8_t f = 0;
	if (!ReadT(flags + static_cast<uintptr_t>(index), &f))
		return false;
	return (f & CPool_FREE_BIT_MASK) == 0;
}

uintptr_t Pool::Entry(int index) const {
	if (index < 0 || index >= size)
		return 0;
	return entries + static_cast<uintptr_t>(index) * entrySize;
}

uint32_t Pool::Handle(int index) const {
	if (!InUse(index))
		return 0;
	uint8_t f = 0;
	if (!ReadT(flags + static_cast<uintptr_t>(index), &f))
		return 0;
	return (static_cast<uint32_t>(index) << 8) | f;
}

uintptr_t Pool::GetAt(int32_t handle) const {
	// CPool::GetAt, 0x00451CB0: arithmetic shift, so a negative handle gives a
	// negative index and is refused by the range test the game's own
	// version lacks (it would read before the array).
	const int index = handle >> 8;
	if (index < 0 || index >= size || !flags)
		return 0;
	uint8_t f = 0;
	if (!ReadT(flags + static_cast<uintptr_t>(index), &f))
		return 0;
	if (static_cast<uint32_t>(handle & 0xFF) != f)
		return 0;
	return Entry(index);
}

int Pool::CountInUse() const {
	int n = 0;
	for (int i = 0; i < size; ++i)
		if (InUse(i))
			++n;
	return n;
}

bool ReadPool(uintptr_t poolGlobal, uint32_t entrySize, Pool *out) {
	uint32_t pool = 0;   // a 32-bit pointer, as in the game's own address space
	if (!ReadT(poolGlobal, &pool) || pool == 0)
		return false;

	uint32_t entries = 0, flags = 0;
	int32_t  size = 0;
	if (!ReadT(pool + CPool_OFF_ENTRIES, &entries) || !ReadT(pool + CPool_OFF_FLAGS, &flags) ||
	    !ReadT(pool + CPool_OFF_SIZE, &size))
		return false;

	// A pool that is half-built, freed, or not a pool at all is refused here
	// rather than walked: size is bounded by what the game allocates.
	if (entries == 0 || flags == 0 || size <= 0 || size > 100000)
		return false;

	out->entries   = entries;
	out->flags     = flags;
	out->size      = size;
	out->entrySize = entrySize;
	return true;
}

bool ReadPedPool(Pool *out) {
	return ReadPool(ms_pPedPool, PED_ENTRY_SIZE, out);
}

bool ReadVehiclePool(Pool *out) {
	return ReadPool(ms_pVehiclePool, VEHICLE_ENTRY_SIZE, out);
}

// ---- the player -------------------------------------------------------------

bool PlayerPed(uintptr_t *ped) {
	// FindPlayerPed, 0x004BC120: Players[PlayerInFocus].m_pPed.
	uint8_t focus = 0;
	if (!ReadT(CWorld__PlayerInFocus, &focus))
		return false;

	uint32_t p = 0;
	if (!ReadT(CWorld__Players + static_cast<uintptr_t>(focus) * PLAYER_INFO_SIZE, &p) || p == 0)
		return false;

	*ped = p;
	return true;
}

bool ReadPlayer(PlayerState *out) {
	PlayerState s;
	if (!PlayerPed(&s.ped))
		return false;

	// FindPlayerVehicle, 0x004BC1E0: the car counts only while the in-vehicle
	// byte is set and the pointer is not null.
	uint8_t inVehicle = 0;
	if (!ReadT(s.ped + PED_OFF_IN_VEHICLE, &inVehicle))
		return false;
	if (inVehicle) {
		uint32_t v = 0;
		if (ReadT(s.ped + PED_OFF_MY_VEHICLE, &v) && v != 0) {
			s.vehicle   = v;
			s.inVehicle = true;
		}
	}

	// FindPlayerCoors, 0x004BC240: three floats at entity + 0x34.
	const uintptr_t entity = s.inVehicle ? s.vehicle : s.ped;
	if (!ReadT(entity + ENTITY_OFF_POSITION, &s.position))
		return false;

	// A ped that is being created or destroyed can hold garbage for a frame.
	if (!Finite(s.position))
		return false;

	*out = s;
	return true;
}

bool ReadClock(GameClock *out) {
	GameClock c;
	if (!ReadT(CTimer__TimeInMilliseconds, &c.ms) || !ReadT(CTimer__PreviousTimeMs, &c.prevMs) ||
	    !ReadT(CTimer__TimeStep, &c.step))
		return false;
	*out = c;
	return true;
}

} // namespace coopvc::game
