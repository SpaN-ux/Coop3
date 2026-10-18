// Addresses inside gta-vc.exe.
//
// ############################################################
// #  EVERY ADDRESS HERE IS SPECIFIC TO ONE EXACT BUILD.      #
// #  gta-vc.exe, MD5 167A5C8B31B3E0DBEFA033CA24453D4E        #
// #  3 088 896 bytes, PE timestamp 2012-09-10, base 0x400000 #
// #  Nothing here is portable to another gta-vc.exe.         #
// ############################################################
//
// What goes in this file: only what has been proven against the exe itself.
// Each entry says how. Things that were found but not proven live in
// docs/addresses.md under "unverified", and are not promoted until somebody
// has done the proof; a guess and a proof must not look the same.
//
// Method used so far (all static, nothing was run): find a string the game
// uses, find the code that pushes its address, read the function around it
// with a disassembler, and match its shape against the reconstructed VC
// source (reVC, "miami" branch of GTAmodding/re3), used as a map only.
#pragma once

#include <cstdint>

namespace coopvc::game {

// ---- image identity -------------------------------------------------------

constexpr uint32_t IMAGE_BASE  = 0x00400000;
constexpr uint32_t IMAGE_SIZE  = 3088896;                          // file size
constexpr char     IMAGE_MD5[] = "167A5C8B31B3E0DBEFA033CA24453D4E";

// Two strings in the exe's .data section. .data is writable in this build, so
// neither is a promise that nothing rewrote it; together with the base
// address and the function prologue below they are enough to refuse a
// different executable before any hook goes in.
//
//   VA 0x0068DFE8  (file offset 0x28B9E8)  "grandtheftauto3"
//       The magic CGame::ValidateVersion compares the decoded peds.col
//       against. Yes, Vice City still says "3". Pushed by the memcmp-style
//       call at 0x004A5383.
//   VA 0x0068E59C  (file offset 0x28BF9C)  "DATA\GTA_VC.DAT"
//       The data file the game boots from.
constexpr uintptr_t VA_VERSION_MAGIC = 0x0068DFE8;
constexpr uintptr_t VA_BUILD_MARKER  = 0x0068E59C;

// ---- version check --------------------------------------------------------

// CGame::ValidateVersion (reVC Game.cpp). Proof, read off the disassembly:
//
//   0x004A5320  push ebx / push esi / push ebp / add esp,-0x80
//   0x004A5326  push 0x68DFCC ("rb") ... push 0x68DFD0 ("models\coll\peds.col")
//   0x004A5330  call 0x48DF90            open the file
//   0x004A5359  sub byte [esp+ebp+0xC], 0x17      the "decryption" step
//   0x004A5376  cmp ebp, 0x80 / jl                reads up to 128 bytes
//   0x004A5381  push 0xF / push 0x68DFE8 / push eax / call 0x642650
//                                         compares 15 bytes with the magic
//   0x004A539C  push 0x7E9DC8 ... call 0x642580   copies the rest to
//                                         version_name, max 0x40 bytes
//   0x004A53B7  push 0x68DFF8 ("Invalid version") ... jmp $ (hangs)
//
// That is ValidateVersion step for step, and the call at 0x004A51A3 is its
// one caller. It runs once at start-up, so it is a landmark, not a frame hook.
constexpr uintptr_t CGame__ValidateVersion         = 0x004A5320;
constexpr uintptr_t CGame__ValidateVersion__Caller = 0x004A51A3;

// char[64], zero-initialised (.bss), written by the copy at 0x004A539C. It is
// the *data* version from peds.col, like in the previous game: fine for logs,
// not a substitute for the checks above, since peds.col can be swapped.
constexpr uintptr_t version_name = 0x007E9DC8;

// The first bytes of ValidateVersion. Code, so advisory only: another mod
// hooking it first would change them, which is not an error, but worth a log
// line when diagnosing a conflict.
constexpr uint8_t PROLOGUE_VALIDATE_VERSION[] = {0x53, 0x56, 0x55, 0x83, 0xC4, 0x80};

// ---- the main loop and the frame hook ---------------------------------------

// WinMain's loop, at 0x005FFF40..0x006004F7, is a state machine: it switches
// on a global and the jump table for it is at 0x006D68E4, ten entries.
//
//   game_state            0x009B5F08  int, the switch variable
//   state values          0..9, in jump-table order. Proven by their code:
//                           0 writes 1 (0x005FFFA5)
//                           1 plays the logo video, writes 2 (0x005FFFB4)
//                           6 is the frontend (menu) init: 0x00600590 stores 6
//                             when the frontend is wanted, 0x006005A0 stores 8
//                             when a game is, so 8 is the game init
//                           9 is playing: its case (0x0060042C) times the
//                             frame and calls RsEventHandler(0x1A, 1)
//   quit flag             0x009B48F0  set to 1 by WM_QUIT (0x006006E7) and by
//                                     RsEventHandler(0x1E) (0x00602EF8)
constexpr uintptr_t game_state = 0x009B5F08;
constexpr uintptr_t quit_flag  = 0x009B48F0;

constexpr int GS_INIT_FRONTEND = 6;   // start-up is over: logos and one-time init are done
constexpr int GS_INIT_GAME     = 8;
constexpr int GS_PLAYING_GAME  = 9;

// RsEventHandler (reVC skeleton.cpp). cdecl, (event, param), returns the
// status. Proof:
//
//   0x00602EE0  push esi / push edi / mov esi,[esp+0x10] / mov edi,[esp+0xC]
//               - two cdecl arguments, event in edi, param in esi
//   0x00602EEC  call 0x004A5AD0          AppEventHandler(event, param) first
//   0x00602EF1  cmp edi, 0x1E / jne
//   0x00602EF8  mov [0x9B48F0], 1        event 0x1E is the quit event
//   0x00602F19  jmp [edx*4 + 0x6D6E74]   the default handler's jump table
//
// 25 call sites, every one popping its two arguments itself. The playing
// state's call (0x006004A2, push 1 / push 0x1A) is the once-a-frame one.
constexpr uintptr_t RsEventHandler     = 0x00602EE0;
constexpr uintptr_t AppEventHandler    = 0x004A5AD0;
constexpr int       EVENT_IDLE         = 0x1A;   // the playing state's per-frame event
constexpr int       EVENT_QUITAPP      = 0x1E;

// The first ten bytes. Starting with push esi / push edi means the five bytes
// a detour overwrites hold no relative jump or call, so the trampoline is a
// plain copy.
constexpr uint8_t PROLOGUE_RS_EVENT_HANDLER[] = {0x56, 0x57, 0x8B, 0x74, 0x24, 0x10,
                                                 0x8B, 0x7C, 0x24, 0x0C};

// ---- pools ------------------------------------------------------------------

// CPools::Initialise builds every pool in one run of "new CPool(size, name)"
// at 0x004C0270..0x004C03BE: each is `push 0x10 / call operator_new / ... /
// push name / push size / call ctor / mov [global], eax`, and the constructor
// returns the new pool in eax. Sizes and names, read off that code:
//
//   global      size    name           entry size
//   0x00A0FD38  50000   "PtrNode"
//   0x00A0D8BC   3200   "EntryInfoNode"
//   0x0097F2AC    140   "Peds"          0x6D8   (imul at 0x004C09C1)
//   0x00A0FDE4    110   "Vehicles"      0x5DC   (imul at 0x004C0B21)
//   0x0097F240   7000   "Buildings"
//   0x00978834      1   "Treadables"
//   0x0094DBE0    460   "Objects"
//   0x0097F25C   2340   "Dummys"
//   0x00978778    192   "AudioScriptObj"
//   0x009753C0   4400   "ColModel"
//
// Each global holds a pointer to a CPool, whose layout the constructor
// (0x004C09B0 for peds) shows:
//
//   +0x00  entries   size * entry-size bytes, malloc'd
//   +0x04  flags     one byte per slot, malloc'd
//   +0x08  size      slot count
//   +0x0C  allocPtr  starts at -1
//
// A slot's flag byte is bit 7 = FREE (every slot starts as 0x80, the
// constructor ORs it in and then masks to 0x80), and bits 0..6 a counter that
// changes each time the slot is reused, so a stale handle stops matching.
constexpr uintptr_t ms_pPedPool     = 0x0097F2AC;
constexpr uintptr_t ms_pVehiclePool = 0x00A0FDE4;

constexpr uint32_t PED_ENTRY_SIZE     = 0x6D8;
constexpr uint32_t VEHICLE_ENTRY_SIZE = 0x5DC;
constexpr int      PED_POOL_SIZE      = 140;
constexpr int      VEHICLE_POOL_SIZE  = 110;

constexpr uintptr_t CPool_FREE_BIT_MASK = 0x80;   // flag byte & this: slot is free

constexpr uintptr_t CPool_OFF_ENTRIES = 0x00;
constexpr uintptr_t CPool_OFF_FLAGS   = 0x04;
constexpr uintptr_t CPool_OFF_SIZE    = 0x08;

// Handles. CPool<CPed>::GetAt, thiscall, one argument, `ret 4`:
//
//   0x00451CB0  index = handle >> 8 (arithmetic)
//               flag  = flags[index]
//               if ((handle & 0xFF) != flag) return 0     (stale, or free)
//               return entries + index * 0x6D8
//
// and the reverse at 0x00451CF0, which turns a pointer into a handle:
//   index = (ptr - entries) / 0x6D8 (the division is the 0x95A02569 multiply)
//   handle = (index << 8) + flags[index]
// The vehicle pool's GetAt is the same code with 0x5DC, at 0x00451C70.
constexpr uintptr_t CPool_Ped_GetAt     = 0x00451CB0;
constexpr uintptr_t CPool_Ped_GetHandle = 0x00451CF0;
constexpr uintptr_t CPool_Vehicle_GetAt = 0x00451C70;

// ---- the player -------------------------------------------------------------

// FindPlayerPed, 0x004BC120, is the whole of:
//
//   movzx eax, byte [0x00A10AFB]          PlayerInFocus
//   imul  eax, eax, 0x2E                  46 ...
//   mov   eax, [eax*8 + 0x0094AD28]       ... times 8 = 0x170 bytes per record
//   ret
//
// so CWorld::Players is an array of 0x170-byte records at 0x0094AD28 whose
// first field is the ped pointer, and PlayerInFocus is the index in use.
constexpr uintptr_t CWorld__Players        = 0x0094AD28;
constexpr uintptr_t CWorld__PlayerInFocus  = 0x00A10AFB;   // u8
constexpr uint32_t  PLAYER_INFO_SIZE       = 0x170;
constexpr uintptr_t FindPlayerPed          = 0x004BC120;

// The next functions in that file, same shape, give the entity offsets:
//
//   FindPlayerVehicle 0x004BC1E0: ped = FindPlayerPed(); if (ped &&
//       byte [ped + 0x3AC]) return dword [ped + 0x3A8]; else 0
//   FindPlayerCoors   0x004BC240: takes a CVector*; takes the vehicle
//       if the player is in one, else the ped; copies three dwords from
//       [entity + 0x34] to the argument and returns it
constexpr uintptr_t FindPlayerVehicle = 0x004BC1E0;
constexpr uintptr_t FindPlayerCoors   = 0x004BC240;

constexpr uint32_t ENTITY_OFF_POSITION   = 0x34;    // float x, y, z
constexpr uint32_t PED_OFF_MY_VEHICLE    = 0x3A8;   // CVehicle*
constexpr uint32_t PED_OFF_IN_VEHICLE    = 0x3AC;   // u8, non-zero while in one

// ---- the timer --------------------------------------------------------------

// CTimer::Update, 0x004D0F30. Its first four instructions copy the game clock
// into "previous" and it later stores the advanced clock back, only when
// neither pause flag is set (0x004D1047..0x004D107B); the same function stores
// the frame's time step, as a float, at 0x00975424 (0x004D10E6), and zero
// while paused (0x004D1038).
//
//   0x004D0DF0  CTimer::GetCurrentTimeInCycles   QueryPerformanceCounter, low dword
//   0x004D0E30  CTimer::GetCyclesPerMillisecond  [0x00691010], or 1
//   0x004D0F30  CTimer::Update
//   0x00974B2C  game time in ms, u32, advances only while unpaused
//   0x00978D8C  the same, as of the frame before
//   0x00975424  this frame's time step, float, 0 while paused
constexpr uintptr_t CTimer__Update             = 0x004D0F30;
constexpr uintptr_t CTimer__TimeInMilliseconds = 0x00974B2C;
constexpr uintptr_t CTimer__PreviousTimeMs     = 0x00978D8C;
constexpr uintptr_t CTimer__TimeStep           = 0x00975424;

// ---- not yet found --------------------------------------------------------
//
// Spawning a ped, giving a ped a task, the weapon and animation state, and the
// vehicle spawn path. docs/porting.md lists them in the order they are needed.

} // namespace coopvc::game
