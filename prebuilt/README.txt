CoopVC.asi - milestone 3a, built with MinGW (i686-w64-mingw32), static runtime,
no dependencies beyond Windows' own DLLs. NOT built with MSVC/xmake, and NOT
yet run inside the game: the only runs so far were under Wine, in a process
that was not Vice City, where it logged the reason and stood down.

It only reads: it logs the player, pools and clock every ten seconds of play. See docs/porting.md, "Trying milestone 2 in the game".
Built from the sources in this folder; the SHA-256 is in SHA256.txt.
