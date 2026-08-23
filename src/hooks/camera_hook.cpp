#include "pch.h"
#include "camera_hook.h"
#include "core/mod.h"
#include "core/logger.h"
#include "core/constants.h"

#include <cameraunlock/math/quat4.h>
#include <cameraunlock/math/vec3.h>
#include <cameraunlock/memory/pe_fingerprint.h>

#include <xmmintrin.h>
#include <utility>
#include <tlhelp32.h>

namespace ACUHT {

// ----------------------------------------------------------------------------
// AnvilNext 2.0 camera hook for Assassin's Creed Unity v1.5.0
// ----------------------------------------------------------------------------
//
// All offsets and the singleton address are the ones published by the
// NameTaken3125/ACUFixes reverse-engineering project, which targets the same
// game version. Its findings are referenced as numbers; none of its code is
// used here (it publishes no license). References:
//   CommonLibACU/ACU-RE/inc/ACU/CameraManager.h
//   CommonLibACU/ACU-RE/inc/ACU/ACUPlayerCameraComponent.h
//
// Why per-instance vtable swap instead of MinHook function patching:
// ACU ships wrapped in VMProtect, which requires its own code and read-only
// data to stay byte-for-byte intact; a MinHook prologue patch in .text, or a
// write to the shared vtable in .rdata, breaks that requirement and the
// process exits. Rather than work around that, we do not touch protected
// memory at all: per-instance vtable replacement writes only the vptr slot
// (offset 0) of a camera object the game allocated on the heap. No section of
// the shipped executable is modified, and no protection, license check or
// ownership check is bypassed or interfered with.
// ----------------------------------------------------------------------------

namespace {

// Every address/RVA below is pinned to this exact build (ACU v1.5.0, the
// final patch, 2015-02-11). The fingerprint is checked before any of them is
// dereferenced or breakpointed; an unknown build leaves the mod fully dormant
// instead of corrupting memory in a binary where these offsets mean something
// else. If Ubisoft ever ships another build (or a store variant differs),
// append a new profile - never edit these values in place.
constexpr cameraunlock::memory::PeFingerprint kAcuV150Fingerprint = {
    0x54DB5826,  // TimeDateStamp (2015-02-11)
    0x07825000,  // SizeOfImage
    0x01D87E65,  // CheckSum
};

constexpr uintptr_t kCameraManagerSingletonAddr = 0x14521AAD0;
constexpr size_t    kCMOffArrToPlayerCam        = 0x40;   // SmallArray::arr ptr
constexpr size_t    kCMOffArrSize               = 0x4A;   // SmallArray::size (uint16)
constexpr int       kApplyFXVTableByteOffset    = 0x288;
constexpr int       kApplyFXSlotIndex           = kApplyFXVTableByteOffset / 8;

// MenuManager singleton, used to suppress tracking on the title screen and any
// other full-screen menu (pause, inventory, map). Offsets from the same
// NameTaken3125/ACUFixes project (CommonLibACU/ACU-RE/inc/ACU/MenuManager.h):
//   MenuManager        +0x30 -> MenuManager_30* menuPagesStack
//   MenuManager_30     +0x10 -> SmallArray<...> arrHasMenuPages
// A non-empty arrHasMenuPages means a menu page is on screen. During free-roam
// gameplay the stack is empty; the HUD lives in a separate manager.
constexpr uintptr_t kMenuManagerSingletonAddr = 0x1451EACE8;
constexpr size_t    kMMOffMenuPagesStack      = 0x30;   // MenuManager_30*
constexpr size_t    kMM30OffArrHasMenuPages   = 0x10;   // SmallArray::arr ptr
constexpr size_t    kSmallArraySizeOffset     = 0x0A;   // SmallArray::size (uint16)

// Size of the vtable we copy into our own memory. Slot 81 is what we need;
// 256 slots covers every virtual any AnvilNext class is realistically going
// to have, with headroom. Reading past the real vtable's end is safe because
// the game won't dispatch to those slots from our cloned table.
constexpr int kClonedVTableSlots = 256;

// ----------------------------------------------------------------------------
// NPC cull-frustum guard band
// ----------------------------------------------------------------------------
// Head tracking rotates the rendered view (in HookedApplyCameraFX) downstream
// of AnvilNext's visibility culling, so actors just outside the un-rotated FOV
// are culled before a head-turn reveals them. The Gribb-Hartmann frustum
// builder FUN_14223bd70 writes 6 world-space cull planes from a view-projection
// matrix, then a 6-iteration loop derives AABB-test data from them. We place a
// hardware EXECUTE breakpoint just after the 6th plane is written and before
// that loop (RVA +0x223c064, RCX = frustumBase + 0xc there) and, only for the
// main camera's frustum (apex at the camera eye), rotate the 4 side planes
// outward by the current head deflection. Render is unaffected (it rasterises
// from the projection matrix); only what gets submitted widens - a guard band.
//
// A hardware breakpoint is used rather than a code patch for the same reason
// the camera uses a per-instance vtable swap: the builder is reached by a
// direct CALL, so there is no pointer to swap, and we will not write to the
// shipped executable's sections. A debug register modifies no game memory at
// all. Offsets target ACU v1.5.0.
constexpr uintptr_t kFrustumBuilderInjectRva = 0x223C064;

// Actor-cull suppressor: the per-entity visibility function FUN_141ce8a70 ORs
// (bVar8<<0x22 | !bVar1 | 0x80000) << 4 into [RDI+0x88] at +0x1CE8CC7 - bit 4
// of that = IsHiddenByUserCullingPlanes. RDI holds the entity. RAX holds the
// OR mask, computed just before this insn. If we clear bit 4 of RAX in a VEH
// before the OR runs, the engine cannot set the "hidden by cull volumes" bit,
// and NPCs that would have been culled stay visible. Hooked via a HW execute
// breakpoint (DR2), so no game memory is written.
constexpr uintptr_t kActorCullOrInstrRva = 0x1CE8CC7;

std::atomic<bool> g_guardEnabled{true};
void*             g_guardVeh = nullptr;
uintptr_t         g_guardInjectAddr = 0;
uintptr_t         g_actorCullInstrAddr = 0;
float             g_guardBiasMeters = 500.0f;
std::atomic<int>  g_guardFireLogCount{0};  // log first N builder fires after enable

struct GuardCam {
    float eye[3];
    float fwd[3], right[3], up[3];
    float yawDeg, pitchDeg;
    volatile bool valid;
};
GuardCam g_guardCam{};  // written each frame by HookedApplyCameraFX, read by VEH

using ApplyCameraFX_t = void(__fastcall*)(void* self,
                                          __m128* posInOut,
                                          __m128* quatInOut,
                                          float*  fovInOut);

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_menuSuppressed{false};
std::atomic<bool> g_installed{false};
std::atomic<bool> g_watcherStop{false};
std::thread       g_watcherThread;
ApplyCameraFX_t   g_origApplyFX = nullptr;

// Our cloned vtable lives in this DLL's writable data, not the game's.
alignas(16) void* g_clonedVTable[kClonedVTableSlots] = {};
void**            g_origVTable    = nullptr;  // game's read-only vtable
std::mutex        g_patchMutex;                // serializes instance patching

bool IsInModuleSection(uintptr_t addr,
                       uintptr_t modBase,
                       size_t    modSize) {
    return addr >= modBase && addr < modBase + modSize;
}

// Compose the game's clean quat with head tracking and write the result into
// `out` (x, y, z, w - same component order as Quat4 and AnvilNext's Vector4f).
// Axis mapping for AnvilNext (Z-up, Y-forward, RH):
//   yaw   -> rotation about +Z (up)
//   pitch -> rotation about +X (right)
//   roll  -> rotation about +Y (forward), sign-flipped
//
// worldYaw applies yaw about the world up axis (premultiply onto gameQ) so the
// horizon stays level at extreme camera pitch; otherwise yaw is applied in the
// camera-local frame alongside pitch and roll.
inline void ComposeHeadRotation(const float gameQ[4],
                                float yawDeg, float pitchDeg, float rollDeg,
                                bool worldYaw, float out[4]) {
    using cameraunlock::math::Quat4;

    constexpr float kDeg2Rad = 0.017453292519943295f;
    const float hy = yawDeg   * kDeg2Rad * 0.5f;
    const float hp = pitchDeg * kDeg2Rad * 0.5f;
    const float hr = -rollDeg * kDeg2Rad * 0.5f;

    const Quat4 qy(0.0f, 0.0f, sinf(hy), cosf(hy));
    const Quat4 qp(sinf(hp), 0.0f, 0.0f, cosf(hp));
    const Quat4 qr(0.0f, sinf(hr), 0.0f, cosf(hr));
    const Quat4 game(gameQ[0], gameQ[1], gameQ[2], gameQ[3]);

    const Quat4 result = worldYaw
        ? qy * (game * (qp * qr))   // worldYaw * gameQ * (pitch * roll), horizon-locked
        : game * (qy * qp * qr);    // gameQ * headLocal

    out[0] = result.x;
    out[1] = result.y;
    out[2] = result.z;
    out[3] = result.w;
}

inline float Dot3f(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

#ifdef ACUHT_DEBUG_FRUSTUM_SCAN
// Snapshot of the clean camera state, captured by the hook on arming and
// consumed by the worker thread (off the render hot path - the wide memory
// scan takes ~1s and must not stall the camera tick).
struct DbgSnapshot {
    void* self;
    float basis[3][3];  // rotated camera X/Y/Z basis (world space)
    float pos[3];       // clean camera world position
};
std::atomic<bool> g_dbgArm{false};       // hotkey -> hook
std::atomic<bool> g_dbgSnapReady{false}; // hook -> worker
DbgSnapshot       g_dbgSnap{};
std::thread       g_dbgThread;
std::atomic<bool> g_dbgThreadStop{false};

// Hardware-write-breakpoint probe: catch the instruction that writes a frustum.
uintptr_t          g_modBaseDbg = 0;     // set in InstallCameraHook
void*              g_hbpVeh = nullptr;
std::atomic<void*> g_hbpAddr{nullptr};   // address currently watched (DR0 write BP)
std::atomic<int>   g_hbpFireCount{0};

// Cull-volume probe: briefly arm a HW exec BP at FUN_141ce8a70 entry; the VEH
// captures RDX (param_2 = the SmallArray pointer to cull volumes) on the first
// fire, then the worker disarms it and arms a HW write BP on the volume array
// itself. The writer of that array each frame is the cull-volume builder.
constexpr uintptr_t kActorCullFuncEntryRva = 0x1CE8A70;
uintptr_t           g_actorCullFuncAddr = 0;
std::atomic<bool>   g_volumeProbeArmed{false};   // worker -> VEH: capture next fire
std::atomic<bool>   g_volumeProbeCaptured{false};// VEH -> worker: capture done
std::atomic<void*>  g_capturedArrayPtr{nullptr}; // first element of cull-volume array

// Forward declarations - definitions live later in the file.
void SetExecBpAllThreads(int drSlot, uintptr_t addr, bool enable);
void SetDr0AllThreads(void* addr, bool enable);

inline bool Float3Near(const float* m, const float v[3], float tol) {
    return fabsf(m[0] - v[0]) < tol &&
           fabsf(m[1] - v[1]) < tol &&
           fabsf(m[2] - v[2]) < tol;
}

inline bool IsReadable16(const void* p) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    DWORD pr = mbi.Protect & 0xFF;
    bool r = pr == PAGE_READONLY || pr == PAGE_READWRITE ||
             pr == PAGE_EXECUTE_READ || pr == PAGE_EXECUTE_READWRITE ||
             pr == PAGE_WRITECOPY || pr == PAGE_EXECUTE_WRITECOPY;
    return r && !(mbi.Protect & PAGE_GUARD);
}

// Dump the float layout around a confirmed view object, one 16-byte row per
// line, each annotated with the xyz length and the dot of its (normalized) xyz
// against the camera basis X/Y/Z. Plane normals show up as |xyz|~1 rows with a
// clear dot signature; the basis matrix and position are equally legible.
void DumpView(const DbgSnapshot& s, uint8_t* viewBase, long posRel) {
    Logger& log = Logger::Instance();
    log.Info("  -- dump @ viewBase=%p (camera pos at %s0x%lX) --", viewBase,
             posRel < 0 ? "-" : "+", posRel < 0 ? -posRel : posRel);
    for (long off = -0x40; off <= 0x600; off += 16) {
        const uint8_t* a = viewBase + off;
        if (!IsReadable16(a)) continue;
        const float* f = reinterpret_cast<const float*>(a);
        float n[3] = { f[0], f[1], f[2] };
        float len = sqrtf(Dot3f(n, n));
        const char* sgn = off < 0 ? "-" : "+";
        long ao = off < 0 ? -off : off;
        if (len > 1e-4f) {
            float inv = 1.0f / len;
            float u[3] = { n[0] * inv, n[1] * inv, n[2] * inv };
            log.Info("   %s0x%03lX: %11.4f %11.4f %11.4f %11.4f | L=%.3f dX=%+.2f dY=%+.2f dZ=%+.2f",
                     sgn, ao, f[0], f[1], f[2], f[3], len,
                     Dot3f(u, s.basis[0]), Dot3f(u, s.basis[1]), Dot3f(u, s.basis[2]));
        } else {
            log.Info("   %s0x%03lX: %11.4f %11.4f %11.4f %11.4f | L=%.3f",
                     sgn, ao, f[0], f[1], f[2], f[3], len);
        }
    }
}

// Fingerprint scan: walk every committed read/write region and flag structures
// that store BOTH the camera world position and one of its basis axes within a
// small window - that is a camera/view transform, and the cull frustum lives
// adjacent to it. This finds the renderer's scene-view object no matter how
// many pointer hops it sits from the camera component. Convention-agnostic; it
// does not assume which axis is forward.
void RunFingerprintScan(const DbgSnapshot& s) {
    constexpr float  kPosTol     = 0.05f;
    constexpr float  kAxisTol    = 0.02f;
    constexpr size_t kMaxRegion  = 128ull * 1024 * 1024;  // skip big buffers/textures
    constexpr int    kMaxViewLog = 50;   // VIEW? lines to log
    constexpr int    kDumpCap    = 0;    // VIEW? hits to dump in full
    constexpr int    kNearWindow = 256;  // bytes around a pos match to probe for a basis axis
    const char* kAxisName[3] = { "X", "Y", "Z" };

    Logger& log = Logger::Instance();
    log.Info("Fingerprint: self=%p pos=(%.3f,%.3f,%.3f) fwdY=(%.3f,%.3f,%.3f)",
             s.self, s.pos[0], s.pos[1], s.pos[2],
             s.basis[1][0], s.basis[1][1], s.basis[1][2]);

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const uint8_t* addr = static_cast<const uint8_t*>(si.lpMinimumApplicationAddress);
    const uint8_t* maxAddr = static_cast<const uint8_t*>(si.lpMaximumApplicationAddress);

    int viewHits = 0, posOnly = 0, dumped = 0;
    size_t bytesScanned = 0;
    MEMORY_BASIC_INFORMATION mbi;
    while (addr < maxAddr) {
        if (g_dbgThreadStop.load(std::memory_order_relaxed)) return;
        if (!VirtualQuery(addr, &mbi, sizeof(mbi))) break;
        uint8_t* regionBase = static_cast<uint8_t*>(mbi.BaseAddress);
        size_t regionSize = mbi.RegionSize;
        DWORD prot = mbi.Protect & 0xFF;
        bool rw = (prot == PAGE_READWRITE || prot == PAGE_WRITECOPY) &&
                  !(mbi.Protect & PAGE_GUARD);
        if (mbi.State == MEM_COMMIT && rw && regionSize <= kMaxRegion) {
            size_t last = regionSize - 12;
            for (size_t off = 0; off <= last; off += 4) {
                const float* m = reinterpret_cast<const float*>(regionBase + off);
                if (!Float3Near(m, s.pos, kPosTol)) continue;

                // pos match - probe nearby for any camera basis axis to confirm
                // it is a view transform rather than some other cached position.
                int axisFound = -1; long axisRel = 0;
                size_t lo = (off >= (size_t)kNearWindow) ? off - kNearWindow : 0;
                size_t hi = (off + kNearWindow <= regionSize - 12) ? off + kNearWindow
                                                                   : regionSize - 12;
                for (size_t q = lo; q <= hi && axisFound < 0; q += 4) {
                    const float* a = reinterpret_cast<const float*>(regionBase + q);
                    for (int ax = 0; ax < 3; ++ax) {
                        if (Float3Near(a, s.basis[ax], kAxisTol)) {
                            axisFound = ax; axisRel = (long)q - (long)off; break;
                        }
                    }
                }
                if (axisFound < 0) { ++posOnly; continue; }

                if (viewHits < kMaxViewLog) {
                    log.Info("Fingerprint VIEW?: %p (region %p)  pos here, basis-%s at %+ld",
                             regionBase + off, regionBase, kAxisName[axisFound], axisRel);
                }
                ++viewHits;
                if (dumped < kDumpCap) {
                    // viewBase anchored at the matched basis axis; pos sits at -axisRel.
                    DumpView(s, regionBase + off + axisRel, -axisRel);
                    ++dumped;
                }
            }
            bytesScanned += regionSize;
        }
        addr = regionBase + regionSize;
    }
    log.Info("Fingerprint: complete, %d VIEW? hit(s), %d pos-only, %.0f MB scanned",
             viewHits, posOnly, bytesScanned / (1024.0 * 1024.0));
}

// Global hunt for a stored world-space view frustum. The decisive test is
// camera-specific: a frustum's 4 side planes all pass through the eye (the
// apex) and the near plane is ~0 from it too, so >= 4 of the 6 planes satisfy
// |d + n.eye| ~ 0. Generic normalized-vector buffers (matrices, bone data)
// have d-terms that do NOT pass through our specific eye point, so they are
// rejected. Also require >= 1 plane (anti)parallel to fwd (near/far) and the
// normals to not all be parallel. Dedupes shifted re-hits. Returns the address
// of the strongest hit (highest thruEye) for the write-breakpoint probe.
void* RunPlaneScan(const DbgSnapshot& s) {
    constexpr size_t kStride    = 16;
    constexpr int    kWin       = 6;
    constexpr size_t kMaxRegion = 128ull * 1024 * 1024;
    constexpr int    kMaxHits   = 40;
    constexpr float  kEyeEps    = 3.0f;  // metres: plane passes through eye
    constexpr int    kMinThruEye = 4;

    const float* fwd = s.basis[1];
    Logger& log = Logger::Instance();
    log.Info("PlaneScan: fwd=(%.3f,%.3f,%.3f) pos=(%.3f,%.3f,%.3f)",
             fwd[0], fwd[1], fwd[2], s.pos[0], s.pos[1], s.pos[2]);

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const uint8_t* addr = static_cast<const uint8_t*>(si.lpMinimumApplicationAddress);
    const uint8_t* maxAddr = static_cast<const uint8_t*>(si.lpMaximumApplicationAddress);

    int hits = 0, bestThruEye = 0;
    void* bestAddr = nullptr;
    MEMORY_BASIC_INFORMATION mbi;
    while (addr < maxAddr && hits < kMaxHits) {
        if (g_dbgThreadStop.load(std::memory_order_relaxed)) return bestAddr;
        if (!VirtualQuery(addr, &mbi, sizeof(mbi))) break;
        uint8_t* regionBase = static_cast<uint8_t*>(mbi.BaseAddress);
        size_t regionSize = mbi.RegionSize;
        DWORD prot = mbi.Protect & 0xFF;
        bool rw = (prot == PAGE_READWRITE || prot == PAGE_WRITECOPY) &&
                  !(mbi.Protect & PAGE_GUARD);
        if (mbi.State == MEM_COMMIT && rw && regionSize <= kMaxRegion &&
            regionSize >= kWin * kStride) {
            size_t last = regionSize - kWin * kStride;
            for (size_t off = 0; off <= last; off += kStride) {
                int thruEye = 0, fwdAligned = 0, nonParallel = 0;
                bool ok = true;
                float firstU[3] = { 0, 0, 0 };
                for (int pl = 0; pl < kWin; ++pl) {
                    const float* f = reinterpret_cast<const float*>(
                        regionBase + off + pl * kStride);
                    float n[3] = { f[0], f[1], f[2] };
                    float l = sqrtf(Dot3f(n, n));
                    if (!(l > 1e-3f && l < 1e4f)) { ok = false; break; }
                    float inv = 1.0f / l;
                    float u[3] = { n[0] * inv, n[1] * inv, n[2] * inv };
                    if (pl == 0) { firstU[0] = u[0]; firstU[1] = u[1]; firstU[2] = u[2]; }
                    else if (fabsf(Dot3f(u, firstU)) < 0.98f) ++nonParallel;
                    if (fabsf(Dot3f(u, fwd)) > 0.90f) ++fwdAligned;
                    if (fabsf(f[3] / l + Dot3f(u, s.pos)) < kEyeEps) ++thruEye;
                }
                if (!ok || thruEye < kMinThruEye || fwdAligned < 1 || nonParallel < 2)
                    continue;

                log.Info("PlaneScan HIT: %p (region %p)  thruEye=%d fwdAligned=%d",
                         regionBase + off, regionBase, thruEye, fwdAligned);
                for (int pl = 0; pl < kWin; ++pl) {
                    const float* f = reinterpret_cast<const float*>(
                        regionBase + off + pl * kStride);
                    float n[3] = { f[0], f[1], f[2] };
                    float l = sqrtf(Dot3f(n, n));
                    float dF = (l > 1e-6f) ? Dot3f(n, fwd) / l : 0.0f;
                    float eyeR = (l > 1e-6f) ? f[3] / l + Dot3f(n, s.pos) / l : 0.0f;
                    log.Info("    pl[%d] n=(%.4f,%.4f,%.4f) d=%.3f L=%.3f dF=%+.2f eyeR=%.2f",
                             pl, f[0], f[1], f[2], f[3], l, dF, eyeR);
                }
                if (thruEye > bestThruEye) {
                    bestThruEye = thruEye;
                    bestAddr = regionBase + off;
                }
                ++hits;
                off += (kWin - 1) * kStride;  // dedupe shifted re-hits
            }
        }
        addr = regionBase + regionSize;
    }
    log.Info("PlaneScan: complete, %d hit(s), best=%p thruEye=%d",
             hits, bestAddr, bestThruEye);
    return bestAddr;
}

// VEH: when the engine writes the watched frustum address, the CPU raises a
// debug single-step exception with DR6 bit0 set. Log the writer's RIP (and its
// module RVA - the prize for Ghidra), then leave the breakpoint armed for a few
// more fires to catch all the instructions of the build loop.
LONG CALLBACK HbpVeh(PEXCEPTION_POINTERS ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    if ((ep->ContextRecord->Dr6 & 0x1) == 0)
        return EXCEPTION_CONTINUE_SEARCH;

    DWORD64 rip = ep->ContextRecord->Rip;
    int n = g_hbpFireCount.fetch_add(1) + 1;
    if (n <= 30) {
        Logger::Instance().Info("HBP FIRE #%d: writer RIP=%p  RVA=+0x%llX",
                                n, reinterpret_cast<void*>(rip),
                                static_cast<unsigned long long>(rip - g_modBaseDbg));
    }
    ep->ContextRecord->Dr6 = 0;
    if (n >= 30) ep->ContextRecord->Dr7 &= ~static_cast<DWORD64>(1);  // disarm DR0 (this thread)
    return EXCEPTION_CONTINUE_EXECUTION;
}

void SetDr0AllThreads(void* addr, bool enable) {
    DWORD pid = GetCurrentProcessId();
    DWORD me = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{}; te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            if (te.th32ThreadID == me) continue;  // never suspend the worker itself
            HANDLE h = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
                                  THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (!h) continue;
            SuspendThread(h);
            CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(h, &ctx)) {
                ctx.Dr7 &= ~static_cast<DWORD64>((0xF << 16) | 0x3);  // clear RW0/LEN0 + L0/G0
                if (enable) {
                    ctx.Dr0 = reinterpret_cast<DWORD64>(addr);
                    ctx.Dr7 |= 0x1;          // L0 enable
                    ctx.Dr7 |= (0x1 << 16);  // RW0 = 01 (break on data write)
                    ctx.Dr7 |= (0x3 << 18);  // LEN0 = 11 (4 bytes)
                } else {
                    ctx.Dr0 = 0;
                }
                ctx.Dr6 = 0;
                SetThreadContext(h, &ctx);
            }
            ResumeThread(h);
            CloseHandle(h);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

// Find an entity-shaped struct near the camera. BaseEntity layout (per ACUFixes
// RE): mainTransform (4 unit-basis rows) at +0x20..+0x4F, position at +0x50,
// World* at +0x60, EntityFlags at +0x88 (incl. IsHiddenByUserCullingPlanes).
void* FindNearbyEntity(const DbgSnapshot& s) {
    constexpr float  kPosTol    = 8.0f;
    constexpr size_t kMaxRegion = 128ull * 1024 * 1024;
    constexpr int    kMaxLog    = 8;
    Logger& log = Logger::Instance();
    log.Info("EntityScan: searching near eye=(%.2f,%.2f,%.2f) tol=%.1fm",
             s.pos[0], s.pos[1], s.pos[2], kPosTol);

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const uint8_t* addr = static_cast<const uint8_t*>(si.lpMinimumApplicationAddress);
    const uint8_t* maxAddr = static_cast<const uint8_t*>(si.lpMaximumApplicationAddress);

    int matches = 0;
    void* bestAddr = nullptr;
    MEMORY_BASIC_INFORMATION mbi;
    while (addr < maxAddr) {
        if (g_dbgThreadStop.load(std::memory_order_relaxed)) return bestAddr;
        if (!VirtualQuery(addr, &mbi, sizeof(mbi))) break;
        uint8_t* regionBase = static_cast<uint8_t*>(mbi.BaseAddress);
        size_t regionSize = mbi.RegionSize;
        DWORD prot = mbi.Protect & 0xFF;
        bool rw = (prot == PAGE_READWRITE || prot == PAGE_WRITECOPY) &&
                  !(mbi.Protect & PAGE_GUARD);
        if (mbi.State == MEM_COMMIT && rw && regionSize <= kMaxRegion &&
            regionSize >= 0x100) {
            // Entity heap is allocated by the game's allocator, in the
            // 0x10000000..0x00007FF000000000 range. DLLs/modules sit above this
            // (0x00007FFA...). Bail on those regions entirely.
            uintptr_t rb = reinterpret_cast<uintptr_t>(regionBase);
            if (rb < 0x10000000ull || rb > 0x00007FF000000000ull) {
                addr = regionBase + regionSize;
                continue;
            }
            // ACU.exe is ~120MB at modBase 0x140000000; entity vtables sit in
            // its .rdata section in [modBase, modBase + 0x10000000) range.
            uintptr_t modLo = g_modBaseDbg;
            uintptr_t modHi = g_modBaseDbg + 0x10000000ull;
            size_t last = regionSize - 0x100;
            // Entities are 16-byte aligned heap objects. Stepping by 16 (not 8)
            // halves the scan iterations.
            for (size_t off = 0; off <= last; off += 16) {
                // Cheap, decisive first filter: real polymorphic objects begin
                // with a vtable pointer into ACU.exe's read-only section.
                // Cached transforms etc. have something else there.
                uintptr_t vptr = *reinterpret_cast<uintptr_t*>(regionBase + off);
                if (vptr < modLo || vptr >= modHi) continue;
                if ((vptr & 7) != 0) continue;

                const float* pos = reinterpret_cast<const float*>(regionBase + off + 0x50);
                // Reject NaN/Inf and absurd magnitudes. NaN comparisons are
                // false, so finite() check must come first.
                if (!isfinite(pos[0]) || !isfinite(pos[1]) || !isfinite(pos[2])) continue;
                if (fabsf(pos[0]) > 10000.0f || fabsf(pos[1]) > 10000.0f ||
                    fabsf(pos[2]) > 10000.0f) continue;
                float dx = pos[0] - s.pos[0];
                float dy = pos[1] - s.pos[1];
                float dz = pos[2] - s.pos[2];
                if (dx*dx + dy*dy + dz*dz > kPosTol*kPosTol) continue;

                const float* m0 = reinterpret_cast<const float*>(regionBase + off + 0x20);
                const float* m1 = reinterpret_cast<const float*>(regionBase + off + 0x30);
                const float* m2 = reinterpret_cast<const float*>(regionBase + off + 0x40);
                if (!isfinite(m0[0]) || !isfinite(m0[1]) || !isfinite(m0[2])) continue;
                if (!isfinite(m1[0]) || !isfinite(m1[1]) || !isfinite(m1[2])) continue;
                if (!isfinite(m2[0]) || !isfinite(m2[1]) || !isfinite(m2[2])) continue;
                float l0 = sqrtf(m0[0]*m0[0]+m0[1]*m0[1]+m0[2]*m0[2]);
                float l1 = sqrtf(m1[0]*m1[0]+m1[1]*m1[1]+m1[2]*m1[2]);
                float l2 = sqrtf(m2[0]*m2[0]+m2[1]*m2[1]+m2[2]*m2[2]);
                if (fabsf(l0 - 1.0f) > 0.02f || fabsf(l1 - 1.0f) > 0.02f ||
                    fabsf(l2 - 1.0f) > 0.02f) continue;
                // Real transforms have orthogonal basis vectors. Reject if not.
                float d01 = m0[0]*m1[0]+m0[1]*m1[1]+m0[2]*m1[2];
                float d02 = m0[0]*m2[0]+m0[1]*m2[1]+m0[2]*m2[2];
                float d12 = m1[0]*m2[0]+m1[1]*m2[1]+m1[2]*m2[2];
                if (fabsf(d01) > 0.05f || fabsf(d02) > 0.05f || fabsf(d12) > 0.05f) continue;

                uintptr_t worldPtr = *reinterpret_cast<uintptr_t*>(regionBase + off + 0x60);
                // World must be in heap range, not DLL/module space.
                if (worldPtr < 0x10000000ull || worldPtr > 0x00007FF000000000ull) continue;
                if ((worldPtr & 7) != 0) continue;  // 8-byte aligned
                if (!IsReadable16(reinterpret_cast<void*>(worldPtr))) continue;

                void* ent = regionBase + off;
                uint64_t flags = *reinterpret_cast<uint64_t*>(regionBase + off + 0x88);
                ++matches;
                if (matches <= kMaxLog) {
                    log.Info("EntityScan HIT: %p  pos=(%.2f,%.2f,%.2f) world=%p flags=0x%016llX",
                             ent, pos[0], pos[1], pos[2],
                             reinterpret_cast<void*>(worldPtr),
                             static_cast<unsigned long long>(flags));
                }
                if (!bestAddr) bestAddr = ent;
                if (matches >= 50) break;
            }
            if (matches >= 50) break;
        }
        addr = regionBase + regionSize;
    }
    log.Info("EntityScan: complete, %d matches, best=%p (flags@+0x88)",
             matches, bestAddr);
    return bestAddr;
}

void DbgWorkerThread() {
    while (!g_dbgThreadStop.load(std::memory_order_relaxed)) {
        // Ctrl+Shift+U -> capture the cull-volume array pointer next time the
        // actor cull function is entered. Briefly arm DR3 at FUN_141ce8a70.
        if (g_dbgSnapReady.exchange(false)) {
            if (!g_hbpVeh) g_hbpVeh = AddVectoredExceptionHandler(1, HbpVeh);
            g_actorCullFuncAddr = g_modBaseDbg + kActorCullFuncEntryRva;
            g_capturedArrayPtr.store(nullptr);
            g_volumeProbeCaptured.store(false);
            g_volumeProbeArmed.store(true);
            SetExecBpAllThreads(3, g_actorCullFuncAddr, true);
            Logger::Instance().Info(
                "Cull-volume probe armed: DR3 at FUN_141ce8a70 +0x%llX. Move briefly...",
                static_cast<unsigned long long>(kActorCullFuncEntryRva));
        }

        // Once VEH captured the array pointer, disarm DR3 and arm a DR0 write
        // BP on the volume array's first element. Any write to it is the
        // cull-volume builder we are hunting.
        if (g_volumeProbeCaptured.exchange(false)) {
            void* arr = g_capturedArrayPtr.load();
            g_volumeProbeArmed.store(false);
            SetExecBpAllThreads(3, g_actorCullFuncAddr, false);
            if (arr) {
                Logger::Instance().Info("Cull-volume probe: captured array=%p; arming write BP", arr);
                g_hbpFireCount.store(0);
                g_hbpAddr.store(arr);
                SetDr0AllThreads(arr, true);
                Logger::Instance().Info(
                    "HBP armed on cull-volume array %p - play a few seconds; check HBP FIRE lines",
                    arr);
            } else {
                Logger::Instance().Info("Cull-volume probe: capture returned null array - skipped");
            }
        }
        Sleep(50);
    }
}
#endif // ACUHT_DEBUG_FRUSTUM_SCAN

// Push every side plane of the just-built frustum (6 Vec4 planes at base+0..0x50)
// outward by a huge constant. No rotation, no head-deflection scaling, no per-
// frustum match-or-skip cleverness - those iterations all made things worse.
//
// Approach: identify near/far via the antiparallel pair every perspective
// frustum has, treat the remaining 4 as sides, and slide each side outward by
// kBias metres at the eye. Near and far are left untouched so close-clipping
// and draw distance are unchanged; only the lateral aperture grows.
//
// Sign convention is detected from the far plane: the far plane has the eye
// well inside, so sign(n_far . eye + d_far) is the sign of "inside the
// frustum". Side planes get that sign added to d so the eye becomes kBias
// metres further inside, which slides the plane outward perpendicular to
// itself by kBias.
void WidenCullFrustum(float* base) {
    // Only eye[] and valid are consumed below; read them directly to avoid
    // copying the full GuardCam struct on every fire (the dead fields - fwd/
    // right/up/yawDeg/pitchDeg - exist for future use but are unused here).
    if (!g_guardCam.valid) return;
    const float eyeX = g_guardCam.eye[0];
    const float eyeY = g_guardCam.eye[1];
    const float eyeZ = g_guardCam.eye[2];

    int fireIdx = g_guardFireLogCount.fetch_add(1);
    bool logFire = fireIdx < 20;

    // Precompute inverse lengths once. Previously sqrtf was recomputed across
    // the pair-finding, thru-check, and widening loops - up to ~30 sqrts per
    // call. Now exactly 6.
    float Lp[6];
    float invL[6];
    bool  ok[6];
    for (int i = 0; i < 6; ++i) {
        const float* ni = base + i * 4;
        float L2 = Dot3f(ni, ni);
        if (L2 < 1e-6f) { Lp[i] = 0.0f; invL[i] = 0.0f; ok[i] = false; }
        else            { float L = sqrtf(L2); Lp[i] = L; invL[i] = 1.0f / L; ok[i] = true; }
    }

    int nearIdx = -1, farIdx = -1;
    for (int i = 0; i < 6 && nearIdx < 0; ++i) {
        if (!ok[i]) continue;
        const float* ni = base + i * 4;
        float invI = invL[i];
        for (int j = i + 1; j < 6; ++j) {
            if (!ok[j]) continue;
            const float* nj = base + j * 4;
            float invJ = invL[j];
            float dotIJ = (ni[0]*nj[0] + ni[1]*nj[1] + ni[2]*nj[2]) * invI * invJ;
            if (dotIJ < -0.95f) {
                float eyeRi = (ni[0]*eyeX + ni[1]*eyeY + ni[2]*eyeZ + ni[3]) * invI;
                float eyeRj = (nj[0]*eyeX + nj[1]*eyeY + nj[2]*eyeZ + nj[3]) * invJ;
                if (fabsf(eyeRi) < fabsf(eyeRj)) { nearIdx = i; farIdx = j; }
                else                              { nearIdx = j; farIdx = i; }
                break;
            }
        }
    }
    if (nearIdx < 0) {
        if (logFire) Logger::Instance().Info("Fire #%d: base=%p SKIP no-near/far", fireIdx, base);
        return;
    }

    // Confirm this is a camera-apex frustum (3+ side planes pass through the
    // eye) - rejects ortho/shadow/HUD frustums that share the 6-plane layout
    // but don't share the apex-at-eye property.
    int thru = 0;
    for (int p = 0; p < 6; ++p) {
        if (p == nearIdx || p == farIdx) continue;
        if (!ok[p]) continue;
        const float* pl = base + p * 4;
        float resid = (pl[0]*eyeX + pl[1]*eyeY + pl[2]*eyeZ + pl[3]) * invL[p];
        if (fabsf(resid) < 2.0f) ++thru;
    }
    if (logFire) {
        Logger::Instance().Info("Fire #%d: base=%p near=%d far=%d thru=%d %s",
                                fireIdx, base, nearIdx, farIdx, thru,
                                thru < 3 ? "SKIP not-main-apex" : "WIDEN");
    }
    if (thru < 3) return;

    if (!ok[farIdx]) return;
    const float* nFar = base + farIdx * 4;
    float eyeRfar = (nFar[0]*eyeX + nFar[1]*eyeY + nFar[2]*eyeZ + nFar[3]) * invL[farIdx];
    float insideSign = eyeRfar >= 0.0f ? 1.0f : -1.0f;

    for (int p = 0; p < 6; ++p) {
        if (p == nearIdx || p == farIdx) continue;
        if (!ok[p]) continue;
        float* pl = base + p * 4;
        pl[3] += insideSign * g_guardBiasMeters * Lp[p];
    }
}

// VEH for the guard-band execute breakpoint (DR1). Fires before the breakpointed
// instruction; we widen the frustum, then set EFlags.RF so the instruction runs
// once without immediately re-triggering, leaving the breakpoint armed.
LONG CALLBACK GuardVeh(PEXCEPTION_POINTERS ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;

    DWORD64 dr6 = ep->ContextRecord->Dr6;
    DWORD64 rip = ep->ContextRecord->Rip;

    // DR1: frustum guard band - fired before the FUN_14223bd70 derived-data loop.
    if ((dr6 & 0x2) && rip == g_guardInjectAddr) {
        if (g_guardEnabled.load(std::memory_order_relaxed)) {
            float* frustumBase = reinterpret_cast<float*>(ep->ContextRecord->Rcx - 0xc);
            WidenCullFrustum(frustumBase);
        }
        ep->ContextRecord->Dr6 = 0;
        ep->ContextRecord->EFlags |= 0x10000;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // DR2: actor-cull bit-4 suppressor (kept wired but currently not armed -
    // see InstallCameraHook comment for why).
    if ((dr6 & 0x4) && rip == g_actorCullInstrAddr) {
        if (g_guardEnabled.load(std::memory_order_relaxed)) {
            ep->ContextRecord->Rax &= ~static_cast<DWORD64>(0x10);
        }
        ep->ContextRecord->Dr6 = 0;
        ep->ContextRecord->EFlags |= 0x10000;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

#ifdef ACUHT_DEBUG_FRUSTUM_SCAN
    // DR3: cull-volume probe - one-shot capture of param_2 (RDX) at the actor
    // cull function's entry. The worker disarms DR3 and rearms DR0 once we
    // have the array pointer.
    if ((dr6 & 0x8) && rip == g_actorCullFuncAddr) {
        if (g_volumeProbeArmed.load(std::memory_order_relaxed) &&
            !g_volumeProbeCaptured.load(std::memory_order_relaxed)) {
            // param_2 in RDX. SmallArray: *param_2 = array head, [+0xa] = uint16 size.
            DWORD64 param2 = ep->ContextRecord->Rdx;
            void* arrayHead = *reinterpret_cast<void**>(param2);
            g_capturedArrayPtr.store(arrayHead);
            g_volumeProbeCaptured.store(true);
        }
        ep->ContextRecord->Dr6 = 0;
        ep->ContextRecord->EFlags |= 0x10000;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
#endif

    return EXCEPTION_CONTINUE_SEARCH;
}

// Arm/disarm a HW execute breakpoint on every thread in the process using the
// given DR slot (1 or 2). Re-applied periodically by the watcher so that
// threads created after install are covered.
void SetExecBpAllThreads(int drSlot, uintptr_t addr, bool enable) {
    if (drSlot < 1 || drSlot > 3) return;  // we use DR1 (frustum), DR2 (unused), DR3 (probe)
    int lShift = drSlot * 2;             // L1 at bit 2, L2 at 4, L3 at 6
    int rwShift = 16 + drSlot * 4;        // RW/LEN block: 20..23 / 24..27 / 28..31
    DWORD64 cfgMask = static_cast<DWORD64>(0xF) << rwShift;  // clears both RW_n and LEN_n
    DWORD64 lMask = static_cast<DWORD64>(0x3) << lShift;     // clears L_n and G_n

    DWORD pid = GetCurrentProcessId();
    DWORD me = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{}; te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            if (te.th32ThreadID == me) continue;
            HANDLE h = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
                                  THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (!h) continue;
            SuspendThread(h);
            CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(h, &ctx)) {
                ctx.Dr7 &= ~(lMask | cfgMask);
                if (drSlot == 1)      ctx.Dr1 = enable ? addr : 0;
                else if (drSlot == 2) ctx.Dr2 = enable ? addr : 0;
                else                  ctx.Dr3 = enable ? addr : 0;
                if (enable) {
                    ctx.Dr7 |= (static_cast<DWORD64>(0x1) << lShift);  // L_n
                    // RW_n = 00 (execute), LEN_n = 00 (1 byte) - all zero, already cleared.
                }
                ctx.Dr6 = 0;
                SetThreadContext(h, &ctx);
            }
            ResumeThread(h);
            CloseHandle(h);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

// Convenience wrappers preserving the original DR1-only call sites for the
// frustum guard band.
void SetGuardBpAllThreads(uintptr_t addr, bool enable) {
    SetExecBpAllThreads(1, addr, enable);
}
void SetActorCullBpAllThreads(uintptr_t addr, bool enable) {
    SetExecBpAllThreads(2, addr, enable);
}

void __fastcall HookedApplyCameraFX(void* self,
                                    __m128* posInOut,
                                    __m128* quatInOut,
                                    float*  fovInOut) {
    g_origApplyFX(self, posInOut, quatInOut, fovInOut);

#ifdef ACUHT_DEBUG_FRUSTUM_SCAN
    if (quatInOut && g_dbgArm.exchange(false)) {
        const float* q = reinterpret_cast<const float*>(quatInOut);
        cameraunlock::math::Quat4 cq(q[0], q[1], q[2], q[3]);
        cameraunlock::math::Vec3 bx = cq.Rotate(cameraunlock::math::Vec3(1, 0, 0));
        cameraunlock::math::Vec3 by = cq.Rotate(cameraunlock::math::Vec3(0, 1, 0));
        cameraunlock::math::Vec3 bz = cq.Rotate(cameraunlock::math::Vec3(0, 0, 1));
        g_dbgSnap.self = self;
        g_dbgSnap.basis[0][0] = bx.x; g_dbgSnap.basis[0][1] = bx.y; g_dbgSnap.basis[0][2] = bx.z;
        g_dbgSnap.basis[1][0] = by.x; g_dbgSnap.basis[1][1] = by.y; g_dbgSnap.basis[1][2] = by.z;
        g_dbgSnap.basis[2][0] = bz.x; g_dbgSnap.basis[2][1] = bz.y; g_dbgSnap.basis[2][2] = bz.z;
        g_dbgSnap.pos[0] = g_dbgSnap.pos[1] = g_dbgSnap.pos[2] = 0.0f;
        if (posInOut) {
            const float* pp = reinterpret_cast<const float*>(posInOut);
            g_dbgSnap.pos[0] = pp[0]; g_dbgSnap.pos[1] = pp[1]; g_dbgSnap.pos[2] = pp[2];
        }
        g_dbgSnapReady.store(true, std::memory_order_release);
    }
#endif

    if (!g_enabled.load(std::memory_order_relaxed))        return;
    if (g_menuSuppressed.load(std::memory_order_relaxed))  return;
    if (!quatInOut)                                        return;

    Mod& mod = Mod::Instance();

    // Always run the rotation pipeline so the cached pose is fresh for the
    // position pivot compensation, even when we don't render head rotation.
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    bool haveRotation = mod.GetProcessedRotation(yaw, pitch, roll);

    float* qPtr = reinterpret_cast<float*>(quatInOut);
    float gameQ[4] = { qPtr[0], qPtr[1], qPtr[2], qPtr[3] };

    // Refresh the guard-band camera snapshot from the CLEAN state. The frustum
    // builder runs earlier in the frame than this hook, so the VEH reads the
    // previous frame's snapshot - a one-frame lag is immaterial for a guard
    // band. WidenCullFrustum only reads eye[] and valid; the fwd/right/up and
    // yawDeg/pitchDeg fields exist on GuardCam for potential future use but
    // are not currently consumed, so we skip the 3 Quat4::Rotate calls those
    // writes would require - that's the hottest math in this hook.
    if (posInOut) {
        const float* pp = reinterpret_cast<const float*>(posInOut);
        g_guardCam.eye[0] = pp[0]; g_guardCam.eye[1] = pp[1]; g_guardCam.eye[2] = pp[2];
    }
    g_guardCam.valid = true;

    // Position offset is applied in the clean (pre-head-rotation) camera frame
    // so it follows the body, not the head-tracked view. The processed offset
    // is tracker-local (x=right, y=up, +z=back); AnvilNext camera-local axes are
    // X=right, Y=forward, Z=up. First-pass mapping - verify signs in-game and
    // correct via the position invert flags.
    float ox = 0.0f, oy = 0.0f, oz = 0.0f;
    if (posInOut && mod.GetPositionOffset(ox, oy, oz)) {
        cameraunlock::math::Quat4 cleanQ(gameQ[0], gameQ[1], gameQ[2], gameQ[3]);
        cameraunlock::math::Vec3 localOffset(ox, -oz, oy);
        cameraunlock::math::Vec3 worldOffset = cleanQ.Rotate(localOffset);

        float* pPtr = reinterpret_cast<float*>(posInOut);
        pPtr[0] += worldOffset.x;
        pPtr[1] += worldOffset.y;
        pPtr[2] += worldOffset.z;
    }

    if (haveRotation && mod.IsRotationActive()) {
        float finalQ[4];
        ComposeHeadRotation(gameQ, yaw, pitch, roll, mod.IsWorldSpaceYaw(), finalQ);
        qPtr[0] = finalQ[0];
        qPtr[1] = finalQ[1];
        qPtr[2] = finalQ[2];
        qPtr[3] = finalQ[3];
    }
}

// Build the cloned vtable from the game's read-only vtable. Idempotent.
bool EnsureClonedVTable(void** gameVTable) {
    std::lock_guard<std::mutex> lock(g_patchMutex);
    if (g_origVTable == gameVTable) return true;  // already cloned

    if (g_origVTable && g_origVTable != gameVTable) {
        Logger::Instance().Warning(
            "Camera hook: encountered second distinct vtable (%p vs prior %p) - "
            "patching it onto our existing clone anyway.",
            gameVTable, g_origVTable);
    }

    for (int i = 0; i < kClonedVTableSlots; ++i) {
        g_clonedVTable[i] = gameVTable[i];
    }
    g_origApplyFX = reinterpret_cast<ApplyCameraFX_t>(gameVTable[kApplyFXSlotIndex]);
    g_clonedVTable[kApplyFXSlotIndex] = reinterpret_cast<void*>(&HookedApplyCameraFX);
    g_origVTable = gameVTable;
    return true;
}

// Walk the CameraManager's SmallArray and overwrite each instance's vptr to
// point at our cloned vtable. Idempotent (skips instances already swapped).
// Returns the count of instances newly patched.
int PatchAllInstances(uintptr_t modBase, size_t modSize) {
    auto* singletonSlot = reinterpret_cast<void**>(kCameraManagerSingletonAddr);
    void* camMgr = *singletonSlot;
    if (!camMgr) return 0;

    void*** arrPtr   = reinterpret_cast<void***>(reinterpret_cast<uint8_t*>(camMgr) + kCMOffArrToPlayerCam);
    uint16_t arrSize = *reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(camMgr) + kCMOffArrSize);
    void** entries   = *arrPtr;
    if (!entries || arrSize == 0) return 0;

    int patched = 0;
    void* ourClone = reinterpret_cast<void*>(&g_clonedVTable[0]);

    for (uint16_t i = 0; i < arrSize; ++i) {
        void* inst = entries[i];
        if (!inst) continue;
        void** vptrSlot = reinterpret_cast<void**>(inst);
        void* currentVT = *vptrSlot;
        if (currentVT == ourClone) continue;

        uintptr_t vtAddr = reinterpret_cast<uintptr_t>(currentVT);
        if (!IsInModuleSection(vtAddr, modBase, modSize)) {
            Logger::Instance().Warning(
                "Camera hook: instance %p has vtable %p outside ACU.exe - skipping.",
                inst, currentVT);
            continue;
        }

        if (!EnsureClonedVTable(static_cast<void**>(currentVT))) continue;

        // Atomic-ish: write a pointer-sized value. Aligned 8-byte writes are
        // atomic on x64, so the game cannot observe a torn vptr.
        *vptrSlot = ourClone;
        ++patched;
        Logger::Instance().Info(
            "Camera hook: vptr-swapped instance %p (orig vtable=%p, slot[%d]=%p, rva=+0x%llX)",
            inst, currentVT, kApplyFXSlotIndex, g_origApplyFX,
            static_cast<unsigned long long>(
                reinterpret_cast<uintptr_t>(g_origApplyFX) - modBase));
    }
    return patched;
}

// True when a full-screen menu page is on screen (title screen, pause,
// inventory, map). A null MenuManager means the UI subsystem hasn't been
// constructed yet - that only happens at boot/load, which is also not
// gameplay, so it counts as suppressed.
bool IsAnyMenuOpen() {
    void* menuMgr = *reinterpret_cast<void**>(kMenuManagerSingletonAddr);
    if (!menuMgr) return true;

    void* pagesStack = *reinterpret_cast<void**>(
        reinterpret_cast<uint8_t*>(menuMgr) + kMMOffMenuPagesStack);
    if (!pagesStack) return false;

    uint16_t pageCount = *reinterpret_cast<uint16_t*>(
        reinterpret_cast<uint8_t*>(pagesStack) +
        kMM30OffArrHasMenuPages + kSmallArraySizeOffset);
    return pageCount > 0;
}

void WatcherThread(uintptr_t modBase, size_t modSize) {
    constexpr DWORD kPollInterval = 250;  // ms
    int totalPatched = 0;
    int loops = 0;
    int waitLogs = 0;
    while (!g_watcherStop.load(std::memory_order_relaxed)) {
        int p = PatchAllInstances(modBase, modSize);
        if (p > 0) {
            totalPatched += p;
            if (totalPatched == p) g_installed.store(true);
        }
        // Capped: this polls every 250ms and the state it reports does not
        // change on its own, so left uncapped it becomes the bulk of the log a
        // user is asked to send after reporting no head tracking.
        if (++loops % 20 == 0 && totalPatched == 0 && waitLogs < 6) {
            ++waitLogs;
            Logger::Instance().Info(
                "Camera hook: waiting for player camera instance...%s",
                waitLogs == 6 ? " (last of these; still waiting silently)" : "");
        }

        bool menuOpen = IsAnyMenuOpen();
        if (g_menuSuppressed.exchange(menuOpen) != menuOpen) {
            Logger::Instance().Info(
                "Camera hook: tracking %s (menu %s)",
                menuOpen ? "suppressed" : "active",
                menuOpen ? "opened" : "closed");
        }

        // Re-arm the guard breakpoints: covers threads created since install.
        if (g_guardInjectAddr) SetGuardBpAllThreads(g_guardInjectAddr, true);
        if (g_actorCullInstrAddr) SetActorCullBpAllThreads(g_actorCullInstrAddr, true);

        Sleep(kPollInterval);
    }
}

} // anonymous namespace

bool InstallCameraHook() {
    if (g_watcherThread.joinable()) return true;

    HMODULE gameModule = GetModuleHandleA(ACU_GAME_EXE);
    if (!gameModule) {
        Logger::Instance().Error(
            "Camera hook: GetModuleHandleA(\"%s\") returned null. "
            "This DLL was loaded into a process that does not have ACU.exe mapped.",
            ACU_GAME_EXE);
        return false;
    }

    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), gameModule, &mi, sizeof(mi))) {
        Logger::Instance().Error("Camera hook: GetModuleInformation failed (err=%lu)",
                                 GetLastError());
        return false;
    }
    const uintptr_t modBase = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
    const size_t    modSize = mi.SizeOfImage;

