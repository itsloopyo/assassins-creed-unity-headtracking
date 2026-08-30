# Assassin's Creed Unity Head Tracking

![Assassin's Creed Unity running with this mod](https://raw.githubusercontent.com/itsloopyo/assassins-creed-unity-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Assassin's Creed Unity that moves the camera with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

## Features

- **Head-driven camera nudge** - layered on top of the normal third-person orbit; mouse and controller still drive aim and Arno's facing.
- **6DOF positional tracking** - lean and peek with head position, tuned to ACU's tight follow camera.

## Requirements

- A legitimate copy of [Assassin's Creed Unity on Steam](https://store.steampowered.com/app/289650/Assassins_Creed_Unity/) (or Ubisoft Connect).
- A head-tracking source that outputs OpenTrack UDP, such as [OpenTrack](https://github.com/opentrack/opentrack) with a webcam, a VR headset, or a phone app.
- Windows 10 or 11, 64-bit.

## Installation

1. Download the latest installer ZIP from the [Releases page](https://github.com/itsloopyo/assassins-creed-unity-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack (or your tracker) to send UDP output to `127.0.0.1:4242`.
5. Launch the game normally via Steam or Ubisoft Connect.

If the installer cannot find your game, point it at the install folder explicitly with either an environment variable or a positional argument:

```powershell
set ASSASSINS_CREED_UNITY_PATH=D:\Games\Assassin's Creed Unity
install.cmd
```

or

```powershell
install.cmd "D:\Games\Assassin's Creed Unity"
```

### Manual Installation
For users who prefer to place files by hand, extract the Nexus ZIP into the folder containing `ACU.exe`:

1. Install the [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) yourself, kept as `dinput8.dll` next to `ACU.exe`.
2. Copy `AssassinsCreedUnityHeadTracking.asi` into the same folder.
3. `HeadTracking.ini` is written next to `ACU.exe` on first launch.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

`Page Down` / `Ctrl+Shift+H` toggles yaw between **world-space** (horizon-locked, default) and **camera-local**.

## Configuration

The mod writes `HeadTracking.ini` next to `ACU.exe` on first launch. All settings are annotated in-line; the defaults are below.

```ini
[Network]
; UDP port for OpenTrack data
UDPPort=4242

[Sensitivity]
YawMultiplier=1.0
PitchMultiplier=1.0
; Roll reduced for third-person. ACU is a parkour game and unchecked
; roll whips the horizon on rooftop landings. Raise to 1.0 for more tilt.
RollMultiplier=0.5
; Smoothing is chosen per connection and covers rotation and position.
; LocalSmoothing applies when the tracker runs on this machine (loopback),
; RemoteSmoothing when it is a phone or other device on the network.
; 0.0 = no smoothing, 1.0 = heavy (~5s settling).
LocalSmoothing=0.0
RemoteSmoothing=0.15

[Position]
SensitivityX=1.0
SensitivityY=1.0
SensitivityZ=1.0
; Positional limits in meters, kept conservative for ACU's tight follow camera.
LimitX=0.20
LimitY=0.15
LimitZ=0.25
LimitZBack=0.05
InvertX=true
InvertY=false
InvertZ=false
Enabled=true

[Hotkeys]
; Virtual key codes in hex. Nav-cluster defaults:
ToggleKey=0x23        ; End
PositionToggleKey=0x21; Page Up
YawModeKey=0x22       ; Page Down
; Chord alternatives (Ctrl+Shift+<letter>):
ChordToggleKey=0x59   ; Y
ChordPositionKey=0x47 ; G
ChordYawModeKey=0x48  ; H

[General]
AutoEnable=true
; Horizon-locked yaw (true) is best for third-person games.
WorldSpaceYaw=true
; Adds camera-discovery diagnostics to HeadTracking.log when reporting a problem.
VerboseLogging=false

[Culling]
; Widens actor visibility culling so head turns do not reveal empty crowd edges.
; This does not change the rendered FOV. Ctrl+Shift+J toggles it in-game.
GuardEnabled=true
; Side-plane outward bias in metres.
GuardBiasMeters=500.0
```

## Troubleshooting

- **Mod not loading** - confirm `dinput8.dll` and `AssassinsCreedUnityHeadTracking.asi` sit next to `ACU.exe`. A `HeadTracking.log` file appearing next to them is a good sign. It is rewritten on every launch and the previous run is kept as `HeadTracking.prev.log`, so send both when reporting a problem. If the game crashes on launch, removing those two files restores vanilla behavior.
- **No tracking response** - check that your tracker is outputting UDP to `127.0.0.1:4242` and that the port matches `UDPPort` in `HeadTracking.ini`.
- **Jittery or unstable tracking** - raise `[Sensitivity] LocalSmoothing` (tracker on this PC) or `[Sensitivity] RemoteSmoothing` (phone or other network device) toward `0.3`. Local defaults to `0.0` for zero latency, remote to `0.15` because network packets jitter; the mod picks one per connection from the packet source address.
- **Wrong rotation / horizon whipping** - lower `[Sensitivity] RollMultiplier`, or set `[General] WorldSpaceYaw=true` for horizon-locked yaw. Use the invert options in `[Position]` if an axis moves the wrong way.
- **NPCs disappear while turning your head** - keep `[Culling] GuardEnabled=true`. Raise `GuardBiasMeters` if crowd edges still pop; lower it if you need to test performance impact.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs. The Ultimate ASI Loader is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Prerequisites: Visual Studio 2022 with the C++ workload, CMake 3.20+, git, and [pixi](https://pixi.sh).

```powershell
git clone --recurse-submodules https://github.com/itsloopyo/assassins-creed-unity-headtracking
cd assassins-creed-unity-headtracking
pixi run build-release
pixi run package
```

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT - see [LICENSE](LICENSE). Copyright (c) 2026 itsloopyo.

## Credits

- **Ubisoft Montreal / Ubisoft** - Assassin's Creed Unity and the AnvilNext engine.
- **NameTaken3125** - [ACUFixes](https://github.com/NameTaken3125/ACUFixes), the community reverse-engineering project this mod's camera and menu offsets come from. No ACUFixes code is used or redistributed here; see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
- **ThirteenAG** - [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader).
- **Stanisław Halik et al.** - [OpenTrack](https://github.com/opentrack/opentrack).
- Built on the shared [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core) library.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Ubisoft, and requires a legitimately purchased copy of the game. It ships no game code or assets, modifies no file the game installs, and bypasses no copy protection or ownership check. It changes only what you see on your own machine; aim, hitboxes, and game logic are unchanged. Tracking is client-side, but keep it off in co-op out of courtesy. Use at your own risk.
