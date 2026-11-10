// Hooks for default.xbe. A strong definition replaces the recompiled function at that address. The instruction hooks are
// here and in movie_probe.cpp.
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "app/app.h"
#include "common/fps_policy.h"
#include "common/frame_step.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

extern PlayRateSetting g_play_rate;  // src/common/game_info.cpp: experimental_play_fps, as this run plays it

namespace {

constexpr uint32_t kMaxFrameRate = 0x30FF68;  // EXBaseApp::m_MaxFrameRate in the GameCube's names
constexpr uint32_t kFrameLength = 0x30FF74;   // EXBaseApp::m_FrameLength: 1 / the rate the game steps at
constexpr uint32_t kMinFrameRate = 0x30FF64;  // EXBaseApp::m_MinFrameRate
constexpr uint32_t kApp = 0x400418;           // EXBaseApp::m_pTheApp
constexpr uint32_t kAppClockMode = 0x18, kAppBlanksPerSecond = 0x8C;
constexpr uint32_t kAreaFearDecay = 0x30E948;  // XFearFactor::m_AreaFearDecay
constexpr uint32_t kWidescreenByte = 0x4006A4;  // EXBaseRenderEnv::ms_WideScreen
constexpr uint32_t kConsoleWidescreen = 0x3EE4CC;  // what the console said, a new game's default
constexpr uint32_t kOptionList = 0x3F4CFC;      // pointer to (code, value) pairs of 16 bits each, then their count
constexpr uint32_t kWidescreenOption = 0x3E;    // the option's code, and the pair's in a save

float FrameLength() { return std::bit_cast<float>(kernel::R32(kFrameLength)); }

// The per frame hooks only touch frames shorter than the shipped one. A frame at or over it steps as the game always did,
// so at the game's own rate they change nothing, slow frames included.
bool ShortFrame() { return FrameLength() < kShippedFrameLength; }
float Scale() { return ShortFrame() ? FrameScale(FrameLength()) : 1.0f; }

// before mulss xmm<reg>, [k] in x += (target - x) * k: the same share of the gap per second at any rate
void LerpBefore(X86Context& ctx, int reg, float k) {
    if (ShortFrame()) ctx.xmm[reg].f[0] *= StepFraction(k, FrameLength()) / k;
}

// the same for fmul [k] on the x87 stack
double& St0(X86Context& ctx) { return ctx.st[ctx.top & 7]; }
void LerpBeforeSt0(X86Context& ctx, float k) {
    if (ShortFrame()) St0(ctx) *= double(StepFraction(k, FrameLength())) / double(k);
}

// before addss xmm<reg>, [step]: the instruction will add the whole step, so take off the part this frame does not get
void AddBefore(X86Context& ctx, int reg, float step) {
    ctx.xmm[reg].f[0] -= step * (1.0f - Scale());
}

// a rate above the shipped one was asked for
bool g_above_shipped = false;

// whether a frame counter tuned at the shipped rate counts this frame
CounterClock g_counters;

}  // namespace

// The picture's shape is the port's choice (app::Widescreen), but the game takes it from the save: a 16:9 save in a 4:3 window
// comes out squeezed. So at the start of each frame, on the title's thread, the byte the camera reads, the option some
// screens read and the next save writes, and the console's default for a new game all follow the port's choice. Nothing
// changes when they already agree. docs/TECHNICAL.md, "Aspect ratio".
void KeepWidescreenOption(X86Context&, uint8_t* base) {
    const uint8_t wide = app::Widescreen() ? 1 : 0;
    base[kWidescreenByte] = wide;
    kernel::W32(kConsoleWidescreen, wide);
    const uint32_t list = kernel::R32(kOptionList), count = kernel::R32(kOptionList + 4);
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t pair = list + i * 4;
        if (kernel::R16(pair) == kWidescreenOption) {
            kernel::W16(pair + 2, wide);
            break;
        }
    }
}

// at 0xAC71C the game has just asked for 30 frames per second, for all of its play. The player's choice on the options
// screen (experimental_play_fps, as read at the start) replaces that with 60, and BB_DEV_PLAY_FPS with any rate, for measuring.
void PlayFrameRateChosen(X86Context&, uint8_t*) {
    uint32_t rate = PlayFrameRate(g_play_rate.InForce());
    if (const char* forced = std::getenv("BB_DEV_PLAY_FPS"); forced && *forced) rate = static_cast<uint32_t>(std::atoi(forced));
    if (rate == kShippedPlayFps) return;
    g_above_shipped = rate > kShippedPlayFps;
    kernel::W32(kMaxFrameRate, rate);
    LOGR("play frame rate %u", rate);
}

// The hero's speed factor chases the stick once per frame, with these shares of the gap (GameCube offset 0x49C, here
// 0x470), in XItemHandler_Hero::MoveNormal and the Move functions after it. The second rotation lerp in MoveNormal and the
// smoothed distance to danger in XItemHandler_Player::DoFindHeadLookAt use them too.
void HeroLerpXmm0K005(X86Context& ctx, uint8_t*) { LerpBefore(ctx, 0, 0.05f); }
void HeroLerpXmm2K005(X86Context& ctx, uint8_t*) { LerpBefore(ctx, 2, 0.05f); }
void HeroLerpXmm0K0075(X86Context& ctx, uint8_t*) { LerpBefore(ctx, 0, 0.075f); }
void HeroLerpXmm0K05(X86Context& ctx, uint8_t*) { LerpBefore(ctx, 0, 0.5f); }
void HeroLerpXmm0K02(X86Context& ctx, uint8_t*) { LerpBefore(ctx, 0, 0.2f); }
void HeroLerpXmm1K005(X86Context& ctx, uint8_t*) { LerpBefore(ctx, 1, 0.05f); }