    // Build fingerprint gate: every offset this hook touches is pinned to
    // ACU v1.5.0. On any other build, stay dormant - dereferencing the
    // singleton slots or arming breakpoints at these RVAs in a different
    // binary is memory corruption, not head tracking.
    cameraunlock::memory::PeFingerprint running{};
    if (!cameraunlock::memory::ReadPeFingerprint(gameModule, running)) {
        Logger::Instance().Error(
            "Camera hook: could not read ACU.exe PE header - staying dormant.");
        return false;
    }
    Logger::Instance().Info(
        "Camera hook: ACU.exe fingerprint ts=0x%08X size=0x%08X csum=0x%08X",
        running.TimeDateStamp, running.SizeOfImage, running.CheckSum);
    if (!running.Matches(kAcuV150Fingerprint)) {
        const auto kind = cameraunlock::memory::ClassifyMismatch(running, kAcuV150Fingerprint);
        const char* hint =
            kind == cameraunlock::memory::FingerprintMismatch::Newer
                ? "game build is newer than this mod supports - check for a mod update"
            : kind == cameraunlock::memory::FingerprintMismatch::Older
                ? "game build is older than v1.5.0 - let the store finish updating the game"
                : "EXE does not match the supported build (tampered or repacked binary)";
        Logger::Instance().Error(
            "Camera hook: unknown ACU build (expected v1.5.0: ts=0x%08X size=0x%08X csum=0x%08X). "
            "%s. Staying dormant - no hooks installed.",
            kAcuV150Fingerprint.TimeDateStamp, kAcuV150Fingerprint.SizeOfImage,
            kAcuV150Fingerprint.CheckSum, hint);
        return false;
    }

