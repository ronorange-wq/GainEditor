#include "Analysis.h"
#include "HighPass.h"
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

    double blobTargetMs (float resolution)
    {
        return lerpLog (1200.0, 25.0, (double) juce::jlimit (0.0f, 1.0f, resolution));
    }

    std::vector<Segment> detectBlobs (const CaptureStore& audio, int64_t start, int64_t end, float resolution)
    {
        std::vector<Segment> result;
        if (end <= start)
            return result;

        const auto sr = audio.getSampleRate();

        // 1) Energy on a fixed 1 ms grid, so the same audio always gives the same blobs.
        const int hop = juce::jmax (1, (int) std::round (sr / 1000.0));
        start -= ((start % hop) + hop) % hop;
        const auto numHops = (int) ((end - start + hop - 1) / hop);
        std::vector<double> energy ((size_t) numHops, 0.0);
        {
            std::vector<float> buf ((size_t) hop);
            for (int i = 0; i < numHops; ++i)
            {
                const auto p = start + (int64_t) i * hop;
                const auto n = (int) juce::jmin ((int64_t) hop, end - p);
                audio.read (p, buf.data(), n);
                double acc = 0.0;
                for (int k = 0; k < n; ++k)
                    acc += (double) buf[(size_t) k] * buf[(size_t) k];
                energy[(size_t) i] = acc / juce::jmax (1, n);
            }
        }

        // 2) Every slider position has one target length. Blobs are cut so they come out close to it.
        const auto T = blobTargetMs (resolution);                      // in hops (1 hop = 1 ms)
        const int window = juce::jlimit (1, 120, (int) std::round (T / 5.0));
        std::vector<float> db ((size_t) numHops, -120.0f);
        {
            std::vector<double> prefix ((size_t) numHops + 1, 0.0);
            for (int i = 0; i < numHops; ++i)
                prefix[(size_t) i + 1] = prefix[(size_t) i] + energy[(size_t) i];
            const int half = window / 2;
            for (int i = 0; i < numHops; ++i)
            {
                const int a = juce::jmax (0, i - half), b = juce::jmin (numHops, i + half + 1);
                const auto ms = (prefix[(size_t) b] - prefix[(size_t) a]) / (double) (b - a);
                db[(size_t) i] = (float) (10.0 * std::log10 (ms + 1.0e-12));
            }
        }

        float peakDb = -120.0f;
        for (auto v : db) peakDb = juce::jmax (peakDb, v);
        const auto gate = juce::jmax (-60.0f, peakDb - 40.0f);

        // 3) Sound runs: above the gate, short gaps inside a run are closed.
        std::vector<std::pair<int, int>> runs;
        {
            const int closeGap = juce::jlimit (3, 150, (int) std::round (T * 0.25));
            int i = 0;
            while (i < numHops)
            {
                while (i < numHops && db[(size_t) i] < gate) ++i;
                if (i >= numHops) break;
                const int a = i;
                while (i < numHops && db[(size_t) i] >= gate) ++i;
                if (! runs.empty() && a - runs.back().second <= closeGap)
                    runs.back().second = i;
                else
                    runs.push_back ({ a, i });
            }
        }

        // 4) Cut each run at its valleys, choosing the cuts that give blobs closest to the target length.
        const int minLen = juce::jmax (2, (int) std::round (T * 0.35));
        const int maxLen = juce::jmax (minLen + 1, (int) std::round (T * 4.0));
        const int look = juce::jmax (2, (int) std::round (T * 0.5));

        auto valleyDepth = [&] (int c, int lo, int hi)
        {
            float l = db[(size_t) c], r = db[(size_t) c];
            for (int k = juce::jmax (lo, c - look); k < c; ++k) l = juce::jmax (l, db[(size_t) k]);
            for (int k = c + 1; k < juce::jmin (hi, c + look + 1); ++k) r = juce::jmax (r, db[(size_t) k]);
            return juce::jmin (l, r) - db[(size_t) c];
        };

        std::vector<std::pair<int, int>> segs;
        for (auto [a, b] : runs)
        {
            if (b - a < 2)
                continue;
            if (b - a <= (int) (T * 1.6) || b - a < 2 * minLen)
            {
                segs.push_back ({ a, b });
                continue;
            }

            std::vector<int> node { a };
            std::vector<float> cutCost { 0.0f };
            for (int k = a + minLen; k <= b - minLen; ++k)
            {
                const auto v = db[(size_t) k];
                if (v <= db[(size_t) k - 1] && v < db[(size_t) k + 1])
                {
                    node.push_back (k);
                    cutCost.push_back (1.5f * std::exp (-valleyDepth (k, a, b) / 6.0f));
                }
            }
            node.push_back (b);
            cutCost.push_back (0.0f);

            const auto m = node.size();
            std::vector<double> best (m, 1.0e30);
            std::vector<int> from (m, -1);
            best[0] = 0.0;
            for (size_t j = 1; j < m; ++j)
                for (size_t i = j; i-- > 0;)
                {
                    const auto len = node[j] - node[i];
                    if (len > maxLen && i + 1 < j) break;
                    if (len < minLen && i > 0 && j + 1 < m) continue;
                    const auto lc = std::log ((double) len / T);
                    const auto c = best[i] + 3.0 * lc * lc + cutCost[j];
                    if (c < best[j]) { best[j] = c; from[j] = (int) i; }
                }

            std::vector<int> cuts;
            for (int j = (int) m - 1; j > 0 && from[(size_t) j] >= 0; j = from[(size_t) j])
                cuts.push_back (node[(size_t) j]);
            cuts.push_back (a);
            std::reverse (cuts.begin(), cuts.end());
            for (size_t k = 0; k + 1 < cuts.size(); ++k)
                segs.push_back ({ cuts[k], cuts[k + 1] });
        }

        // 5) Give the outer edges of each run a little air, so attacks and tails are inside the blob.
        const int pad = juce::jlimit (1, 12, (int) std::round (T * 0.1));
        for (size_t k = 0; k < segs.size(); ++k)
        {
            auto& sgm = segs[k];
            const int prevEnd = k > 0 ? segs[k - 1].second : 0;
            const int nextStart = k + 1 < segs.size() ? segs[k + 1].first : numHops;
            if (sgm.first > prevEnd)  sgm.first  = juce::jmax (prevEnd, sgm.first - pad);
            if (sgm.second < nextStart) sgm.second = juce::jmin (nextStart, sgm.second + pad);
        }

        for (auto& sgm : segs)
        {
            Segment b;
            b.start = start + (int64_t) sgm.first * hop;
            b.end = juce::jmin (end, start + (int64_t) sgm.second * hop);
            if (b.end > b.start)
                result.push_back (b);
        }
        return result;
    }

    float resolutionForZoom (double spp, double sr)
    {
        // aim for blobs about 36 px wide on screen
        const auto targetMs = juce::jmax (1.0, 36.0 * spp / sr * 1000.0);
        const auto r = std::log (400.0 / targetMs) / std::log (400.0);
        return (float) juce::jlimit (0.0, 1.0, r);
    }

    std::vector<Peak> findPeaks (const CaptureStore& audio, const EditState& edits, int64_t start, int64_t end, float thr)
    {
        std::vector<Peak> out;
        if (end <= start || ! audio.hasData())
            return out;

        start = juce::jmax (start, audio.getWrittenStart());
        end = juce::jmin (end, audio.getWrittenEnd());
        const auto sr = audio.getSampleRate();
        const auto mergeGap = (int64_t) (sr * 0.025);   // one peak = one burst, not every wave cycle
        auto map = EditOps::buildGainMap (edits, sr);

        constexpr int block = 8192;
        std::vector<float> buf (block);
        Peak cur;
        bool open = false;

        for (auto p = start; p < end; p += block)
        {
            const auto n = (int) juce::jmin ((int64_t) block, end - p);
            audio.read (p, buf.data(), n);
            for (int k = 0; k < n; ++k)
            {
                const auto pos = p + k;
                const auto v = std::abs (buf[(size_t) k] * map->gainAt (pos));
                if (v <= thr)
                    continue;

                if (open && pos - cur.end <= mergeGap)
                {
                    cur.end = pos + 1;
                    if (v > cur.level) { cur.level = v; cur.at = pos; }
                }
                else
                {
                    if (open) out.push_back (cur);
                    cur = { pos, pos + 1, pos, v };
                    open = true;
                }
            }
        }
        if (open) out.push_back (cur);
        return out;
    }

    void peakRegion (const Peak& p, double sr, int64_t& a, int64_t& b)
    {
        const auto pad = (int64_t) (sr * (EditOps::edgeFadeSeconds + 0.002));
        a = p.start - pad;
        b = p.end + pad;
    }

    void applyAutoReduction (EditState& edits, const CaptureStore& audio, float thr, float reductionDb)
    {
        edits.spots.erase (std::remove_if (edits.spots.begin(), edits.spots.end(),
                                           [] (const SpotEdit& s) { return s.kind == SpotEdit::peakAuto; }),
                           edits.spots.end());
        if (std::abs (reductionDb) < 0.01f || ! audio.hasData())
            return;

        const auto sr = audio.getSampleRate();
        auto peaks = findPeaks (audio, edits, audio.getWrittenStart(), audio.getWrittenEnd(), thr);
        for (auto& pk : peaks)
        {
            int64_t a, b;
            peakRegion (pk, sr, a, b);
            const bool manual = std::any_of (edits.spots.begin(), edits.spots.end(), [&] (const SpotEdit& s)
                                             { return s.kind == SpotEdit::peak && s.start < b && s.end > a; });
            if (! manual)
                edits.spots.push_back ({ a, b, reductionDb, SpotEdit::peakAuto });
        }
        EditOps::sortSpots (edits);
    }

    std::vector<Sibilant> detectSibilance (const CaptureStore& audio, int64_t start, int64_t end, float sensitivity, float freq)
    {
        std::vector<Sibilant> out;
        if (! audio.hasData()) return out;
        start = juce::jmax (start, audio.getWrittenStart());
        end = juce::jmin (end, audio.getWrittenEnd());
        if (end <= start) return out;

        const auto sr = audio.getSampleRate();
        const auto sens = (double) juce::jlimit (0.0f, 1.0f, sensitivity);
        const auto ratioNeeded = 0.72 - 0.40 * sens;                   // share of the energy that must be in the highs
        const auto levelNeeded = juce::Decibels::decibelsToGain (-38.0 - 14.0 * sens);   // the highs must also be audible
        const int frame = juce::jmax (32, (int) std::round (sr * 0.005));
        const auto minLen = (int64_t) (sr * 0.02), maxGap = (int64_t) frame * 3;
        const auto pad = (int64_t) (sr * (EditOps::edgeFadeSeconds + 0.001));

        TrueHighPass4 hp;
        hp.setup (sr, freq);
        std::vector<float> buf ((size_t) frame);

        bool open = false;
        Sibilant cur;
        int64_t lastHit = 0;

        auto finish = [&]
        {
            if (open && cur.end - cur.start >= minLen)
            {
                cur.start -= pad; cur.end += pad;
                out.push_back (cur);
            }
            open = false;
        };

        for (auto p = start - frame * 4; p < end; p += frame)   // a little warm-up for the filter
        {
            audio.read (p, buf.data(), frame);
            double et = 0.0, eh = 0.0;
            float peak = 0.0f; int64_t peakAt = p;
            for (int k = 0; k < frame; ++k)
            {
                const auto x = buf[(size_t) k];
                const auto h = hp.process (x);
                et += (double) x * x;
                eh += (double) h * h;
                if (std::abs (x) > peak) { peak = std::abs (x); peakAt = p + k; }
            }
            if (p < start) continue;

            const auto hRms = std::sqrt (eh / frame);
            const bool hit = et > 1e-12 && eh / et >= ratioNeeded && hRms >= levelNeeded;
            if (hit)
            {
                if (open && p - lastHit <= maxGap)
                {
                    cur.end = p + frame;
                    if (peak > cur.level) { cur.level = peak; cur.at = peakAt; }
                }
                else
                {
                    finish();
                    cur = { p, p + frame, peakAt, peak };
                    open = true;
                }
                lastHit = p;
            }
            else if (open && p - lastHit > maxGap)
            {
                finish();
            }
        }
        finish();
        return out;
    }

    void applyAutoSibReduction (EditState& edits, const CaptureStore& audio, float sensitivity, float reductionDb)
    {
        edits.spots.erase (std::remove_if (edits.spots.begin(), edits.spots.end(),
                                           [] (const SpotEdit& s) { return s.kind == SpotEdit::sibAuto; }),
                           edits.spots.end());
        if (std::abs (reductionDb) < 0.01f || ! audio.hasData())
            return;

        auto found = detectSibilance (audio, audio.getWrittenStart(), audio.getWrittenEnd(), sensitivity, edits.sibFreq);
        for (auto& sb : found)
        {
            const bool manual = std::any_of (edits.spots.begin(), edits.spots.end(), [&] (const SpotEdit& s)
                                             { return s.kind == SpotEdit::sib && s.start < sb.end && s.end > sb.start; });
            if (! manual)
                edits.spots.push_back ({ sb.start, sb.end, reductionDb, SpotEdit::sibAuto });
        }
        EditOps::sortSpots (edits);
    }

    float levelAfterEdits (const CaptureStore& audio, const EditState& edits, int64_t a, int64_t b)
    {
        float m = 0.0f;
        const auto sr = audio.getSampleRate();
        for (auto p = a; p < b; ++p)
            m = juce::jmax (m, std::abs (audio.get (p)) * juce::Decibels::decibelsToGain (EditOps::displayDbAt (edits, p, sr), -120.0f));
        return m;
    }

    int tamePeaks (EditState& edits, const CaptureStore& audio, int64_t start, int64_t end, float thr)
    {
        const auto sr = audio.getSampleRate();
        int count = 0;

        // two passes: overlapping soft edges can leave a peak a hair over the line
        for (int pass = 0; pass < 3; ++pass)
        {
            auto peaks = findPeaks (audio, edits, start, end, thr * 1.0005f);
            if (peaks.empty())
                break;

            for (auto& pk : peaks)
            {
                int64_t a, b;
                peakRegion (pk, sr, a, b);
                const auto idx = EditOps::ensureSpot (edits, a, b, SpotEdit::peak);
                const auto needed = juce::Decibels::gainToDecibels (thr / pk.level) - 0.05f;
                edits.spots[(size_t) idx].db += needed;
                if (pass == 0) ++count;
            }
        }
        return count;
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
        auto sibMap = EditOps::buildSibMap (edits, sr);
        HighPass4 hp;
        hp.setup (sr, edits.sibFreq);
        // the processed sample, exactly as the plugin plays it
        auto process = [&] (float x, int64_t pos)
        {
            const auto h = hp.process (x);
            const auto gs = sibMap->gainAt (pos);
            return (x - (1.0f - gs) * h) * map->gainAt (pos);
        };

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
                now.warm (process (buf[(size_t) k], p + k));
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
                now.add (process (buf[(size_t) k], p + k));
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
