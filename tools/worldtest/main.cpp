// Tests the world-reading code against a world built here, from addresses.h's
// own numbers. No game.
//   xmake build worldtest && xmake run worldtest
//
// What this proves: the logic uses the layouts consistently - the pool
// handle arithmetic matches the game's, a stale handle is refused, a null or
// garbage pointer anywhere in a chain gives `false` and not a crash. What it
// cannot prove is that the layouts are right; tools/vcexe/check.py does that
// against the exe.

#include "game/addresses.h"
#include "game/world.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

using namespace coopvc::game;

namespace {

int g_failures = 0;

void Check(bool cond, const char *what) {
	std::printf("  [%s] %s\n", cond ? "ok" : "FAIL", what);
	if (!cond)
		++g_failures;
}

// ---- a world ----------------------------------------------------------------

std::map<uintptr_t, std::vector<uint8_t>> g_mem;

void Map(uintptr_t base, size_t size) {
	g_mem[base].assign(size, 0);
}

uint8_t *At(uintptr_t va) {
	for (auto &r : g_mem)
		if (va >= r.first && va < r.first + r.second.size())
			return &r.second[va - r.first];
	return nullptr;
}

template <class T>
void Put(uintptr_t va, T v) {
	uint8_t *p = At(va);
	if (p)
		std::memcpy(p, &v, sizeof(T));
}

bool FakeRead(uintptr_t va, void *out, size_t len) {
	for (auto &r : g_mem) {
		if (va >= r.first && va + len <= r.first + r.second.size()) {
			std::memcpy(out, &r.second[va - r.first], len);
			return true;
		}
	}
	return false;   // unmapped: exactly what the real reader says
}

constexpr uintptr_t PED_A    = 0x10000000;
constexpr uintptr_t PED_B    = 0x10010000;
constexpr uintptr_t CAR      = 0x10020000;
constexpr uintptr_t POOL_PED = 0x30000000;
constexpr uintptr_t PED_ENT  = 0x31000000;
constexpr uintptr_t PED_FLG  = 0x32000000;
constexpr uintptr_t POOL_VEH = 0x30001000;
constexpr uintptr_t VEH_ENT  = 0x33000000;
constexpr uintptr_t VEH_FLG  = 0x34000000;

void BuildPool(uintptr_t global, uintptr_t header, uintptr_t entries, uintptr_t flags, int size,
               uint32_t entrySize) {
	Map(header, 0x10);
	Map(entries, static_cast<size_t>(size) * entrySize);
	Map(flags, static_cast<size_t>(size));
	Put<uint32_t>(global, static_cast<uint32_t>(header));
	Put<uint32_t>(header + CPool_OFF_ENTRIES, static_cast<uint32_t>(entries));
	Put<uint32_t>(header + CPool_OFF_FLAGS, static_cast<uint32_t>(flags));
	Put<int32_t>(header + CPool_OFF_SIZE, size);
	Put<int32_t>(header + 0x0C, -1);
	for (int i = 0; i < size; ++i)
		Put<uint8_t>(flags + i, 0x80);   // every slot starts free, as the constructor leaves it
}

void BuildWorld() {
	g_mem.clear();
	Map(CWorld__PlayerInFocus, 1);
	Map(CWorld__Players, 2 * PLAYER_INFO_SIZE);
	Map(CTimer__TimeInMilliseconds, 4);
	Map(CTimer__PreviousTimeMs, 4);
	Map(CTimer__TimeStep, 4);
	Map(ms_pPedPool, 4);
	Map(ms_pVehiclePool, 4);

	Map(PED_A, 0x800); Map(PED_B, 0x800); Map(CAR, 0x800);
	Put<uint8_t>(CWorld__PlayerInFocus, 0);
	Put<uint32_t>(CWorld__Players, static_cast<uint32_t>(PED_A));
	Put<uint32_t>(CWorld__Players + PLAYER_INFO_SIZE, static_cast<uint32_t>(PED_B));

	Put<float>(PED_A + ENTITY_OFF_POSITION + 0, 100.5f);
	Put<float>(PED_A + ENTITY_OFF_POSITION + 4, -200.25f);
	Put<float>(PED_A + ENTITY_OFF_POSITION + 8, 11.0f);
	Put<float>(PED_B + ENTITY_OFF_POSITION + 0, 7.0f);
	Put<float>(CAR + ENTITY_OFF_POSITION + 0, 300.0f);
	Put<float>(CAR + ENTITY_OFF_POSITION + 4, 400.0f);
	Put<float>(CAR + ENTITY_OFF_POSITION + 8, 5.0f);

	BuildPool(ms_pPedPool, POOL_PED, PED_ENT, PED_FLG, PED_POOL_SIZE, PED_ENTRY_SIZE);
	BuildPool(ms_pVehiclePool, POOL_VEH, VEH_ENT, VEH_FLG, VEHICLE_POOL_SIZE, VEHICLE_ENTRY_SIZE);

	Put<uint32_t>(CTimer__TimeInMilliseconds, 123456);
	Put<uint32_t>(CTimer__PreviousTimeMs, 123440);
	Put<float>(CTimer__TimeStep, 0.8f);
}

void TestNoGame() {
	std::printf("\nno game running\n");
	g_mem.clear();
	PlayerState p;
	Pool pool;
	GameClock c;
	Check(!ReadPlayer(&p), "no player");
	Check(!ReadPedPool(&pool), "no ped pool");
	Check(!ReadVehiclePool(&pool), "no vehicle pool");
	Check(!ReadClock(&c), "no clock");
}

void TestThePlayer() {
	std::printf("\nthe player\n");
	BuildWorld();
	PlayerState p;
	Check(ReadPlayer(&p), "is found");
	Check(p.ped == PED_A && !p.inVehicle && p.vehicle == 0, "on foot");
	Check(p.position.x == 100.5f && p.position.y == -200.25f && p.position.z == 11.0f,
	      "at the position three floats at +0x34 hold");

	Put<uint8_t>(PED_A + PED_OFF_IN_VEHICLE, 1);
	Put<uint32_t>(PED_A + PED_OFF_MY_VEHICLE, static_cast<uint32_t>(CAR));
	Check(ReadPlayer(&p) && p.inVehicle && p.vehicle == CAR, "in a car");
	Check(p.position.x == 300.0f && p.position.y == 400.0f && p.position.z == 5.0f,
	      "and then the car's position, as FindPlayerCoors says");

	Put<uint32_t>(PED_A + PED_OFF_MY_VEHICLE, 0);
	Check(ReadPlayer(&p) && !p.inVehicle && p.position.x == 100.5f,
	      "the flag set with no car behind it falls back to the ped, as the game does");

	Put<uint8_t>(PED_A + PED_OFF_IN_VEHICLE, 0);
	Put<uint8_t>(CWorld__PlayerInFocus, 1);
	Check(ReadPlayer(&p) && p.ped == PED_B && p.position.x == 7.0f,
	      "PlayerInFocus picks the record, 0x170 bytes apart");

	Put<uint8_t>(CWorld__PlayerInFocus, 0);
	Put<uint32_t>(CWorld__Players, 0);
	Check(!ReadPlayer(&p), "a player with no ped is not found");

	Put<uint32_t>(CWorld__Players, static_cast<uint32_t>(PED_A));
	Put<float>(PED_A + ENTITY_OFF_POSITION, std::nanf(""));
	Check(!ReadPlayer(&p), "a position that is not a number is refused");

	Put<float>(PED_A + ENTITY_OFF_POSITION, 1.0f);
	Put<uint32_t>(CWorld__Players, 0x7FFF0000);   // a pointer into nowhere
	Check(!ReadPlayer(&p), "a pointer into unmapped memory gives false, not a crash");
}

void TestPools() {
	std::printf("\npools\n");
	BuildWorld();
	Pool ped;
	Check(ReadPedPool(&ped), "the ped pool reads");
	Check(ped.size == 140 && ped.entrySize == 0x6D8, "140 slots of 0x6D8 bytes");
	Check(ped.CountInUse() == 0, "a new pool is all free");
	Check(!ped.InUse(0) && !ped.InUse(-1) && !ped.InUse(140), "and out of range is not in use");

	Put<uint8_t>(PED_FLG + 5, 0x23);
	Put<uint8_t>(PED_FLG + 6, 0x01);
	Put<uint8_t>(PED_FLG + 139, 0x7F);
	Check(ped.CountInUse() == 3, "three slots with the free bit clear are three peds");
	Check(ped.InUse(5) && ped.InUse(139) && !ped.InUse(7), "the right ones");

	Check(ped.Entry(5) == PED_ENT + 5 * 0x6D8, "slot 5 is at entries + 5 * 0x6D8");
	Check(ped.Entry(140) == 0 && ped.Entry(-1) == 0, "no address past the ends");

	const uint32_t h = ped.Handle(5);
	Check(h == ((5u << 8) | 0x23), "its handle is (index << 8) | flag byte");
	Check(ped.GetAt(static_cast<int32_t>(h)) == ped.Entry(5), "and GetAt gives the slot back");
	Check(ped.Handle(7) == 0, "a free slot has no handle");

	bool roundTrip = true;
	for (int i = 0; i < ped.size; ++i)
		if (ped.InUse(i) && ped.GetAt(static_cast<int32_t>(ped.Handle(i))) != ped.Entry(i))
			roundTrip = false;
	Check(roundTrip, "every live slot's handle finds that slot");

	Put<uint8_t>(PED_FLG + 5, 0x24);
	Check(ped.GetAt(static_cast<int32_t>(h)) == 0, "a handle goes stale when its slot is reused");
	Put<uint8_t>(PED_FLG + 5, 0x80);
	Check(ped.GetAt(static_cast<int32_t>(h)) == 0, "and when its slot is freed");
	Check(ped.GetAt(-1) == 0 && ped.GetAt(-256) == 0, "a negative handle is refused");
	Check(ped.GetAt(140 << 8) == 0, "so is an index past the end");

	Pool veh;
	Check(ReadVehiclePool(&veh) && veh.size == 110 && veh.entrySize == 0x5DC, "vehicles: 110 of 0x5DC");
	Put<uint8_t>(VEH_FLG + 3, 0x05);
	Check(veh.Entry(3) == VEH_ENT + 3 * 0x5DC && veh.GetAt((3 << 8) | 0x05) == veh.Entry(3),
	      "with their own stride");

	// Pools the game has not built yet, or has freed, or that are not pools.
	Put<uint32_t>(ms_pPedPool, 0);
	Check(!ReadPedPool(&ped), "a null pool pointer is refused");
	Put<uint32_t>(ms_pPedPool, static_cast<uint32_t>(POOL_PED));
	Put<int32_t>(POOL_PED + CPool_OFF_SIZE, 0);
	Check(!ReadPedPool(&ped), "a pool of no slots is refused");
	Put<int32_t>(POOL_PED + CPool_OFF_SIZE, 5000000);
	Check(!ReadPedPool(&ped), "and so is an absurd one");
	Put<int32_t>(POOL_PED + CPool_OFF_SIZE, 140);
	Put<uint32_t>(POOL_PED + CPool_OFF_FLAGS, 0);
	Check(!ReadPedPool(&ped), "a pool with no flag array is refused");
	Put<uint32_t>(POOL_PED + CPool_OFF_FLAGS, 0x7FFF0000);
	Check(ReadPedPool(&ped) && !ped.InUse(0) && ped.CountInUse() == 0 && ped.GetAt(0) == 0,
	      "flags pointing into nowhere read as nothing in use, without a fault");
}

void TestTheClock() {
	std::printf("\nthe clock\n");
	BuildWorld();
	GameClock c;
	Check(ReadClock(&c), "reads");
	Check(c.ms == 123456 && c.prevMs == 123440 && std::fabs(c.step - 0.8f) < 1e-6f,
	      "the time, the time before, the step");
}

} // namespace

int main() {
	std::printf("worldtest\n");
	SetMemoryReader(&FakeRead);

	TestNoGame();
	TestThePlayer();
	TestPools();
	TestTheClock();

	SetMemoryReader(nullptr);
	if (g_failures) {
		std::printf("\nFAIL (%d failures)\n", g_failures);
		return 1;
	}
	std::printf("\nPASS (0 failures)\n");
	return 0;
}