    if (!IsInModuleSection(kCameraManagerSingletonAddr, modBase, modSize)) {
        Logger::Instance().Error(
            "Camera hook: CameraManager singleton slot 0x%llX is outside ACU.exe.",
            static_cast<unsigned long long>(kCameraManagerSingletonAddr));
        return false;
    }

    if (!IsInModuleSection(kMenuManagerSingletonAddr, modBase, modSize)) {
        Logger::Instance().Error(
            "Camera hook: MenuManager singleton slot 0x%llX is outside ACU.exe.",
            static_cast<unsigned long long>(kMenuManagerSingletonAddr));
        return false;
    }

    Logger::Instance().Info(
        "Camera hook: ACU.exe at %p (size %.2f MB). Strategy: per-instance vtable "
        "swap (no .text or .rdata modification).",
        reinterpret_cast<void*>(modBase),
        modSize / (1024.0 * 1024.0));

    const Config& config = Mod::Instance().GetConfig();
    g_guardEnabled.store(config.cullGuardEnabled, std::memory_order_relaxed);
    g_guardBiasMeters = config.cullGuardBiasMeters;

    // Guard band: install the VEH and arm the frustum-builder breakpoint (DR1).
    // The actor-cull bit-4 suppressor (DR2 at kActorCullOrInstrRva) was tried
    // and DISABLED: clearing bit 4 on every cull-test fires entities that the
    // engine intended to keep hidden as side-effect markers - downstream
    // animation/LOD/skin-update paths read the bit too, so forcing it 0
    // produced deformed meshes, missing-Arno, and broken animations. Need a
    // narrower intervention; left wired but unarmed for now.
    g_guardInjectAddr = modBase + kFrustumBuilderInjectRva;
    g_actorCullInstrAddr = 0;  // intentionally not armed
    if (!g_guardVeh) g_guardVeh = AddVectoredExceptionHandler(1, GuardVeh);
    SetGuardBpAllThreads(g_guardInjectAddr, true);
    Logger::Instance().Info(
        "Guard armed: frustum widener at +0x%llX (%s, bias %.0f m); "
        "actor-cull suppressor disabled (broke animations/LOD on first try)",
        static_cast<unsigned long long>(kFrustumBuilderInjectRva),
        config.cullGuardEnabled ? "enabled" : "disabled",
        g_guardBiasMeters);

