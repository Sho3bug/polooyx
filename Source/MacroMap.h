#pragma once
// POLOOYX — macro network.
//
// The six macros never map 1:1 to an effect. Each one moves several engine targets at once,
// on top of the ADVANCED values (which act as the base). With POLOOYX mode on, the macros are
// also cross-linked so they interact musically (e.g. DARK tames AURA's air, SPACE + BODY raise
// the ducking that keeps the lead clear, GLITCH × SPACE throws glitched fragments into the delay).

#include "dsp/DspCommon.h"
#include <algorithm>

namespace plx
{
struct RawParams
{
    float inGain, outGain, mix, ceiling, punch;               // punch 0..1
    float polooyx;                                             // smoothed 0..1 (click-free switching)
    float aura, glitch, space, dark, chaos, body;              // 0..1
    bool  tuneOn; int key, scale; float tuneAmount, retune, formant;
    float eqOn; float hpf, lpf, lowFreq, lowGain, lmFreq, lmGain, hmFreq, hmGain, highFreq, highGain;
    float deessOn; float deessFreq, deessThresh, deessRange;
    float compOn; float compThresh, compRatio, compAttack, compRelease, compMakeup;
    float satOn; int satMode; float satDrive, satMix, satTone, satOut;           // drive/mix 0..1, tone -1..1
    float glitchOn; float glitchAmount; int glitchRate; bool glitchSmart;
    float glitchStutter, glitchReverse, glitchGate, glitchTape, glitchPitch, glitchMix;   // 0..1
    float spaceOn; float revSize, revDecay, revPredelay, revDamp, revMix; int dlyDiv;
    float dlyFeedback, dlyMix; bool dlyPingPong; float dlyHp, dlyLp, duck;               // 0..1 where %
    float width, movement, microPitch, shadow;                                            // width 0..2, others 0..1
};

struct Targets
{
    // pitch
    bool tuneOn; int key, scale; float tuneAmount, retune, formant;
    float vibratoCents, vibratoHz, driftCents, driftHz;
    // eq
    float eqAmt; float hpf, lpf, lowFreq, lowGain, lmFreq, lmGain, hmFreq, hmGain, highFreq, highGain;
    // dynamics
    float deessAmt; float deessFreq, deessThresh, deessRange;
    float compAmt; float compThresh, compRatio, compAttack, compRelease, compMakeup;
    // saturation
    float satAmt; int satMode; float satDrive, satMix, satTone, satOut;
    // glitch
    float glitchAmt; float glitchProb; int glitchRate; bool glitchSmart;
    float gStutter, gReverse, gGate, gTape, gPitch, gChaos, gMix, glitchSend;
    // space
    float spaceAmt; float revSize, revDecay, revPredelay, revDamp, revMix, revMod;
    int dlyDiv; float dlyFeedback, dlyMix, dlyHp, dlyLp, dlyWow; bool dlyPingPong;
    float duck;
    // stereo / layers
    float width, panDepth, panRate, doubler, doublerDepth, doublerRate, shadow, shadowDrive;
    // output
    float inGainDb, outGainDb, mix, ceiling;
    float punch, punchDriveDb;
};

inline Targets computeTargets (const RawParams& p) noexcept
{
    Targets t {};
    const float sg = p.polooyx;   // 0 = neutral macro mapping, 1 = POLOOYX signature network
    const float a = p.aura, g = p.glitch, s = p.space, d = p.dark, c = p.chaos, b = p.body;

    // ------------------------------------------------ pitch
    t.tuneOn = p.tuneOn; t.key = p.key; t.scale = p.scale;
    t.tuneAmount = p.tuneAmount;
    t.retune     = p.retune;
    t.formant    = p.formant - 2.5f * d * (0.8f + 0.2f * sg);         // DARK: formants drop
    t.vibratoCents = 7.0f * a;                                           // AURA: subtle lead pitch modulation
    t.vibratoHz    = 4.6f + 1.5f * c;
    t.driftCents   = 38.0f * c * c + sg * 4.0f * g * c;        // CHAOS: pitch movement
    t.driftHz      = 0.25f + 2.5f * c;

    // ------------------------------------------------ EQ
    t.eqAmt = p.eqOn;
    t.hpf      = std::max (p.hpf, lerp (20.0f, 95.0f, sg));
    t.lpf      = std::min (p.lpf, lerp (20000.0f, 4800.0f, std::pow (d, 0.8f)));   // DARK: top end rolls off
    t.lowFreq  = p.lowFreq;  t.lowGain  = p.lowGain + 3.5f * b + 1.5f * d;          // BODY/DARK: weight
    t.lmFreq   = p.lmFreq;   t.lmGain   = p.lmGain + 2.0f * b;                      // BODY: low-mid density
    t.hmFreq   = p.hmFreq;   t.hmGain   = p.hmGain + sg * (1.5f + 2.2f * (0.5f * s + 0.5f * d));   // intelligibility guard
    const float air = 4.5f * a * (1.0f - 0.6f * d * sg);                  // AURA air (tamed by DARK in POLOOYX)
    t.highFreq = p.highFreq; t.highGain = p.highGain + air - 7.0f * d;              // DARK: HF attenuation

    // ------------------------------------------------ de-esser / compressor
    t.deessAmt = p.deessOn;
    t.deessFreq = p.deessFreq; t.deessThresh = p.deessThresh - 4.0f * a; t.deessRange = p.deessRange + 3.0f * a;   // more air → more de-essing
    t.compAmt = p.compOn;
    t.compThresh  = p.compThresh - 12.0f * b;                            // BODY: more compression
    t.compRatio   = p.compRatio + 3.0f * b;
    t.compAttack  = p.compAttack;
    t.compRelease = p.compRelease;
    t.compMakeup  = p.compMakeup + 5.0f * b;

    // ------------------------------------------------ saturation
    t.satAmt = p.satOn;
    t.satMode = p.satMode;
    t.satDrive = clamp01 (p.satDrive + 0.12f * a + 0.22f * d + 0.25f * c + 0.15f * b);
    t.satMix   = clamp01 (p.satMix + 0.25f * b + 0.15f * c);
    t.satTone  = std::max (-1.0f, std::min (1.0f, p.satTone - 0.5f * d + 0.3f * a));
    t.satOut   = p.satOut;

    // ------------------------------------------------ glitch
    t.glitchAmt  = p.glitchOn;
    t.glitchProb = clamp01 (p.glitchAmount + 0.85f * std::pow (g, 1.2f) + 0.25f * c * g);   // CHAOS amplifies GLITCH, never starts it
    t.glitchRate = std::min (4, p.glitchRate + (g > 0.72f ? 1 : 0));    // GLITCH high: finer grid
    t.glitchSmart = p.glitchSmart;
    t.gStutter = p.glitchStutter;
    t.gReverse = clamp01 (p.glitchReverse + 0.4f * c);
    t.gGate    = clamp01 (p.glitchGate + 0.4f * g);                     // GLITCH: rhythmic gating
    t.gTape    = clamp01 (p.glitchTape + 0.3f * c);
    t.gPitch   = clamp01 (p.glitchPitch + 0.35f * g + sg * 0.4f * g * c);
    t.gChaos   = c;
    t.gMix     = p.glitchMix;
    t.glitchSend = sg * 0.6f * g * s;                           // glitched fragments echo into the delay

    // ------------------------------------------------ space
    t.spaceAmt   = p.spaceOn;
    t.revSize    = clamp01 (p.revSize + 0.35f * s);
    t.revDecay   = p.revDecay * (1.0f + 1.3f * s);
    t.revPredelay = p.revPredelay + 45.0f * s;
    t.revDamp    = clamp01 (p.revDamp + 0.45f * d + sg * 0.12f);
    t.revMix     = clamp01 (p.revMix + 0.32f * s + 0.10f * a);
    t.revMod     = 0.25f + 0.4f * a + 0.35f * c;
    t.dlyDiv     = p.dlyDiv;
    t.dlyFeedback = std::min (0.9f, p.dlyFeedback + 0.35f * s);
    t.dlyMix     = clamp01 (p.dlyMix + 0.28f * s);
    t.dlyHp      = p.dlyHp + 300.0f * s;
    t.dlyLp      = std::max (900.0f, p.dlyLp - 2000.0f * s - 2500.0f * d);
    t.dlyWow     = c;
    t.dlyPingPong = p.dlyPingPong;
    t.duck       = clamp01 (p.duck + sg * (0.3f * b + 0.2f * s));

    // ------------------------------------------------ stereo / layers
    t.width     = std::min (2.0f, p.width + 0.6f * a + sg * 0.1f);
    t.panDepth  = clamp01 (p.movement + 0.4f * s + 0.5f * c);
    t.panRate   = 0.08f + 1.6f * c + 0.2f * s;
    t.doubler   = clamp01 (p.microPitch + 0.5f * a + sg * 0.08f);
    t.doublerDepth = clamp01 (0.2f + 0.6f * a + 0.4f * c);
    t.doublerRate  = clamp01 (0.2f + 0.8f * c);
    t.shadow    = clamp01 (p.shadow * (0.6f + 0.4f * sg) + 0.4f * b + sg * 0.15f * d);
    t.shadowDrive = clamp01 (0.3f + 0.5f * d + 0.4f * c);

    // ------------------------------------------------ output
    t.inGainDb = p.inGain; t.outGainDb = p.outGain; t.mix = p.mix; t.ceiling = p.ceiling;
    t.punch        = clamp01 (p.punch + sg * 0.3f * b * p.punch);       // BODY (POLOOYX mode) leans harder into PUNCH
    t.punchDriveDb = 7.0f * t.punch;                                      // drive into the limiter = loudness
    return t;
}
} // namespace plx
