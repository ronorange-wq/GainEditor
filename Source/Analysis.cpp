#include "Analysis.h"
#include <cmath>

namespace Analysis
{
    //==========================================================================
    // Blob detection
    //==========================================================================
    static double lerpLog (double a, double b, double t)
    {
        return std::exp (std::log (a) + (std::log (b) - std::log (a)) * t);
    }

    juce::String resolutionName (float r)
    {
        if (r < 0.2f)  return "Phrase";
        if (r < 0.45f) return "Word";
        if (r < 0.75f) return "Syllable";
        if (r < 0.93f) return "Fine";
        return "Peak";
    }

    std::vector<Blob> detectBlobs (const CaptureStore& audio, int64_t start, int64_t end, float resolution)
    {
        std::vector<Blob> result;
        if (end <= start)
            return result;

        const auto sr = audio.getSampleRate();
        const auto r = (double) juce::jlimit (0.0f, 1.0f, resolution);

        // 1) Peak envelope with a 1 ms hop.
        const int hop = juce::jmax (1, (int) std::round (sr / 1000.0));
        const auto numHops = (size_t) ((end - start + hop - 1) / hop);
        std::vector<float> env (numHops, 0.0f);
        {
            std::vector<float> buf ((size_t) hop);
            for (size_t i = 0; i < numHops; ++i)
            {
                const auto p = start + (int64_t) i * hop;
                const auto n = (int) juce::jmin ((int64_t) hop, end - p);
                audio.read (p, buf.data(), n);
                float m = 0.0f;
                for (int k = 0; k < n; ++k)
                    m = juce::jmax (m, std::abs (buf[(size_t) k]));
                env[i] = m;
            }
        }

        // 2) Smooth it. Phrase = 400 ms window ... Peak = no smoothing.
        const auto windowHops = (int) std::round (lerpLog (400.0, 1.0, r));
        std::vector<float> smooth (numHops, 0.0f);
        if (windowHops <= 1)
        {
            smooth = env;
        }
        else
        {
            double acc = 0.0;
            const int half = windowHops / 2;
            std::vector<double> prefix (numHops + 1, 0.0);
            for (size_t i = 0; i < numHops; ++i)
                prefix[i + 1] = prefix[i] + env[i];
            for (size_t i = 0; i < numHops; ++i)
            {
                const auto a = (size_t) juce::jmax (0, (int) i - half);
                const auto b = (size_t) juce::jmin ((int) numHops, (int) i + half + 1);
                acc = (prefix[b] - prefix[a]) / (double) (b - a);
                smooth[i] = (float) acc;
            }
        }

        auto toDb = [] (float v) { return juce::Decibels::gainToDecibels (v, -120.0f); };

        // 3) Settings that follow the resolution.
        const auto silenceDb = -50.0f;
        const auto valleyDepthDb = (float) (9.0 + (0.4 - 9.0) * r);       // how deep a dip must be to split
        const auto minLenHops = juce::jmax (1, (int) std::round (lerpLog (250.0, 1.5, r)));

        // 4) Walk the envelope: silent hops end a blob, deep enough valleys split one.
        std::vector<std::pair<size_t, size_t>> segs; // [a, b) in hops
        size_t i = 0;
        while (i < numHops)
        {
            while (i < numHops && toDb (smooth[i]) < silenceDb) ++i;
            if (i >= numHops) break;
            size_t runStart = i;
            while (i < numHops && toDb (smooth[i]) >= silenceDb) ++i;
            size_t runEnd = i;

            // split this non-silent run at valleys
            size_t segStart = runStart;
            float segMax = smooth[runStart];
            for (size_t k = runStart + 1; k + 1 < runEnd; ++k)
            {
                segMax = juce::jmax (segMax, smooth[k]);
                const bool isMin = smooth[k] <= smooth[k - 1] && smooth[k] < smooth[k + 1];
                if (! isMin)
                    continue;

                // highest point between this valley and the next valley
                float nextMax = smooth[k];
                for (size_t j = k + 1; j < runEnd; ++j)
                {
                    nextMax = juce::jmax (nextMax, smooth[j]);
                    if (j + 1 < runEnd && smooth[j] <= smooth[j - 1] && smooth[j] < smooth[j + 1] && j > k + 1)
                        break;
                }

                const auto depth = toDb (juce::jmin (segMax, nextMax)) - toDb (smooth[k]);
                if (depth >= valleyDepthDb
                    && (int) (k - segStart) >= minLenHops
                    && (int) (runEnd - k) >= minLenHops)
                {
                    segs.push_back ({ segStart, k });
                    segStart = k;
                    segMax = smooth[k];
                }
            }
            segs.push_back ({ segStart, runEnd });
        }

        // 5) Merge pieces that are too short into a neighbour.
        std::vector<std::pair<size_t, size_t>> merged;
        for (auto& s : segs)
        {
            if (! merged.empty() && (int) (s.second - s.first) < minLenHops && merged.back().second == s.first)
                merged.back().second = s.second;
            else
                merged.push_back (s);
        }

        for (auto& s : merged)
        {
            Blob b;
            b.start = start + (int64_t) s.first * hop;
            b.end = juce::jmin (end, start + (int64_t) s.second * hop);
            if (b.end > b.start)
                result.push_back (b);
        }
        return result;
    }

