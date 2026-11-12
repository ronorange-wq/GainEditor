#pragma once
#include <cmath>

/**
    "The highs" of a signal, made as x minus a 4th-order low-pass. Because high = x - low, the two parts
    always add back to exactly x, so lowering only the highs never smears the phase of the rest.
*/
struct HighPass4
{
    struct Stage
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set (double sr, double f, double q)
        {
            const auto w = 2.0 * 3.14159265358979323846 * f / sr;
            const auto c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            const auto a0 = 1.0 + alpha;
            b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0;   // low-pass
            a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
        }
        double process (double x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1 = z2 = 0; }
    };

    Stage s1, s2;
    double freq = -1.0, rate = -1.0;

    void setup (double sampleRate, double cutoff)
    {
        if (cutoff == freq && sampleRate == rate) return;
        freq = cutoff; rate = sampleRate;
        const auto f = cutoff < sampleRate * 0.45 ? cutoff : sampleRate * 0.45;
        s1.set (sampleRate, f, 0.54119610);
        s2.set (sampleRate, f, 1.30656296);
    }
    float process (float x) noexcept { return (float) ((double) x - s2.process (s1.process ((double) x))); }
    void reset() noexcept { s1.reset(); s2.reset(); }
};

/** A real 4th-order Butterworth high-pass: used to *measure* how much of a sound is in the highs. */
struct TrueHighPass4
{
    HighPass4::Stage s1, s2;

    void setup (double sampleRate, double cutoff)
    {
        const auto f = cutoff < sampleRate * 0.45 ? cutoff : sampleRate * 0.45;
        auto hp = [&] (HighPass4::Stage& st, double q)
        {
            const auto w = 2.0 * 3.14159265358979323846 * f / sampleRate;
            const auto c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            const auto a0 = 1.0 + alpha;
            st.b0 = (1.0 + c) * 0.5 / a0; st.b1 = -(1.0 + c) / a0; st.b2 = st.b0;
            st.a1 = -2.0 * c / a0; st.a2 = (1.0 - alpha) / a0;
        };
        hp (s1, 0.54119610);
        hp (s2, 1.30656296);
    }
    float process (float x) noexcept { return (float) s2.process (s1.process ((double) x)); }
};
