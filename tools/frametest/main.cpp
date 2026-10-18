// Tests the once-a-frame hook with a real detour and no game.
//   xmake build frametest && xmake run frametest
//
// A stand-in for gta-vc.exe's RsEventHandler (same signature, same calling
// convention) is hooked with the same code that hooks the real one, and a
// variable stands in for the game-state global. What cannot be tested here is
// that the addresses in addresses.h are the right ones; that is what
// tools/vcexe/check.py is for.

#include "game/addresses.h"
#include "game/frame.h"
#include "hook/hook.h"

#include <cstdio>
#include <stdexcept>
#include <vector>

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

using namespace coopvc;
using namespace coopvc::game;

namespace {

int g_failures = 0;

void Check(bool cond, const char *what) {
	std::printf("  [%s] %s\n", cond ? "ok" : "FAIL", what);
	if (!cond)
		++g_failures;
}

volatile int g_sink = 0;
volatile int g_state = 0;

// Same shape as the real one: two cdecl arguments, a result, and a body big
// enough that the first five bytes are whole instructions.
NOINLINE int __cdecl FakeRsEventHandler(int event, void *param) {
	g_sink += event;
	g_sink ^= (param ? 1 : 0);
	return event * 3 + (param ? 7 : 0);
}

using Fn = int(__cdecl *)(int, void *);
volatile Fn g_call = &FakeRsEventHandler;   // through a pointer: the optimiser cannot inline it

std::vector<int> g_order;
int              g_a = 0, g_b = 0;

void CallbackA(void *user) { g_order.push_back(1); ++*static_cast<int *>(user); }
void CallbackB(void *user) { g_order.push_back(2); ++*static_cast<int *>(user); }
void Thrower(void *) { throw std::runtime_error("a callback going wrong"); }

void TestTheRule() {
	std::printf("\nwhat counts as a frame\n");
	Check(IsFrameEvent(EVENT_IDLE, GS_PLAYING_GAME), "the idle event while playing is a frame");
	Check(!IsFrameEvent(EVENT_IDLE, GS_INIT_FRONTEND), "the idle event in the menu is not");
	Check(!IsFrameEvent(EVENT_IDLE, GS_INIT_GAME), "nor while a game is still being set up");
	Check(!IsFrameEvent(EVENT_QUITAPP, GS_PLAYING_GAME), "another event while playing is not");
	Check(!IsFrameEvent(0, 0), "and nothing at all is not");
	Check(EVENT_IDLE == 0x1A && GS_PLAYING_GAME == 9, "the constants are the ones in the disassembly");
}

void TestTheHook() {
	std::printf("\nthe hook\n");
	Check(HookInit(), "the hook engine starts");

	g_state = GS_PLAYING_GAME;
	const int plain = g_call(GS_PLAYING_GAME, nullptr);
	Check(FrameCount() == 0, "no hook yet, no frames");

	ClearFrameCallbacks();
	g_a = g_b = 0;
	g_order.clear();
	AddFrameCallback(&CallbackA, &g_a);
	AddFrameCallback(&CallbackB, &g_b);

	FrameHookTarget t;
	t.function  = reinterpret_cast<void *>(&FakeRsEventHandler);
	t.gameState = &g_state;
	Check(InstallFrameHook(t), "installs");
	Check(!InstallFrameHook(t), "and refuses to install twice");

	const int hooked = g_call(GS_PLAYING_GAME, nullptr);
	Check(hooked == plain, "the game's own result comes back unchanged");
	Check(g_call(5, reinterpret_cast<void *>(1)) == 5 * 3 + 7, "and with a parameter, too");

	g_state = GS_PLAYING_GAME;
	const uint64_t before = FrameCount();
	g_call(EVENT_IDLE, reinterpret_cast<void *>(1));
	Check(FrameCount() == before + 1, "the idle event while playing is counted");
	Check(g_a == 1 && g_b == 1, "and runs both callbacks");
	Check(g_order.size() == 2 && g_order[0] == 1 && g_order[1] == 2, "in the order they were added");

	g_state = GS_INIT_FRONTEND;
	g_call(EVENT_IDLE, reinterpret_cast<void *>(1));
	Check(FrameCount() == before + 1 && g_a == 1, "the idle event in the menu is not a frame");

	g_state = GS_PLAYING_GAME;
	g_call(EVENT_QUITAPP, nullptr);
	g_call(0, nullptr);
	Check(FrameCount() == before + 1, "other events while playing are not");

	for (int i = 0; i < 1000; ++i)
		g_call(EVENT_IDLE, nullptr);
	Check(FrameCount() == before + 1001, "a thousand frames are a thousand");
	Check(g_a == 1001, "and each ran its callbacks");
}

void TestACallbackThatThrows() {
	std::printf("\na callback that throws\n");
	ClearFrameCallbacks();
	g_a = 0;
	AddFrameCallback(&Thrower, nullptr);
	AddFrameCallback(&CallbackA, &g_a);

	g_state = GS_PLAYING_GAME;
	bool escaped = false;
	try {
		g_call(EVENT_IDLE, nullptr);
		g_call(EVENT_IDLE, nullptr);
	} catch (...) {
		escaped = true;
	}
	Check(!escaped, "does not reach the game");
	Check(g_a == 2, "and the callbacks after it still run, frame after frame");
}

void TestRemoving() {
	std::printf("\nremoving it\n");
	const uint64_t before = FrameCount();
	RemoveFrameHook();
	g_state = GS_PLAYING_GAME;
	const int r = g_call(EVENT_IDLE, reinterpret_cast<void *>(1));
	Check(r == EVENT_IDLE * 3 + 7, "the original behaves as it did");
	Check(FrameCount() == before, "and nothing is counted any more");

	FrameHookTarget none;
	Check(!InstallFrameHook(none), "no target, no hook");

	FrameHookTarget noState;
	noState.function = reinterpret_cast<void *>(&FakeRsEventHandler);
	Check(!InstallFrameHook(noState), "no game state, no hook");
	RemoveFrameHook();
	HookShutdown();
}

} // namespace

int main() {
	std::printf("frametest\n");
	TestTheRule();
	TestTheHook();
	TestACallbackThatThrows();
	TestRemoving();

	if (g_failures) {
		std::printf("\nFAIL (%d failures)\n", g_failures);
		return 1;
	}
	std::printf("\nPASS (0 failures)\n");
	return 0;
}
