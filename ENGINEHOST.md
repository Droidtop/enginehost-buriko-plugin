# enginehost Buriko/Ethornell plugin

This repository is the canonical enginehost fork of OpenBGI. The upstream
branch remains aligned with Cytlan/OpenBGI. Portable Android host integration
lives on `plugin-core`; release lines apply that changeset to a pinned upstream
revision.

The first plugin is deliberately experimental. It runs the supplied game
directory in place and advertises only OpenBGI's current compiled-script-v1
compatibility. It neither invokes Wine nor bundles proprietary engine code.

The engine runs only in Enginehost's sandbox (`isolatable`, Enginehost
`docs/engine-sandbox.md`): `BurikoPlugin` starts `main()` on its own thread in
the isolated process with the game folder's path, and
`app/src/main/cpp/os_enginehost.c` is the engine's OS layer there in place of
`src/os.c`: frames are kept in memory for the host to draw, sound is mixed
into the host's audio ring, and input arrives as the virtual keys and mouse
the original reads. Every file the engine opens goes through Enginehost's
file layer (`plugin-native/`, copied verbatim from Enginehost), which serves
the game folder by its real path; the bundle declares `writesGameFolder`
because the original keeps `BGI.gdb` and its saves beside the game.

OpenBGI remains GPL-2.0 licensed. SDL is used under its zlib license. See
`LICENSE` and `THIRD_PARTY` for upstream attribution and limitations.

Compatibility contexts: the wrapper advertises this incomplete interpreter
under the `buriko` plugin family. Generic BGI titles use context
`compiled-script-v1`; AUGUST titles using the same BGI scenario line use the
explicit `august-compiled-script-v1` context. Both currently target
compiled-script version `1.0` and share the same experimental opcode
implementation. The separate context records game-family compatibility
without pretending AUGUST is a distinct runtime engine or overstating current
playability.
