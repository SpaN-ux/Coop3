// Refuses to run against an executable these addresses weren't derived from.
//
// Every address in addresses.h belongs to one exact build. A detour installed
// at the wrong address does not fail cleanly: it corrupts an unrelated
// function and the game dies somewhere else, minutes later, with a stack that
// points nowhere near CoopVC. So the check runs once, before the first hook,
// and a mismatch stops us loading instead of limping along.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace coopvc::game {

struct VerifyResult {
	bool        ok = false;
	std::string detail;   // human-readable, always populated, goes in the log
};

// Checks the running gta-vc.exe against addresses.h. Read-only and safe to
// call before anything else is initialised.
VerifyResult VerifyGameImage();

// Reads from the running game's memory without faulting: false when any byte
// of the span is not committed and readable. The ordinary way to look at an
// address from addresses.h before trusting it.
bool ReadGameBytes(uintptr_t va, void *out, size_t len);
bool ReadGameInt(uintptr_t va, int *out);

// True when the bytes at `va` are exactly `expect`.
bool GameBytesMatch(uintptr_t va, const void *expect, size_t len);

} // namespace coopvc::game
