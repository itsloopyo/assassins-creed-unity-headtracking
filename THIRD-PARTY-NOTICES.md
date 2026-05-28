# Third-Party Notices

## Ultimate ASI Loader

- **Version:** v9.x (latest within range)
- **License:** MIT
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** Generic x64 .asi plugin loader. ACU imports `dinput8.dll`, which Ultimate ASI Loader impersonates to load this mod.
- **Bundled:** yes. Bundled in release ZIP as fallback; fetched latest within range at install time.

---

## MinHook

- **Version:** v1.3.3
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** API hooking library, linked statically into `AssassinsCreedUnityHeadTracking.asi`.
- **Bundled:** yes. Linked statically; fetched via CMake FetchContent at build time.

---

## cameraunlock-core

- **Version:** git submodule (see `cameraunlock-core/` commit)
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared OpenTrack/UDP receiver, tracking processor, pose and position interpolation, pattern scanner, INI reader, and math utilities.
- **Bundled:** yes. Consumed as a git submodule and linked statically.

---

## OpenTrack

- **Version:** N/A (protocol only)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** This mod receives OpenTrack UDP packets. No OpenTrack code is bundled.
- **Bundled:** no.

---

## Game

Assassin's Creed Unity (C) Ubisoft Entertainment. This project is an
independent, unofficial mod. It contains no game code or assets from
Assassin's Creed Unity; it interacts with the retail game binary at
runtime via pattern scanning and function hooking. You must own a
legitimate, purchased copy of Assassin's Creed Unity to use this mod.
