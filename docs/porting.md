# Porting plan

What is done, what is proven, and how to find each next thing.

## How an address gets into `addresses.h`

Only when it is proven against the exe. The method, all static:

1. Find a string the game uses, or an import (`PeekMessageA`, `timeGetTime`).
2. Find the code that references it (`push imm32`, `call [iat]`).
3. Read the function around it, and match its shape against the reconstructed
   Vice City source (reVC, the `miami` branch of GTAmodding/re3) - as a map,
   never as an authority. Calling convention, argument count, what it writes.
4. Write the proof into the comment beside the constant, and assert the same
   bytes in `tools/vcexe/check.py`. If it cannot be asserted from the file, it
   is not proven, and it does not go in.

## Milestones

| # | What | State |
|---|---|---|
| 1 | Load, read config, log, refuse any other exe | done, tested under Wine in a non-VC process |
| 2 | Hook the once-a-frame call and count frames | hook logic tested with a real detour; **not yet run in the game** |
| 3a | Read the player, the ped and vehicle pools, the game clock | addresses proven against the exe (89 checks); logic tested against a fake world; **not yet run in the game** |
| 3b | Spawn a ped, move it, give it an animation | not started |
| 4 | Ghost players: others' position and animation | not started |
| 5 | Vehicles, then combat, then world sync (clock, weather, traffic) | not started |
| 6 | Missions | not started; the hardest by far |

## Proven so far

See the comments in `client/src/game/addresses.h`; `check.py` asserts each.

* `CGame::ValidateVersion` 0x004A5320 and its caller; `version_name` 0x007E9DC8.
* WinMain's state machine: `game_state` 0x009B5F08, its jump table
  0x006D68E4, state 9 = playing, 6 = menu, 8 = game init, quit flag 0x009B48F0.
* `RsEventHandler` 0x00602EE0, called once a frame by the playing state with
  event 0x1A.
* The pools: `CPools::Initialise` at 0x004C0270 builds ten of them; the ped
  pool is at 0x0097F2AC (140 slots of 0x6D8 bytes) and the vehicle pool at
  0x00A0FDE4 (110 of 0x5DC). Layout, free bit and the handle format
  (`index << 8 | flag byte`) are proven from `CPool::GetAt` at 0x00451CB0.
* The player: `FindPlayerPed` 0x004BC120 reads `Players[PlayerInFocus]`, 0x170
  bytes a record at 0x0094AD28, index byte at 0x00A10AFB. An entity's position
  is three floats at +0x34; a ped's car is at +0x3A8, in-car flag at +0x3AC.
* The timer: `CTimer::Update` 0x004D0F30, game time in ms at 0x00974B2C,
  the frame's time step (float) at 0x00975424.

## Trying milestone 2 in the game

Untested in the game: this is the first thing to run. You need an ASI loader
(`dinput8.dll` beside `gta-vc.exe`) and the supported exe.

1. Copy `prebuilt/CoopVC.asi` and `CoopVC.ini` next to `gta-vc.exe`.
2. Start the game with the marker set, from a command prompt in that folder:
   `set COOPVC_LAUNCHED=1 && gta-vc.exe`
3. Start a new game, play a few seconds, quit.
4. Read `CoopVC.log` beside the .asi. A good run ends with
   `frame hook installed`, then `the first frame of a game has run`, then a line
   every ten seconds of play like

       frame 300: player on foot at (-378.5, -470.2, 11.1); peds 21/140;
       vehicles 9/110; game time 12345 ms, step 1.00

   Check it against the game: the position should change as you walk (and the
   z near the ground height there), the ped and vehicle counts should move as
   you walk through traffic, and game time should stop growing while paused.

If it reports that `RsEventHandler` does not start with the expected bytes,
another mod patched it first or the exe is not the supported one; send the log.

## Next: milestone 3b

Reading is done; writing is where a wrong address starts to hurt, so each of
these needs its own proof before it is used. Suggested leads, none tried yet:

1. Spawning a ped: the constructor that fills a ped-pool slot, and the pool's
   own allocate/free. Both are near `CPool::GetAt` (0x00451CB0); `CPool::New`
   is the function that scans `allocPtr` for a slot with the free bit set.
2. Moving a ped without the game fighting it: its position is at +0x34 of an
   entity, but the physics and the ped's task state overwrite it every frame,
   so this is about where to stand in the frame, not what to write.
3. Animation and weapon state, for what other players are seen doing.

## Leads from the 3b search (observed, NOT proven)

Nothing below is in `addresses.h`, and none of it may be used until it has a
proof and a line in `tools/vcexe/check.py`. They are what a scan of every
reference to the player array and the ped pool turned up.

* There is no standalone "allocate a pool slot" function to hook. A scan of the
  whole code section for the allocation loop (read `allocPtr`, step it, wrap at
  `size`, test the free bit) found nothing near the pool code, so the loop is
  most likely inlined into each type's `operator new`. The way in is therefore
  the constructor's caller, not the pool.
* 0x004BED90 stores a ped pointer into `Players[0]`, but it sits inside a
  ten-iteration loop that restores state, with its own cleanup after it: it is
  most likely the save-game loader, not the code that creates a ped. A dead
  end for spawning; recorded so nobody follows it again.
* The script engine keeps its operands in a small array at 0x007D743C, indexed
  from 0x007D7438, and hands ped handles to `CPool::GetAt` (0x00451CB0) at over
  a hundred call sites. Opcode handlers are a good place to see how the game
  itself creates, moves and removes a ped, because each one is a short function
  with a known effect.
* Fields read off a ped in many places, meaning not yet established: a dword at
  +0x244 compared with small constants (looks like a state), a float at +0x354
  compared against thresholds (looks like health), a pointer at +0x5F4.

Next step in this direction: take one script opcode that creates a ped, follow
its call to the constructor, and prove that function the way the pools were
proven.
