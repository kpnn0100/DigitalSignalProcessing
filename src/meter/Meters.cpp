#include "Meters.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        double rate() { return std::max(8000.0, (double)AudioConfig::instance().sampleRate()); }
        /** The modified Bessel function I0, by its series — for the Kaiser window (README (T2)). */
        double besselI0(double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 50; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
                if (term < 1e-16 * sum) break;
            }
            return sum;
        }
    }

    // ── K-weighting (README (L1), (L2)) ─────────────────────────────────────────────────────────

    void KWeighting::design(double fs)
    {
        // the pre-filter: a high shelf, ≈ +4 dB above ≈ 1.7 kHz — the bilinear design whose 48 kHz coefficients
        // are BS.1770-4's table exactly, so the same filter at any rate
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(kPi * f0 / fs), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf = MeterSection{(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                                 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0};
        }
        // the RLB weighting: a second-order high-pass at ≈ 38 Hz
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(kPi * f0 / fs), a0 = 1.0 + K / Q + K * K;
            highPass = MeterSection{1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0};
        }
    }

    // ── loudness (README (L3) … (L6)) ────────────────────────────────────────────────────────────

    LoudnessMeter::LoudnessMeter(int channels) : mChannels(std::clamp(channels, 1, 2)), mBinCount(kBins, 0), mBinSum(kBins, 0.0) { reset(); }

    void LoudnessMeter::reset()
    {
        const double fs = rate();
        for (auto &k : mK) k.design(fs);
        mSubLen = std::max(1, (int)std::lround(0.1 * fs));
        mSubFill = 0;
        mAcc = 0.0;
        mRing.fill(0.0);
        mRingAt = mRingCount = 0;
        mBlocks = 0;
        std::fill(mBinCount.begin(), mBinCount.end(), 0);
        std::fill(mBinSum.begin(), mBinSum.end(), 0.0);
    }

    double LoudnessMeter::lufs(double z) { return z > 1e-15 ? -0.691 + 10.0 * std::log10(z) : kSilence; }

    double LoudnessMeter::meanOfLast(int n) const
    {
        n = std::min(n, mRingCount);
        if (n <= 0) return 0.0;
        double s = 0.0;
        for (int i = 1; i <= n; ++i) s += mRing[(size_t)((mRingAt - i + (int)mRing.size()) % (int)mRing.size())];
        return s / n;
    }

    void LoudnessMeter::process(const float *left, const float *right, int frames)
    {
        for (int i = 0; i < frames; ++i)
        {
            // (L3): the K-weighted squares summed over the channels, weight 1 for left and right
            const double l = mK[0].run(left[i]);
            double e = l * l;
            if (mChannels > 1 && right)
            {
                const double r = mK[1].run(right[i]);
                e += r * r;
            }
            mAcc += e;
            if (++mSubFill < mSubLen) continue;
            // a 100 ms sub-block ends: its mean square into the ring
            mRing[(size_t)mRingAt] = mAcc / mSubLen;
            mRingAt = (mRingAt + 1) % (int)mRing.size();
            mRingCount = std::min(mRingCount + 1, (int)mRing.size());
            mAcc = 0.0;
            mSubFill = 0;
            if (mRingCount < 4) continue;
            // (L5): every 100 ms a 400 ms gating block (75 % overlap) — into the histogram when above −70 LUFS
            const double z = meanOfLast(4), L = lufs(z);
            ++mBlocks;
            if (L <= -70.0) continue;
            const int bin = std::clamp((int)std::floor((L + 70.0) / 0.01), 0, kBins - 1);
            ++mBinCount[(size_t)bin];
            mBinSum[(size_t)bin] += z;
        }
    }

    double LoudnessMeter::momentary() const { return lufs(meanOfLast(4)); }
    double LoudnessMeter::shortTerm() const { return lufs(meanOfLast(30)); }

    double LoudnessMeter::integrated() const
    {
        // (L6): the mean of the blocks above the absolute gate; −10 LU from it, the relative gate; the mean above both
        long long n = 0;
        double s = 0.0;
        for (int b = 0; b < kBins; ++b) { n += mBinCount[(size_t)b]; s += mBinSum[(size_t)b]; }
        if (n == 0) return kSilence;
        const double gate = lufs(s / n) - 10.0;
        const int from = std::clamp((int)std::ceil((gate + 70.0) / 0.01), 0, kBins);
        n = 0;
        s = 0.0;
        for (int b = from; b < kBins; ++b) { n += mBinCount[(size_t)b]; s += mBinSum[(size_t)b]; }
        return n ? lufs(s / n) : kSilence;
    }

    // ── true peak (README (T1) … (T3)) ───────────────────────────────────────────────────────────

    TruePeakMeter::TruePeakMeter(int channels) : mChannels(std::clamp(channels, 1, 2))
    {
        // a 64-tap windowed-sinc interpolator at 4× (cut at the original Nyquist), split into 4 phases of 16 taps,
        // each phase normalised to unit gain at DC so a slow signal reads its own level
        constexpr int N = kFactor * kTapsPerPhase;
        const double c = (N - 1) / 2.0, beta = 6.0, i0b = besselI0(beta);
        for (int m = 0; m < N; ++m)
        {
            const double t = (m - c) / kFactor;
            const double sinc = std::fabs(t) < 1e-12 ? 1.0 : std::sin(kPi * t) / (kPi * t);
            const double u = 2.0 * m / (N - 1) - 1.0;
            const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - u * u))) / i0b;
            mH[(size_t)(m % kFactor)][(size_t)(m / kFactor)] = sinc * w;
        }
        for (auto &phase : mH)
        {
            double sum = 0.0;
            for (double h : phase) sum += h;
            for (double &h : phase) h /= sum;
        }
        reset();
    }

    void TruePeakMeter::reset()
    {
        for (auto &h : mHist) h.fill(0.0);
        mPeak = mBlockPeak = 0.0;
    }

    void TruePeakMeter::process(const float *left, const float *right, int frames)
    {
        mBlockPeak = 0.0;
        for (int ch = 0; ch < mChannels; ++ch)
        {
            const float *x = ch == 0 ? left : right;
            if (!x) continue;
            auto &hist = mHist[(size_t)ch];
            for (int i = 0; i < frames; ++i)
            {
                std::copy_backward(hist.begin(), hist.end() - 1, hist.end());
                hist[0] = x[i];
                for (const auto &phase : mH)
                {
                    double y = 0.0;
                    for (int k = 0; k < kTapsPerPhase; ++k) y += phase[(size_t)k] * hist[(size_t)k];
                    mBlockPeak = std::max(mBlockPeak, std::fabs(y));
                }
                mBlockPeak = std::max(mBlockPeak, (double)std::fabs(x[i])); // never under the sample peak
            }
        }
        mPeak = std::max(mPeak, mBlockPeak);
    }

    double TruePeakMeter::peakDb() const { return mPeak > 1e-15 ? 20.0 * std::log10(mPeak) : LoudnessMeter::kSilence; }
    double TruePeakMeter::lastBlockDb() const { return mBlockPeak > 1e-15 ? 20.0 * std::log10(mBlockPeak) : LoudnessMeter::kSilence; }

    // ── correlation (README (C1)) ────────────────────────────────────────────────────────────────

    CorrelationMeter::CorrelationMeter() { reset(); }

    void CorrelationMeter::reset()
    {
        mAlpha = 1.0 - std::exp(-1.0 / (0.3 * rate()));
        mLR = mLL = mRR = 0.0;
    }

    void CorrelationMeter::process(const float *left, const float *right, int frames)
    {
        if (!left || !right) return;
        for (int i = 0; i < frames; ++i)
        {
            const double l = left[i], r = right[i];
            mLR += mAlpha * (l * r - mLR);
            mLL += mAlpha * (l * l - mLL);
            mRR += mAlpha * (r * r - mRR);
        }
    }

    double CorrelationMeter::correlation() const
    {
        const double d = std::sqrt(mLL * mRR);
        return d > 1e-12 ? std::clamp(mLR / d, -1.0, 1.0) : 0.0;
    }

    // ── spectrum (README (S1), (S2)) ─────────────────────────────────────────────────────────────

    SpectrumAnalyser::SpectrumAnalyser() { reset(); }
    SpectrumAnalyser::~SpectrumAnalyser() = default;

    double SpectrumAnalyser::bandHz(int band) { return 1000.0 * std::pow(2.0, (band - 34) / 6.0); }

    void SpectrumAnalyser::reset()
    {
        const double fs = rate();
        const double q = 1.0 / (std::pow(2.0, 1.0 / 12.0) - std::pow(2.0, -1.0 / 12.0)); // a 1/6-octave band's Q
        mBank.clear();
        for (int b = 0; b < kBands; ++b)
        {
            const double hz = bandHz(b);
            if (hz > 0.45 * fs) mBank.push_back(nullptr);
            else
            {
                auto f = std::make_unique<Biquad>(Biquad::BandPass, (Sample)hz, (Sample)q);
                f->reset();
                mBank.push_back(std::move(f));
            }
        }
        mPower.fill(0.0);
        mAlpha = 1.0 - std::exp(-1.0 / (0.3 * fs));
    }

    void SpectrumAnalyser::process(const float *left, const float *right, int frames)
    {
        for (int i = 0; i < frames; ++i)
        {
            const double x = right ? 0.5 * ((double)left[i] + right[i]) : left[i];
            for (int b = 0; b < kBands; ++b)
            {
                Biquad *f = mBank[(size_t)b].get();
                if (!f) continue;
                const double y = f->out((Sample)x, 0);
                mPower[(size_t)b] += mAlpha * (y * y - mPower[(size_t)b]);
            }
        }
    }

    double SpectrumAnalyser::bandDb(int band) const
    {
        if (band < 0 || band >= kBands || !mBank[(size_t)band]) return LoudnessMeter::kSilence;
        const double p = 2.0 * mPower[(size_t)band]; // a full-scale sine's mean square is 1/2: it reads 0 dB
        return p > 1e-15 ? 10.0 * std::log10(p) : LoudnessMeter::kSilence;
    }
}
