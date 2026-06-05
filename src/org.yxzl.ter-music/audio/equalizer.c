/**
 * @file equalizer.c
 * @brief 10-band ISO graphic equaliser — biquad IIR cascade
 *
 * Implements a cascaded peaking-EQ biquad section for each band.
 * Coefficients are recalculated whenever sample rate or any gain changes.
 * The processing is a pure cascaded-series topology — each band's output
 * feeds the next band's input.
 *
 * @author ter-music team
 * @date 2026-06-05
 */

#include "audio/equalizer.h"

#include <string.h>
#include <math.h>
#include <stdint.h>

/* ================================================================
 * Constants
 * ================================================================ */

/** ISO standard centre frequencies (Hz). */
const int eq_band_frequencies[EQ_BAND_COUNT] = {
    31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
};

/** Q factor — 1.0 gives roughly one octave bandwidth. */
#define EQ_Q  1.0f

/** Maximum number of channels we support (stereo is the common case). */
#define EQ_MAX_CHANNELS  2

/* ================================================================
 * Internal state
 * ================================================================ */

typedef struct {
    /* User-facing parameters */
    int   enabled;
    int   preamp;          /* dB, -12 … +12 */
    int   band_gains[EQ_BAND_COUNT];   /* dB per band */

    /* Coefficient dirtiness */
    int   dirty;           /* non-zero → recalc_coefficients needed */
    int   sample_rate;     /* last sample rate used for coefficients */

    /* Normalised biquad coefficients (direct form I) — one set per band.
     * Normalised so a0 = 1.0, so the recurrence is:
     *   y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
     */
    float b0[EQ_BAND_COUNT];
    float b1[EQ_BAND_COUNT];
    float b2[EQ_BAND_COUNT];
    float a1[EQ_BAND_COUNT];
    float a2[EQ_BAND_COUNT];

    /* Per-channel, per-band delay line state.
     * Indexed [channel][band].  Only used when EQ is active.
     */
    float x1[EQ_MAX_CHANNELS][EQ_BAND_COUNT];
    float x2[EQ_MAX_CHANNELS][EQ_BAND_COUNT];
    float y1[EQ_MAX_CHANNELS][EQ_BAND_COUNT];
    float y2[EQ_MAX_CHANNELS][EQ_BAND_COUNT];
} EqualizerState;

static EqualizerState g_eq = {0};

/* ================================================================
 * Coefficient calculation
 * ================================================================ */

/**
 * Recalculate all biquad coefficients from current gains and sample rate.
 * Uses the standard peaking EQ (bell) biquad formulae.
 *
 *   w0    = 2 * pi * f0 / fs
 *   alpha = sin(w0) / (2 * Q)
 *   A     = 10 ^ (gain_dB / 40)
 *
 *   b0 = 1 + alpha * A      a0 = 1 + alpha / A
 *   b1 = -2 * cos(w0)        a1 = -2 * cos(w0)
 *   b2 = 1 - alpha * A       a2 = 1 - alpha / A
 *
 * Each set is normalised so that a0 = 1.0.
 */
static void recalc_coefficients(void)
{
    if (g_eq.sample_rate <= 0) {
        /* Not enough info — leave coefficients as-is (should be all-zero). */
        return;
    }

    const float fs = (float)g_eq.sample_rate;
    const float Q  = EQ_Q;

    for (int i = 0; i < EQ_BAND_COUNT; i++) {
        float f0 = (float)eq_band_frequencies[i];

        /* If the band's centre frequency is at or above Nyquist,
         * force it to a straight-through (no filtering). */
        if (f0 >= fs * 0.5f) {
            g_eq.b0[i] = 1.0f;
            g_eq.b1[i] = 0.0f;
            g_eq.b2[i] = 0.0f;
            g_eq.a1[i] = 0.0f;
            g_eq.a2[i] = 0.0f;
            continue;
        }

        float gain_linear;
        if (g_eq.band_gains[i] == 0) {
            /* Flat — straight-through.  Saves a tiny bit of CPU
             * and avoids float precision issues at exactly 0 dB. */
            g_eq.b0[i] = 1.0f;
            g_eq.b1[i] = 0.0f;
            g_eq.b2[i] = 0.0f;
            g_eq.a1[i] = 0.0f;
            g_eq.a2[i] = 0.0f;
            continue;
        }

        float w0    = (float)(2.0 * M_PI) * f0 / fs;
        float cos_w0 = cosf(w0);
        float sin_w0 = sinf(w0);
        float alpha = sin_w0 / (2.0f * Q);
        float A     = powf(10.0f, (float)g_eq.band_gains[i] / 40.0f);

        float b0 = 1.0f + alpha * A;
        float b1 = -2.0f * cos_w0;
        float b2 = 1.0f - alpha * A;
        float a0 = 1.0f + alpha / A;
        float a1 = -2.0f * cos_w0;
        float a2 = 1.0f - alpha / A;

        /* Normalise so a0 = 1.0 */
        float inv_a0 = 1.0f / a0;
        g_eq.b0[i] = b0 * inv_a0;
        g_eq.b1[i] = b1 * inv_a0;
        g_eq.b2[i] = b2 * inv_a0;
        g_eq.a1[i] = a1 * inv_a0;
        g_eq.a2[i] = a2 * inv_a0;
    }

    g_eq.dirty = 0;
}

