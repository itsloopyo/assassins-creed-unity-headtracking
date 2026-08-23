# Third-Party Notices

## Ultimate ASI Loader

- **Version:** v9.7.2 (upstream commit `ab722befd52581a34449b603926cfab476e66b05`)
- **License:** MIT (upstream `LICENSE` reproduced verbatim at `vendor/ultimate-asi-loader/LICENSE`)
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** Generic x64 `.asi` plugin loader. ACU imports `dinput8.dll`, which Ultimate ASI Loader stands in for so this mod can load.
- **Bundled:** yes. The unmodified upstream `dinput8.dll` is committed at `vendor/ultimate-asi-loader/`, bundled in the release ZIP and used as the install-time source. Nothing is fetched from upstream at install time.

---

## cameraunlock-core

- **Version:** git submodule (see the `cameraunlock-core/` commit pointer)
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared OpenTrack/UDP receiver, tracking processor, pose and position interpolation, PE fingerprinting, INI reader, and math utilities.
- **Bundled:** yes. Consumed as a git submodule and linked statically.

---

## OpenTrack

- **Version:** N/A (wire protocol only)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** This mod receives OpenTrack UDP packets. No OpenTrack code is bundled.
- **Bundled:** no.

---

## ACUFixes (NameTaken3125)

- **License:** none published. The repository carries no LICENSE file, so all
  rights are reserved by its author.
- **Upstream:** https://github.com/NameTaken3125/ACUFixes
- **Usage:** ACUFixes is an independent, community reverse-engineering effort
  covering Assassin's Creed Unity 1.5.0. The numeric facts about the retail
  binary published there - the CameraManager and MenuManager singleton
  addresses, and the struct field offsets named in `src/hooks/camera_hook.*` -
  are what let this mod find the camera without repeating that work. We are
  grateful for it, and credit is owed whether or not it is legally required.
- **Bundled:** no. **No code, header, or other file from ACUFixes is copied,
  adapted, redistributed, or linked into this project.** Because the project
  publishes no license, nothing from it could be redistributed even if we
  wanted to. What this repository contains is our own implementation, which
  refers to those addresses and offsets as numbers. If the author would prefer
  we did not build on their findings, open an issue and we will remove the
  references.

---

## Game

Assassin's Creed Unity (C) Ubisoft Entertainment. This project is an
independent, unofficial mod, not affiliated with or endorsed by Ubisoft.

It contains no game code, assets, binaries, decompiled output, or other
material from Assassin's Creed Unity, and it redistributes none. It interacts
with the retail game binary on the user's own machine at runtime, by reading
addresses and swapping a vtable pointer on a heap object the game allocated.
The installed mod modifies no file the game ships, and it circumvents no
digital rights management, copy protection, license check, or ownership check.
Ubisoft Connect performs its ownership check exactly as it does without this
mod installed.

`scripts/skip-intros.ps1` is a developer convenience that is not part of the
mod and is not included in any release. It renames the game's startup splash
videos in place on the developer's own installation so the game boots straight
to the menu during testing, and `-Restore` renames them back. It copies,
extracts, and redistributes nothing.

You must own a legitimate, purchased copy of Assassin's Creed Unity to use
this mod.
