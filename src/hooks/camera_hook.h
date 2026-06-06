#pragma once

namespace ACUHT {

// Installs the AnvilNext camera hook against ACU.exe.
//
// Resolves the CameraManager singleton (*(CameraManager**)0x14521AAD0,
// ACU v1.5.0), waits asynchronously for a live ACUPlayerCameraComponent,
// then swaps each instance's vptr onto a cloned vtable whose slot 81
// (Unk288_ApplyCameraFX) routes the engine's per-frame render-stage camera
// write through us. Offsets sourced from NameTaken3125/ACUFixes
// (MIT-licensed RE project).
//
// Returns true if the install pipeline started cleanly. False means
// fail-fast: wrong process (rundll32 helper) or game-version mismatch.
bool InstallCameraHook();
void RemoveCameraHook();

// Gate - toggled by Mod::SetEnabled. When false, the hook still runs but
// leaves the camera quaternion untouched.
void SetCameraHookEnabled(bool enabled);

// Toggle the NPC cull-frustum guard band on/off (for A/B comparison in-game).
// Returns the new state. The breakpoint stays armed; only the widening is
// gated, so toggling is instant and side-effect-free.
bool ToggleCullGuard();

#ifdef ACUHT_DEBUG_FRUSTUM_SCAN
// Arms a one-shot scan (runs on the next camera tick) that walks memory
// reachable from the live camera instance looking for the view/cull frustum
// (a contiguous run of unit-length plane normals, one aligned with a camera
// basis axis) and logs every candidate's base+offset. Investigation tooling
// for the NPC-culling guard band; Debug builds only.
void DebugArmFrustumScan();
#endif

} // namespace ACUHT