// XItemHandler_Hero::CheckTurn: the terms read by air control and by the turn in MoveNormal, GameCube 0x4BC and 0x4B8
void HeroTurnLerpXmm1K01(X86Context& ctx, uint8_t*) { LerpBefore(ctx, 1, 0.1f); }
void HeroTurnLerpXmm1K05(X86Context& ctx, uint8_t*) { LerpBefore(ctx, 1, 0.5f); }

// XItemHandler_Hero::SetHeroHeightWidth: the collision capsule grows back once per frame, height in SSE, width in x87
void HeroCapsuleXmm0K01(X86Context& ctx, uint8_t*) { LerpBefore(ctx, 0, 0.1f); }
void HeroCapsuleXmm0K02(X86Context& ctx, uint8_t*) { LerpBefore(ctx, 0, 0.2f); }
void HeroCapsuleSt0K01(X86Context& ctx, uint8_t*) { LerpBeforeSt0(ctx, 0.1f); }
void HeroCapsuleSt0K02(X86Context& ctx, uint8_t*) { LerpBeforeSt0(ctx, 0.2f); }
// before addss xmm0, 0.02 in XItemHandler_Hero::DoLedgeJump
void HeroLedgeCapsuleStep(X86Context& ctx, uint8_t*) { AddBefore(ctx, 0, 0.02f); }

// Moves that set a fixed distance for the frame, which XItemHandler_Hero::UpdateDeltaMove applies with no time factor:
// kick lunge, sideways combat jump, climb across, sliding on pipes, ladders and ropes, dropping from a pipe.
void HeroMoveXmm0(X86Context& ctx, uint8_t*) { ctx.xmm[0].f[0] *= Scale(); }
void HeroMoveSt0(X86Context& ctx, uint8_t*) { St0(ctx) *= Scale(); }

// before dec eax, or lea edx, [eax - 1], on the hero's frame counters: combat button buffer, regrab lockout, remembered
// object, fight mode, gas pipe turn, platform grace. All load whole frames at the shipped rate.
void HeroCounterEax(X86Context& ctx, uint8_t*) {
    if (!g_counters.Ticks()) ctx.eax += 1;
}

// XEnemyAI::DetermineStatus: alert = alert * 0.99 + (seen ? 0.05 : 0) each frame, kept per second at any rate
void EnemyAlertDecay(X86Context& ctx, uint8_t*) {
    if (ShortFrame()) ctx.xmm[2].f[0] *= std::pow(0.99f, Scale()) / 0.99f;
}
void EnemyAlertGain(X86Context& ctx, uint8_t*) {
    if (ShortFrame()) ctx.xmm[0].f[0] *= (1.0f - std::pow(0.99f, Scale())) / (1.0f - 0.99f);
}

// before subss xmm0, [m_AreaFearDecay] in XFearFactor::UpdateAreaFear
void AreaFearDrain(X86Context& ctx, uint8_t*) { AddBefore(ctx, 0, -std::bit_cast<float>(kernel::R32(kAreaFearDecay))); }

// after 0x7832C in MoveNormal: xmm0 is the turn this frame, xmm3 the angle still to turn, so their ratio is a share of
// the gap per frame, like the speed factor's.
void HeroTurnLerp(X86Context& ctx, uint8_t*) {
    float gap = ctx.xmm[3].f[0];
    if (!ShortFrame() || gap == 0.0f) return;
    ctx.xmm[0].f[0] = gap * StepFraction(ctx.xmm[0].f[0] / gap, FrameLength());
}

// after 0x67B98 in XFearFactor::Update: fear falls by edx, 1 / m_NominalGameFrameRate, each frame, not each 60th of a second
void FearStep(X86Context& ctx, uint8_t*) {
    ctx.edx = std::bit_cast<uint32_t>(std::bit_cast<float>(ctx.edx) * Scale());
}

// before 0x1A869F in the engine's frame function, esi holding the blanks this frame took. The engine averages the rate
// with the last frame's, so with a mix of frame lengths the game falls behind real time: the hero ran 8% slow with frames of 20 ms under
// a 60 target. So when a rate above the shipped one was asked for, the rate comes straight from the blanks. At the
// shipped rate it stays as it was: there the average is what the game was tuned with, and at a mix slower than that the
// hero runs closer to his speed with it (docs/TECHNICAL.md, "Frame rate").
void FrameRateFromBlanks(X86Context& ctx, uint8_t*) {
    if (!g_above_shipped) {
        g_counters.Frame(float(kShippedPlayFps));
        return;
    }
    uint32_t app = kernel::R32(kApp);
    uint8_t mode = static_cast<uint8_t*>(kernel::HostPtr(app + kAppClockMode))[0];
    if (mode != 1 || ctx.esi == 0) return;
    float rate = float(kernel::R32(app + kAppBlanksPerSecond)) / float(ctx.esi);
    float lowest = float(kernel::R32(kMinFrameRate));
    if (rate < lowest) rate = lowest;
    kernel::W32(ctx.esp + 0x10, std::bit_cast<uint32_t>(rate));
    g_counters.Frame(rate);
}