    void redetect (EditState& state, const CaptureStore& audio, float resolution, int64_t start, int64_t end)
    {
        if (! audio.hasData())
            return;

        if (end <= start)
        {
            start = audio.getWrittenStart();
            end = audio.getWrittenEnd();
        }

        const auto old = state.blobs;
        auto fresh = detectBlobs (audio, start, end, resolution);

        // Keep the gain the user gave the old blob that sat under each new blob's middle.
        for (auto& nb : fresh)
        {
            const auto mid = (nb.start + nb.end) / 2;
            for (auto& ob : old)
                if (mid >= ob.start && mid < ob.end) { nb.db = ob.db; break; }
        }

        std::vector<Blob> out;
        for (auto& ob : old)
        {
            if (ob.end <= start || ob.start >= end) { out.push_back (ob); continue; }
            if (ob.start < start) out.push_back ({ ob.start, start, ob.db });
            if (ob.end > end)     out.push_back ({ end, ob.end, ob.db });
        }
        out.insert (out.end(), fresh.begin(), fresh.end());
        state.blobs = std::move (out);
        EditOps::sortBlobs (state);
    }

    //==========================================================================
    // Loudness
    //==========================================================================
    namespace
    {
        struct Biquad
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
            double process (double x) noexcept
            {
                const auto y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };

        // ITU-R BS.1770 K-weighting for any sample rate (same formulas as libebur128).
        void makeKWeighting (double fs, Biquad& shelf, Biquad& hp)
        {
            {
                const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
                const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
                const double Vh = std::pow (10.0, G / 20.0);
                const double Vb = std::pow (Vh, 0.4996667741545416);
                const double a0 = 1.0 + K / Q + K * K;
                shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
                shelf.b1 = 2.0 * (K * K - Vh) / a0;
                shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
                shelf.a1 = 2.0 * (K * K - 1.0) / a0;
                shelf.a2 = (1.0 - K / Q + K * K) / a0;
            }
            {
                const double f0 = 38.13547087602444, Q = 0.5003270373238773;
                const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
                const double a0 = 1.0 + K / Q + K * K;
                hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
                hp.a1 = 2.0 * (K * K - 1.0) / a0;
                hp.a2 = (1.0 - K / Q + K * K) / a0;
            }
        }

        // 4x oversampling interpolator for true peak (windowed sinc, 12 taps per phase).
        struct TruePeak
        {
            static constexpr int taps = 12, phases = 4;
            double coeffs[phases][taps] {};
            double hist[taps] {};
            int pos = 0;

            TruePeak()
            {
                for (int p = 0; p < phases; ++p)
                    for (int t = 0; t < taps; ++t)
                    {
                        const double x = (double) (t - taps / 2 + 1) - (double) p / phases;
                        const double sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                        const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * x / (taps / 2.0));
                        coeffs[p][t] = sinc * w;
                    }
            }

            double push (double x) noexcept
            {
                hist[pos] = x;
                pos = (pos + 1) % taps;
                double m = 0.0;
                for (int p = 0; p < phases; ++p)
                {
                    double acc = 0.0;
                    for (int t = 0; t < taps; ++t)
                        acc += coeffs[p][t] * hist[(pos + t) % taps];
                    m = juce::jmax (m, std::abs (acc));
                }
                return m;
            }
        };

        struct Meter
        {
            Biquad shelf, hp;
            TruePeak tp;
            double peak = 0, truePeak = 0, sumSq = 0, sumK = 0;
            int64_t count = 0;

            explicit Meter (double fs) { makeKWeighting (fs, shelf, hp); }

            void warm (double x) noexcept { hp.process (shelf.process (x)); tp.push (x); }

            void add (double x) noexcept
            {
                peak = juce::jmax (peak, std::abs (x));
                truePeak = juce::jmax (truePeak, tp.push (x), std::abs (x));
                sumSq += x * x;
                const auto k = hp.process (shelf.process (x));
                sumK += k * k;
                ++count;
            }

            LoudnessStats stats() const
            {
                LoudnessStats s;
                if (count == 0) return s;
                s.peakDb = juce::Decibels::gainToDecibels ((float) peak, -150.0f);
                s.truePeakDb = juce::Decibels::gainToDecibels ((float) truePeak, -150.0f);
                s.rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (sumSq / (double) count), -150.0f);
                const auto ms = sumK / (double) count;
                s.lufs = ms > 1e-15 ? (float) (-0.691 + 10.0 * std::log10 (ms)) : -150.0f;
                return s;
            }
        };
    }

    LoudnessReport measure (const CaptureStore& audio, const EditState& edits, int64_t start, int64_t end)
    {
        LoudnessReport rep;
        if (end <= start || ! audio.hasData())
            return rep;

        const auto sr = audio.getSampleRate();
        auto map = EditOps::buildGainMap (edits, sr);

        Meter orig (sr), now (sr);
        const auto warmup = (int64_t) (sr * 0.2);
        constexpr int block = 8192;
        std::vector<float> buf (block);

        for (auto p = start - warmup; p < start; p += block)
        {
            const auto n = (int) juce::jmin ((int64_t) block, start - p);
            audio.read (p, buf.data(), n);
            for (int k = 0; k < n; ++k)
            {
                orig.warm (buf[(size_t) k]);
                now.warm (buf[(size_t) k] * map->gainAt (p + k));
            }
        }

        double gainSum = 0.0;
        for (auto p = start; p < end; p += block)
        {
            const auto n = (int) juce::jmin ((int64_t) block, end - p);
            audio.read (p, buf.data(), n);
            for (int k = 0; k < n; ++k)
            {
                const auto g = map->gainAt (p + k);
                orig.add (buf[(size_t) k]);
                now.add (buf[(size_t) k] * g);
                gainSum += juce::Decibels::gainToDecibels (g, -120.0f);
            }
        }

        rep.original = orig.stats();
        rep.now = now.stats();
        rep.gainAppliedDb = (float) (gainSum / (double) (end - start));
        rep.seconds = (double) (end - start) / sr;
        rep.valid = true;
        return rep;
    }
}
