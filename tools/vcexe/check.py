#!/usr/bin/env python3
"""Checks client/src/game/addresses.h against a real gta-vc.exe.

    python tools/vcexe/check.py path/to/gta-vc.exe

Static only: the exe is read as a file and never run. Every address that
addresses.h claims is proven by something visible in the bytes is asserted
here, so a typo, or an exe that is not the supported build, fails loudly
instead of crashing a game.

Exit status 0 when everything holds, 1 otherwise. Needs nothing but Python 3.
"""
import hashlib
import os
import re
import struct
import sys

HEADER = os.path.join(os.path.dirname(__file__), "..", "..", "client", "src", "game", "addresses.h")

failures = 0


def check(cond, what):
    global failures
    print("  [%s] %s" % ("ok" if cond else "FAIL", what))
    if not cond:
        failures += 1


def parse_header(path):
    text = open(path, encoding="utf-8").read()
    consts = {}
    for m in re.finditer(r"constexpr\s+(?:uintptr_t|uint32_t|int)\s+(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;", text):
        consts[m.group(1)] = int(m.group(2), 0)
    md5 = re.search(r'IMAGE_MD5\[\]\s*=\s*"([0-9A-Fa-f]+)"', text)
    arrays = {}
    for m in re.finditer(r"constexpr\s+uint8_t\s+(\w+)\[\]\s*=\s*\{([^}]*)\}", text):
        arrays[m.group(1)] = bytes(int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", m.group(2)))
    return consts, (md5.group(1).upper() if md5 else None), arrays


class Image:
    def __init__(self, data):
        self.d = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        nsec = struct.unpack_from("<H", data, pe + 6)[0]
        so = struct.unpack_from("<H", data, pe + 20)[0]
        opt = pe + 24
        self.base = struct.unpack_from("<I", data, opt + 28)[0]
        self.secs = []
        for i in range(nsec):
            o = opt + so + i * 40
            name = data[o:o + 8].rstrip(b"\0").decode()
            vs, va, rs, rp = struct.unpack_from("<IIII", data, o + 8)
            self.secs.append((name, self.base + va, vs, rp, rs))

    def section(self, va):
        for name, b, vs, rp, rs in self.secs:
            if b <= va < b + max(vs, rs):
                return name
        return None

    def read(self, va, n):
        """File bytes at a VA, or None where the VA has no bytes in the file (.bss)."""
        for name, b, vs, rp, rs in self.secs:
            if b <= va < b + vs and va - b + n <= rs:
                return self.d[rp + va - b: rp + va - b + n]
        return None

    def u32(self, va):
        r = self.read(va, 4)
        return struct.unpack("<I", r)[0] if r else None


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2

    data = open(sys.argv[1], "rb").read()
    consts, want_md5, arrays = parse_header(HEADER)
    img = Image(data)

    print("the file")
    check(len(data) == consts["IMAGE_SIZE"], "size is %d" % consts["IMAGE_SIZE"])
    check(hashlib.md5(data).hexdigest().upper() == want_md5, "MD5 is %s" % want_md5)
    check(img.base == consts["IMAGE_BASE"], "image base is 0x%08X" % consts["IMAGE_BASE"])

    if failures:
        # Everything below reads addresses from a build they were not derived
        # from, so a pile of further failures would only be noise.
        print("\nFAIL: this is not the supported gta-vc.exe, so nothing further was checked")
        return 1

    print("\nevery address lies inside the image")
    for name, va in sorted(consts.items()):
        if va >= img.base and name not in ("IMAGE_BASE", "IMAGE_SIZE"):
            check(img.section(va) is not None, "%s 0x%08X is in %s" % (name, va, img.section(va)))

    print("\nstrings")
    check(img.read(consts["VA_VERSION_MAGIC"], 15) == b"grandtheftauto3", "version magic at VA_VERSION_MAGIC")
    check(img.read(consts["VA_BUILD_MARKER"], 15) == b"DATA\\GTA_VC.DAT", "build marker at VA_BUILD_MARKER")

    print("\nfunction prologues")
    pro = {"PROLOGUE_VALIDATE_VERSION": "CGame__ValidateVersion", "PROLOGUE_RS_EVENT_HANDLER": "RsEventHandler"}
    for arr, fn in pro.items():
        va = consts[fn]
        check(arr in arrays and img.read(va, len(arrays[arr])) == arrays[arr], "%s starts with %s" % (fn, arr))

    print("\nCGame::ValidateVersion")
    # push 0xF / push magic / push eax / call memcmp-ish
    check(img.read(0x4A5381, 7) == b"\x6A\x0F\x68" + struct.pack("<I", consts["VA_VERSION_MAGIC"])[:4],
          "compares 15 bytes against the magic at 0x004A5381")
    check(img.read(0x4A539C, 5) == b"\x68" + struct.pack("<I", consts["version_name"]),
          "copies into version_name at 0x004A539C")
    call = img.read(consts["CGame__ValidateVersion__Caller"], 5)
    rel = struct.unpack("<i", call[1:])[0] if call and call[0] == 0xE8 else None
    check(rel is not None and (consts["CGame__ValidateVersion__Caller"] + 5 + rel) & 0xFFFFFFFF ==
          consts["CGame__ValidateVersion"], "the caller's call lands on ValidateVersion")
    check(img.section(consts["version_name"]) == ".bss", "version_name is in .bss")

    print("\nthe main loop")
    table = 0x6D68E4
    entries = [img.u32(table + 4 * i) for i in range(10)]
    check(all(e is not None and img.section(e) == ".text" for e in entries), "the state jump table has ten code entries")
    check(entries[9] == 0x60042C, "entry 9 (playing) is 0x0060042C")
    check(img.read(0x5FFF8D, 6) == b"\x8B\x15" + struct.pack("<I", consts["game_state"]),
          "the loop reads game_state to index it")
    check(img.read(0x600590, 10) == b"\xC7\x05" + struct.pack("<I", consts["game_state"]) + struct.pack("<I", 6),
          "state 6 (menu) is stored at 0x00600590")
    check(img.read(0x6005A0, 10) == b"\xC7\x05" + struct.pack("<I", consts["game_state"]) + struct.pack("<I", 8),
          "state 8 (game init) is stored at 0x006005A0")

    print("\nthe per-frame call")
    site = 0x60049C
    check(img.read(site, 4) == b"\x6A\x01\x6A" + bytes([consts["EVENT_IDLE"]]), "playing state pushes 1 and the idle event")
    call = img.read(site + 6, 5)
    rel = struct.unpack("<i", call[1:])[0] if call and call[0] == 0xE8 else None
    check(rel is not None and (site + 6 + 5 + rel) & 0xFFFFFFFF == consts["RsEventHandler"],
          "and calls RsEventHandler")
    check(entries[9] <= site < entries[9] + 0x80, "that call is inside the playing state's code")

    print("\nRsEventHandler")
    check(img.read(0x602EEC, 5)[0] == 0xE8 and
          (0x602EEC + 5 + struct.unpack("<i", img.read(0x602EEC, 5)[1:])[0]) & 0xFFFFFFFF == consts["AppEventHandler"],
          "calls AppEventHandler first")
    check(img.read(0x602EF1, 3) == b"\x83\xFF" + bytes([consts["EVENT_QUITAPP"]]), "compares the event with the quit event")
    check(img.read(0x602EF8, 10) == b"\xC7\x05" + struct.pack("<I", consts["quit_flag"]) + struct.pack("<I", 1),
          "and sets quit_flag")

    def le(v):
        return struct.pack("<I", v)

    print("\nthe pools (CPools::Initialise)")
    pools = [  # global, size, name VA, name
        (0xA0FD38, 50000, 0x68F8E4, b"PtrNode"),
        (0xA0D8BC, 3200, 0x68F8EC, b"EntryInfoNode"),
        (consts["ms_pPedPool"], consts["PED_POOL_SIZE"], 0x68F8FC, b"Peds"),
        (consts["ms_pVehiclePool"], consts["VEHICLE_POOL_SIZE"], 0x68F904, b"Vehicles"),
        (0x97F240, 7000, 0x68F910, b"Buildings"),
        (0x978834, 1, 0x68F91C, b"Treadables"),
        (0x94DBE0, 460, 0x68F928, b"Objects"),
        (0x97F25C, 2340, 0x68F930, b"Dummys"),
        (0x978778, 192, 0x68F938, b"AudioScriptObj"),
        (0x9753C0, 4400, 0x68F948, b"ColModel"),
    ]
    init = img.read(0x4C0270, 0x14F)
    for glob, size, nameva, name in pools:
        # push name / push size, then the ctor call. Sizes up to 127 are a two-byte `push imm8`.
        call = b"\x68" + le(nameva) + (b"\x6A" + bytes([size]) if size < 128 else b"\x68" + le(size))
        at = init.find(call)
        # the store of the finished pool follows the NEXT `push 0x10`; the last one has no next
        store = b"\xA3" + le(glob)
        check(at >= 0 and init.find(store, at) > at, "%-16s size %5d, stored in 0x%08X" % (name.decode(), size, glob))
        check(img.read(nameva, len(name) + 1) == name + b"\0", "  and its name string is at 0x%08X" % nameva)

    print("\nthe pool layout (constructor at 0x004C09B0)")
    check(img.read(0x4C09C1, 6) == b"\x69\xC0" + le(consts["PED_ENTRY_SIZE"]), "a ped entry is 0x6D8 bytes")
    check(img.read(0x4C0B21, 6) == b"\x69\xC0" + le(consts["VEHICLE_ENTRY_SIZE"]), "a vehicle entry is 0x5DC bytes")
    ctor = img.read(0x4C09CF, 0x13)
    check(ctor[:2] == b"\x89\x06" and ctor[7:10] == b"\x89\x46\x04" and ctor[10:13] == b"\x89\x5E\x08",
          "+0 entries, +4 flags, +8 size are written in that order")
    check(img.read(0x4C09DE, 4) == b"\x83\x4E\x0C\xFF" or img.read(0x4C09DE, 4) == b"\x83\x4e\x0c\xff",
          "+0xC allocPtr starts at -1")
    check(img.read(0x4C09FA, 4) == b"\x80\x0C\x01\x80", "every slot is created with the 0x80 free bit")

    print("\nhandles (CPool::GetAt and its reverse)")
    getat = img.read(consts["CPool_Ped_GetAt"], 0x3A)
    check(getat[0x0A:0x0D] == b"\xC1\xF8\x08", "GetAt shifts the handle right by 8 for the index")
    check(getat[0x16:0x1C] == b"\x81\xE2\xFF\x00\x00\x00", "and masks the low byte of the handle")
    check(getat[0x1C:0x1E] == b"\x39\xCA", "compares it with the slot's flag byte")
    check(getat[0x20:0x26] == b"\x69\xC0" + le(consts["PED_ENTRY_SIZE"]), "multiplies by the ped entry size")
    check(getat[0x2A:0x2D] == b"\xC2\x04\x00", "one stack argument (ret 4)")
    veh = img.read(consts["CPool_Vehicle_GetAt"], 0x34)
    check(veh[0x0A:0x0D] == b"\xC1\xF8\x08" and veh[0x2A:0x2D] == b"\xC2\x04\x00" and
          veh[0x20:0x26] == b"\x69\xC0" + le(consts["VEHICLE_ENTRY_SIZE"]),
          "the vehicle pool's GetAt is the same code with 0x5DC")
    rev = img.read(consts["CPool_Ped_GetHandle"], 0x28)
    check(rev[4:9] == b"\xB8\x69\x25\xA0\x95", "the reverse divides by 0x6D8 (the 0x95A02569 multiply)")
    check(rev[0x23:0x26] == b"\xC1\xE0\x08", "and builds the handle as index << 8 ...")
    check(rev[0x26:0x28] == b"\x01\xC8", "... plus the flag byte")

    print("\nthe player (FindPlayerPed and its neighbours)")
    fp = b"\x0F\xB6\x05" + le(consts["CWorld__PlayerInFocus"]) + b"\x6B\xC0\x2E\x8B\x04\xC5" + \
         le(consts["CWorld__Players"]) + b"\xC3"
    check(consts["PLAYER_INFO_SIZE"] == 0x2E * 8, "a player record is 0x2E * 8 = 0x170 bytes")
    check(img.read(consts["FindPlayerPed"], len(fp)) == fp, "FindPlayerPed is exactly Players[PlayerInFocus].ped")
    fv = img.read(consts["FindPlayerVehicle"], 0x28)
    check(fv[:3] == b"\x0F\xB6\x05" + b"" and fv[3:7] == le(consts["CWorld__PlayerInFocus"]), "FindPlayerVehicle starts the same way")
    check(b"\x80\xB9" + le(consts["PED_OFF_IN_VEHICLE"]) + b"\x00" in fv, "it tests the in-vehicle byte at ped+0x3AC")
    check(b"\x8B\x81" + le(consts["PED_OFF_MY_VEHICLE"]) in fv, "and returns the vehicle at ped+0x3A8")
    fc = img.read(consts["FindPlayerCoors"], 0x70)
    check(b"\x83\xC1" + bytes([consts["ENTITY_OFF_POSITION"]]) in fc and b"\xA5\xA5\xA5" in fc,
          "FindPlayerCoors copies three dwords from entity+0x34")

    print("\nthe timer")
    upd = img.read(consts["CTimer__Update"], 12)
    check(upd == b"\x55\xA1" + le(consts["CTimer__TimeInMilliseconds"]) + b"\x89\xE5\xA3" + le(consts["CTimer__PreviousTimeMs"])[:3],
          "Update copies the game clock into 'previous' first thing")
    check(img.read(0x4D107B, 5) == b"\xA3" + le(consts["CTimer__TimeInMilliseconds"]), "and stores the advanced clock back")
    check(img.read(0x4D10E6, 6) == b"\xD9\x1D" + le(consts["CTimer__TimeStep"]), "and stores the float time step")
    check(img.read(0x4D1038, 10) == b"\xC7\x05" + le(consts["CTimer__TimeStep"]) + le(0),
          "which is zero while paused")

    print("\nentity layout")
    check(img.read(0x40654F, 4) == b"\x0F\xBF\x51" + bytes([consts["ENTITY_OFF_MODEL_INDEX"]]),
          "a model index is an int16 at entity+0x5C (0x0040654F)")
    check(img.read(0x406557, 7) == b"\x8B\x04\x95" + le(consts["CModelInfo__ms_modelInfoPtrs"]),
          "and indexes CModelInfo::ms_modelInfoPtrs (0x00406557)")
    check(img.section(consts["CModelInfo__ms_modelInfoPtrs"]) is not None, "ms_modelInfoPtrs is in the image")
    check(img.read(0x50DC85, 10) == b"\xC7\x83" + le(consts["PED_OFF_HEALTH"]) + le(0x42C80000),
          "a new ped's health is 100.0f at ped+0x354 (0x0050DC85)")
    check(img.read(0x50DC93, 10) == b"\xC7\x80" + le(consts["PED_OFF_ARMOUR"]) + le(0),
          "and its armour is 0 at ped+0x358 (0x0050DC93)")
    check(img.read(0x4ED857, 10) == b"\xC7\x80" + le(consts["PED_OFF_ARMOUR"]) + le(0x42C80000),
          "the armour cheat stores 100.0f at +0x358 (0x004ED857)")
    call = img.read(0x43122A, 5)
    rel = struct.unpack("<i", call[1:])[0] if call and call[0] == 0xE8 else None
    check(rel is not None and (0x43122A + 5 + rel) & 0xFFFFFFFF == consts["FindPlayerVehicle"],
          "the fix-the-car cheat calls FindPlayerVehicle (0x0043122A)")
    check(img.read(0x43122F, 10) == b"\xC7\x80" + le(consts["VEHICLE_OFF_HEALTH"]) + le(0x447A0000),
          "and stores 1000.0f at vehicle+0x204 (0x0043122F)")

    print("\nentity flags and scan code")
    sc = le(consts["CWorld__ms_nCurrentScanCode"])[:2]
    check(img.read(0x4BC324, 4) == b"\x66\x8B\x53" + bytes([consts["ENTITY_OFF_SCAN_CODE"]]) and
          img.read(0x4BC328, 2) == b"\x66\xA1" and img.read(0x4BC32A, 2) == sc,
          "the scan code is a uint16 at entity+0x58 compared with ms_nCurrentScanCode (0x004BC324)")
    check(img.read(0x4BC337, 5) == b"\x8A\x43" + bytes([consts["ENTITY_OFF_FLAGS_A"]]) + b"\x24" + bytes([consts["ENTITY_USES_COLLISION"]]),
          "bUsesCollision is bit 0 of entity+0x51 (0x004BC337)")
    check(img.read(0x4BC342, 7) == b"\x8A\x43" + bytes([consts["ENTITY_OFF_FLAGS"]]) + b"\x24\x07\x3C\x02",
          "m_type is the low three bits of entity+0x50 (0x004BC342)")
    check(img.read(0x4BC359, 6) == b"\x8A\x43" + bytes([consts["ENTITY_OFF_FLAGS"]]) + b"\xC0\xE8\x03",
          "and m_status is what is left after shr 3 (0x004BC359)")

    print("\n%s" % ("all checks passed" if not failures else "FAIL (%d failures)" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