/* ================================================================
 * Public API
 * ================================================================ */

void eq_init(void)
{
    memset(&g_eq, 0, sizeof(g_eq));
    g_eq.enabled = 0;
    g_eq.preamp  = 0;
    /* band_gains are implicitly zero from memset — flat EQ. */
    g_eq.sample_rate = 0;
    g_eq.dirty = 0;
}

void eq_process(int32_t *samples, int frame_count, int channels, int sample_rate)
{
    if (!samples || frame_count <= 0 || channels <= 0) return;
    if (!g_eq.enabled) return;

    /* Clamp channels to our storage limit */
    if (channels > EQ_MAX_CHANNELS)
        channels = EQ_MAX_CHANNELS;

    /* Recalculate coefficients if sample rate changed or gains were updated */
    if (g_eq.sample_rate != sample_rate)
        g_eq.dirty = 1;
    if (g_eq.dirty) {
        g_eq.sample_rate = sample_rate;
        recalc_coefficients();

        /* Reset delay lines on coefficient change to avoid discontinuities */
        memset(g_eq.x1, 0, sizeof(g_eq.x1));
        memset(g_eq.x2, 0, sizeof(g_eq.x2));
        memset(g_eq.y1, 0, sizeof(g_eq.y1));
        memset(g_eq.y2, 0, sizeof(g_eq.y2));
    }

    /* Pre-amp linear factor */
    float preamp_linear = (g_eq.preamp == 0)
        ? 1.0f
        : powf(10.0f, (float)g_eq.preamp / 20.0f);

    /* Process each frame */
    for (int f = 0; f < frame_count; f++) {
        for (int ch = 0; ch < channels; ch++) {
            /* Convert S32 to float (normalised to [-1.0, 1.0)) */
            float in = (float)samples[f * channels + ch] / 2147483648.0f;
            float out = in;

            /* Cascade through all bands */
            for (int b = 0; b < EQ_BAND_COUNT; b++) {
                float b0 = g_eq.b0[b];
                float b1 = g_eq.b1[b];
                float b2 = g_eq.b2[b];
                float a1 = g_eq.a1[b];
                float a2 = g_eq.a2[b];

                float xn = out;
                float yn = b0 * xn + b1 * g_eq.x1[ch][b] + b2 * g_eq.x2[ch][b]
                         - a1 * g_eq.y1[ch][b] - a2 * g_eq.y2[ch][b];

                /* Shift delay line */
                g_eq.x2[ch][b] = g_eq.x1[ch][b];
                g_eq.x1[ch][b] = xn;
                g_eq.y2[ch][b] = g_eq.y1[ch][b];
                g_eq.y1[ch][b] = yn;

                out = yn;  /* feed output to next band */
            }

            /* Apply pre-amp gain */
            out *= preamp_linear;

            /* Clip to valid S32 range to prevent wrap-around on overflow */
            if (out > 1.0f)
                out = 1.0f;
            else if (out < -1.0f)
                out = -1.0f;

            /* Convert back to S32 */
            samples[f * channels + ch] = (int32_t)(out * 2147483647.0f);
        }
    }
}

void eq_set_enabled(int enabled)
{
    g_eq.enabled = (enabled != 0);
}

int eq_is_enabled(void)
{
    return g_eq.enabled;
}

void eq_set_preamp(int gain_db)
{
    if (gain_db < EQ_PREAMP_MIN) gain_db = EQ_PREAMP_MIN;
    if (gain_db > EQ_PREAMP_MAX) gain_db = EQ_PREAMP_MAX;
    g_eq.preamp = gain_db;
    /* Pre-amp is applied independently per-sample — no coefficient recalc needed. */
}

int eq_get_preamp(void)
{
    return g_eq.preamp;
}

void eq_set_band_gain(int band, int gain_db)
{
    if (band < 0 || band >= EQ_BAND_COUNT) return;
    if (gain_db < EQ_GAIN_MIN) gain_db = EQ_GAIN_MIN;
    if (gain_db > EQ_GAIN_MAX) gain_db = EQ_GAIN_MAX;
    if (g_eq.band_gains[band] == gain_db) return;   /* no change */
    g_eq.band_gains[band] = gain_db;
    g_eq.dirty = 1;
}

int eq_get_band_gain(int band)
{
    if (band < 0 || band >= EQ_BAND_COUNT) return 0;
    return g_eq.band_gains[band];
}

void eq_set_all_gains(const int gains[EQ_BAND_COUNT])
{
    if (!gains) return;
    int changed = 0;
    for (int i = 0; i < EQ_BAND_COUNT; i++) {
        int g = gains[i];
        if (g < EQ_GAIN_MIN) g = EQ_GAIN_MIN;
        if (g > EQ_GAIN_MAX) g = EQ_GAIN_MAX;
        if (g_eq.band_gains[i] != g) {
            g_eq.band_gains[i] = g;
            changed = 1;
        }
    }
    if (changed)
        g_eq.dirty = 1;
}
