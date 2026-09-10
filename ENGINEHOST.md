# enginehost Buriko/Ethornell plugin

This repository is the canonical enginehost fork of OpenBGI. The upstream
branch remains aligned with Cytlan/OpenBGI. Portable Android host integration
lives on `plugin-core`; release lines apply that changeset to a pinned upstream
revision.

The first plugin is deliberately experimental. It runs the supplied game
directory in place and advertises only OpenBGI's current compiled-script-v1
compatibility. It neither invokes Wine nor bundles proprietary engine code.

OpenBGI remains GPL-2.0 licensed. SDL is used under its zlib license. See
`LICENSE` and `THIRD_PARTY` for upstream attribution and limitations.
