/*
 *  Arstro DSP — Dither (REQ-out-dither-1): TPDF dither and quantisation to a bit depth.
 *
 *  Judged by the properties TPDF dither exists for — the error's mean and power the same whatever the signal
 *  (Δ²/4), a sound under one LSB kept rather than rounded away — and by the grid it writes on. No main():
 *  registration is shared with synthTests.cpp through MiniTest.
 */
#include "MiniTest.h"
#include "../src/synth_dsp.h"
#include <cmath>
#include <vector>

using namespace arstro;

namespace
{
    constexpr double kPi = 3.14159265358979323846;
}

TEST(Dither_writes_on_the_grid_of_its_depth)
{
    for (int bits : {16, 24})
    {
        Dither d(bits);
        const double Q = std::ldexp(1.0, bits - 1);
        bool grid = true, range = true;
        for (int i = 0; i < 20000; ++i)
        {
            const float x = (float)(1.2 * std::sin(0.001 * i)); // over full scale too: clamped
            const double k = (double)d.quantize(x) * Q;
            grid &= k == std::floor(k);
            range &= k >= -Q && k <= Q - 1.0;
        }
        CHECK(grid && range);
    }
}

TEST(Dither_error_has_the_same_mean_and_power_whatever_the_signal)
{
    // TPDF: E[e] = 0 and E[e²] = Δ²/4 (Δ²/12 of rounding + Δ²/6 of the triangle) for ANY input — (D1)–(D3).
    // Rounding alone: an error that depends on where the input falls between two steps.
    const double Q = 32768.0;
    for (double frac : {0.0, 0.25, 0.5, 0.8})
    {
        Dither d(16);
        Dither r(16);
        r.setEnabled(false);
        const float x = (float)((1000.0 + frac) / Q);
        double s = 0, s2 = 0, rs2 = 0;
        const int n = 200000;
        for (int i = 0; i < n; ++i)
        {
            const double e = ((double)d.quantize(x) - (double)x) * Q;
            s += e;
            s2 += e * e;
            const double er = ((double)r.quantize(x) - (double)x) * Q;
            rs2 += er * er;
        }
        CHECK(std::fabs(s / n) < 0.01);             // in steps
        CHECK(std::fabs(s2 / n - 0.25) < 0.01);
        if (frac == 0.0) CHECK(rs2 == 0.0);         // on a step, rounding is exact …
        if (frac == 0.5) CHECK(std::fabs(rs2 / n - 0.25) < 1e-9); // … half way, a quarter step² — it follows x
    }
}

TEST(Dither_keeps_a_sound_under_one_step)
{
    // a 1 kHz sine at 0.4 LSB of 16 bits (−98 dBFS): rounding writes silence; dithered, its DFT bin reads it
    const double Q = 32768.0, amp = 0.4 / Q;
    Dither d(16), r(16);
    r.setEnabled(false);
    double re = 0, im = 0;
    bool silent = true;
    const int n = 48000 * 4;
    for (int i = 0; i < n; ++i)
    {
        const double ph = 2.0 * kPi * 1000.0 * i / 48000.0;
        const float x = (float)(amp * std::sin(ph));
        const double y = d.quantize(x);
        re += y * std::cos(ph);
        im += y * std::sin(ph);
        silent &= r.quantize(x) == 0.0f;
    }
    const double measured = 2.0 * std::sqrt(re * re + im * im) / n;
    CHECK(silent);
    CHECK(std::fabs(measured / amp - 1.0) < 0.1);
}

TEST(Dither_is_seeded)
{
    Dither a(16), b(16);
    std::vector<float> x(4096), y(4096);
    for (size_t i = 0; i < x.size(); ++i) x[i] = y[i] = (float)(0.3 * std::sin(0.01 * i));
    a.process(x.data(), 4096);
    b.process(y.data(), 4096);
    CHECK(x == y);
    a.reset();
    std::vector<float> z(4096);
    for (size_t i = 0; i < z.size(); ++i) z[i] = (float)(0.3 * std::sin(0.01 * i));
    a.process(z.data(), 4096);
    CHECK(z == y);
}

TEST(Dither_lets_digital_silence_be)
{
    // (D0): exact zeros stay zero — a silent intro is still silent; the smallest non-zero value is dithered as any
    Dither d(16);
    bool silent = true;
    for (int i = 0; i < 10000; ++i) silent &= d.quantize(0.0f) == 0.0f;
    int moved = 0;
    for (int i = 0; i < 10000; ++i) moved += d.quantize(1e-9f) != 0.0f;
    CHECK(silent && moved > 1000 && moved < 4000); // ±1 step 1/4 of the time
}

