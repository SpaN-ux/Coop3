#include "verify.h"

#include "addresses.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace coopvc::game {

namespace {

// Reads `len` bytes at `va` only if the whole span is committed and
// readable. A wrong exe is exactly the case where an address might be
// unmapped, so the check must not fault while deciding whether it is safe.
bool SafeRead(uintptr_t va, void *out, size_t len) {
#ifdef _WIN32
	MEMORY_BASIC_INFORMATION mbi;
	uintptr_t                cursor = va;
	const uintptr_t          end    = va + len;

	while (cursor < end) {
		if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi)) != sizeof(mbi))
			return false;
		if (mbi.State != MEM_COMMIT)
			return false;
		const DWORD prot = mbi.Protect & 0xFF;
		if (prot == PAGE_NOACCESS || (mbi.Protect & PAGE_GUARD))
			return false;
		cursor = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
	}
	std::memcpy(out, reinterpret_cast<const void *>(va), len);
	return true;
#else
	(void)va; (void)out; (void)len;
	return false;
#endif
}

bool MatchesBytes(uintptr_t va, const void *expect, size_t len) {
	uint8_t buf[64];
	if (len > sizeof(buf) || !SafeRead(va, buf, len))
		return false;
	return std::memcmp(buf, expect, len) == 0;
}

bool MatchesString(uintptr_t va, const char *expect) {
	return MatchesBytes(va, expect, std::strlen(expect));
}

} // namespace

VerifyResult VerifyGameImage() {
	VerifyResult r;

#ifndef _WIN32
	r.detail = "not a Windows build";
	return r;
#else
	const HMODULE exe = GetModuleHandleA(nullptr);
	if (!exe) {
		r.detail = "cannot resolve the main module";
		return r;
	}

	const auto base = reinterpret_cast<uintptr_t>(exe);
	if (base != IMAGE_BASE) {
		char buf[200];
		std::snprintf(buf, sizeof(buf),
		              "gta-vc.exe is loaded at 0x%08X, expected 0x%08X. Every address CoopVC "
		              "uses is absolute, so a relocated image cannot be hooked safely.",
		              static_cast<unsigned>(base), static_cast<unsigned>(IMAGE_BASE));
		r.detail = buf;
		return r;
	}

	if (!MatchesString(VA_VERSION_MAGIC, "grandtheftauto3")) {
		r.detail = "this does not look like GTA Vice City: the version magic is missing. "
		           "CoopVC will not load.";
		return r;
	}

	if (!MatchesString(VA_BUILD_MARKER, "DATA\\GTA_VC.DAT")) {
		r.detail = "this is not the gta-vc.exe CoopVC targets (the build marker is absent). "
		           "Expected MD5 " + std::string(IMAGE_MD5) + ", " + std::to_string(IMAGE_SIZE) +
		           " bytes. CoopVC will not load: its addresses belong to that build only.";
		return r;
	}

	r.ok = true;

	// Advisory only: a patched prologue means a mod got here first, which is
	// expected and fine, but worth a line when diagnosing a conflict.
	const bool prologueClean = MatchesBytes(CGame__ValidateVersion, PROLOGUE_VALIDATE_VERSION,
	                                        sizeof(PROLOGUE_VALIDATE_VERSION));

	char versionName[64] = {0};
	const bool haveName  = SafeRead(version_name, versionName, sizeof(versionName) - 1);

	char buf[256];
	std::snprintf(buf, sizeof(buf),
	              "GTA Vice City build confirmed at 0x%08X (data reports \"%s\"%s)",
	              static_cast<unsigned>(base),
	              haveName && versionName[0] ? versionName : "not read yet",
	              prologueClean ? "" : "; ValidateVersion already patched by another mod");
	r.detail = buf;
	return r;
#endif
}

bool ReadGameBytes(uintptr_t va, void *out, size_t len) {
	return SafeRead(va, out, len);
}

bool ReadGameInt(uintptr_t va, int *out) {
	return SafeRead(va, out, sizeof(int));
}

bool GameBytesMatch(uintptr_t va, const void *expect, size_t len) {
	// MatchesBytes caps at its own buffer; compare longer spans in pieces.
	const auto *want = static_cast<const uint8_t *>(expect);
	for (size_t done = 0; done < len;) {
		const size_t n = (len - done) < 64 ? (len - done) : 64;
		if (!MatchesBytes(va + done, want + done, n))
			return false;
		done += n;
	}
	return true;
}

} // namespace coopvc::game