    g_watcherStop.store(false);
    g_watcherThread = std::thread(WatcherThread, modBase, modSize);

#ifdef ACUHT_DEBUG_FRUSTUM_SCAN
    g_modBaseDbg = modBase;
    g_dbgThreadStop.store(false);
    g_dbgThread = std::thread(DbgWorkerThread);
#endif
    return true;
}

void RemoveCameraHook() {
    g_watcherStop.store(true);
    if (g_watcherThread.joinable()) g_watcherThread.join();

    // Tear down: disarm all hardware breakpoints everywhere, drop the VEH.
    if (g_guardInjectAddr) { SetGuardBpAllThreads(g_guardInjectAddr, false); g_guardInjectAddr = 0; }
    if (g_actorCullInstrAddr) { SetActorCullBpAllThreads(g_actorCullInstrAddr, false); g_actorCullInstrAddr = 0; }
#ifdef ACUHT_DEBUG_FRUSTUM_SCAN
    if (g_actorCullFuncAddr) { SetExecBpAllThreads(3, g_actorCullFuncAddr, false); g_actorCullFuncAddr = 0; }
#endif
    if (g_guardVeh) { RemoveVectoredExceptionHandler(g_guardVeh); g_guardVeh = nullptr; }

#ifdef ACUHT_DEBUG_FRUSTUM_SCAN
    g_dbgThreadStop.store(true);
    if (g_dbgThread.joinable()) g_dbgThread.join();
    if (g_hbpAddr.load()) { SetDr0AllThreads(nullptr, false); g_hbpAddr.store(nullptr); }
    if (g_hbpVeh) { RemoveVectoredExceptionHandler(g_hbpVeh); g_hbpVeh = nullptr; }
#endif

    // Restore every instance's vptr we touched, so unloading doesn't leave
    // dangling pointers into our DLL's memory after PROCESS_DETACH.
    if (g_origVTable) {
        std::lock_guard<std::mutex> lock(g_patchMutex);
        auto* singletonSlot = reinterpret_cast<void**>(kCameraManagerSingletonAddr);
        void* camMgr = *singletonSlot;
        if (camMgr) {
            void*** arrPtr   = reinterpret_cast<void***>(reinterpret_cast<uint8_t*>(camMgr) + kCMOffArrToPlayerCam);
            uint16_t arrSize = *reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(camMgr) + kCMOffArrSize);
            void** entries   = *arrPtr;
            void* ourClone = reinterpret_cast<void*>(&g_clonedVTable[0]);
            if (entries) {
                for (uint16_t i = 0; i < arrSize; ++i) {
                    void* inst = entries[i];
                    if (!inst) continue;
                    void** vptrSlot = reinterpret_cast<void**>(inst);
                    if (*vptrSlot == ourClone) *vptrSlot = g_origVTable;
                }
            }
        }
        g_origVTable = nullptr;
        g_origApplyFX = nullptr;
    }
    g_installed.store(false);
    Logger::Instance().Info("Camera hook removed");
}

void SetCameraHookEnabled(bool enabled) {
    g_enabled.store(enabled);
}

bool ToggleCullGuard() {
    bool now = !g_guardEnabled.load();
    g_guardEnabled.store(now);
    if (now) {
        g_guardFireLogCount.store(0);  // restart per-fire diagnostic stream
    }
    Logger::Instance().Info("Cull-frustum guard band %s", now ? "ON" : "OFF");
    return now;
}

#ifdef ACUHT_DEBUG_FRUSTUM_SCAN
void DebugArmFrustumScan() {
    g_dbgArm.store(true);
    Logger::Instance().Info("FrustumScan armed - fires on next camera tick");
}
#endif

} // namespace ACUHT
