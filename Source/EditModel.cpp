#include "EditModel.h"
#include <algorithm>
#include <cmath>

//==============================================================================
bool EditState::operator== (const EditState& o) const
{
    auto sameRanges = ranges.size() == o.ranges.size()
        && std::equal (ranges.begin(), ranges.end(), o.ranges.begin(), [] (const RangeEdit& x, const RangeEdit& y)
               { return x.start == y.start && x.end == y.end && x.db == y.db; });
    auto sameEnv = envelope.size() == o.envelope.size()
        && std::equal (envelope.begin(), envelope.end(), o.envelope.begin(), [] (const EnvPoint& x, const EnvPoint& y)
               { return x.pos == y.pos && x.db == y.db; });
    auto sameBlobs = blobs.size() == o.blobs.size()
        && std::equal (blobs.begin(), blobs.end(), o.blobs.begin(), [] (const Blob& x, const Blob& y)
               { return x.start == y.start && x.end == y.end && x.db == y.db; });
    return sameRanges && sameEnv && sameBlobs && blobResolution == o.blobResolution;
}

namespace
{
    template <typename T, typename Writer>
    juce::String encodeList (const std::vector<T>& items, Writer w)
    {
        juce::MemoryOutputStream out;
        for (auto& it : items)
            w (out, it);
        return out.getMemoryBlock().toBase64Encoding();
    }

    juce::MemoryBlock decode (const juce::String& s)
    {
        juce::MemoryBlock mb;
        mb.fromBase64Encoding (s);
        return mb;
    }
}

juce::ValueTree EditState::toValueTree() const
{
    juce::ValueTree v ("EDITS");
    v.setProperty ("blobResolution", blobResolution, nullptr);

    v.setProperty ("ranges", encodeList (ranges, [] (juce::MemoryOutputStream& o, const RangeEdit& r)
                   { o.writeInt64 (r.start); o.writeInt64 (r.end); o.writeFloat (r.db); }), nullptr);
    v.setProperty ("envelope", encodeList (envelope, [] (juce::MemoryOutputStream& o, const EnvPoint& p)
                   { o.writeInt64 (p.pos); o.writeFloat (p.db); }), nullptr);
    v.setProperty ("blobs", encodeList (blobs, [] (juce::MemoryOutputStream& o, const Blob& b)
                   { o.writeInt64 (b.start); o.writeInt64 (b.end); o.writeFloat (b.db); }), nullptr);
    return v;
}

EditState EditState::fromValueTree (const juce::ValueTree& v)
{
    EditState s;
    if (! v.hasType ("EDITS"))
        return s;

    s.blobResolution = (float) v.getProperty ("blobResolution", 0.6f);

    {
        auto mb = decode (v.getProperty ("ranges").toString());
        juce::MemoryInputStream in (mb, false);
        while (in.getNumBytesRemaining() >= 20)
        {
            RangeEdit r;
            r.start = in.readInt64(); r.end = in.readInt64(); r.db = in.readFloat();
            s.ranges.push_back (r);
        }
    }
    {
        auto mb = decode (v.getProperty ("envelope").toString());
        juce::MemoryInputStream in (mb, false);
        while (in.getNumBytesRemaining() >= 12)
        {
            EnvPoint p;
            p.pos = in.readInt64(); p.db = in.readFloat();
            s.envelope.push_back (p);
        }
    }
    {
        auto mb = decode (v.getProperty ("blobs").toString());
        juce::MemoryInputStream in (mb, false);
        while (in.getNumBytesRemaining() >= 20)
        {
            Blob b;
            b.start = in.readInt64(); b.end = in.readInt64(); b.db = in.readFloat();
            s.blobs.push_back (b);
        }
    }
    EditOps::sortEnvelope (s);
    EditOps::sortBlobs (s);
    return s;
}

//==============================================================================
float GainMap::gainAt (int64_t pos) const noexcept
{
    if (gains.empty())
        return before;

    const auto rel = pos - origin;
    if (rel < 0)
        return before;

    const auto idx = rel / hop;
    if (idx >= (int64_t) gains.size() - 1)
        return idx >= (int64_t) gains.size() ? after : gains.back();

    const auto frac = (float) (rel - idx * hop) / (float) hop;
    const auto g0 = gains[(size_t) idx], g1 = gains[(size_t) idx + 1];
    return g0 + (g1 - g0) * frac;
}

//==============================================================================
namespace EditOps
{
    static float edgeWeight (int64_t pos, int64_t start, int64_t end, double sampleRate)
    {
        if (pos < start || pos >= end)
            return 0.0f;

        const auto len = (double) (end - start);
        const auto fade = juce::jmin ((double) edgeFadeSeconds * sampleRate, len * 0.5);
        if (fade < 1.0)
            return 1.0f;

        const auto fromStart = (double) (pos - start);
        const auto toEnd = (double) (end - 1 - pos);
        const auto d = juce::jmin (fromStart, toEnd);
        if (d >= fade)
            return 1.0f;

        const auto t = d / fade;                         // 0..1
        return (float) (0.5 - 0.5 * std::cos (t * juce::MathConstants<double>::pi));
    }

    float rangeDbAt (const EditState& s, int64_t pos, double sr)
    {
        float db = 0.0f;
        for (auto& r : s.ranges)
            if (pos >= r.start && pos < r.end)
                db += r.db * edgeWeight (pos, r.start, r.end, sr);
        return db;
    }

    float envelopeDbAt (const EditState& s, int64_t pos)
    {
        const auto& e = s.envelope;
        if (e.empty())
            return 0.0f;
        if (pos <= e.front().pos)
            return e.front().db;
        if (pos >= e.back().pos)
            return e.back().db;

        auto it = std::upper_bound (e.begin(), e.end(), pos, [] (int64_t p, const EnvPoint& q) { return p < q.pos; });
        const auto& b = *it;
        const auto& a = *(it - 1);
        if (b.pos == a.pos)
            return b.db;
        const auto t = (float) (pos - a.pos) / (float) (b.pos - a.pos);
        return a.db + (b.db - a.db) * t;
    }

    float blobDbAt (const EditState& s, int64_t pos, double sr)
    {
        const auto& bl = s.blobs;
        auto it = std::upper_bound (bl.begin(), bl.end(), pos, [] (int64_t p, const Blob& b) { return p < b.start; });
        if (it == bl.begin())
            return 0.0f;
        const auto& b = *(it - 1);
        if (pos >= b.end || b.db == 0.0f)
            return 0.0f;
        return b.db * edgeWeight (pos, b.start, b.end, sr);
    }

    float totalDbAt (const EditState& s, int64_t pos, double sr)
    {
        return rangeDbAt (s, pos, sr) + envelopeDbAt (s, pos) + blobDbAt (s, pos, sr);
    }

    std::shared_ptr<GainMap> buildGainMap (const EditState& s, double sr)
    {
        auto map = std::make_shared<GainMap>();

        int64_t lo = std::numeric_limits<int64_t>::max(), hi = 0;
        auto extend = [&] (int64_t a, int64_t b) { lo = juce::jmin (lo, a); hi = juce::jmax (hi, b); };

        for (auto& r : s.ranges)   if (r.db != 0.0f) extend (r.start, r.end);
        for (auto& b : s.blobs)    if (b.db != 0.0f) extend (b.start, b.end);
        for (auto& p : s.envelope) extend (p.pos, p.pos + 1);

        const auto envBefore = juce::Decibels::decibelsToGain (envelopeDbAt (s, std::numeric_limits<int64_t>::min() / 2));
        const auto envAfter  = juce::Decibels::decibelsToGain (envelopeDbAt (s, std::numeric_limits<int64_t>::max() / 2));
        map->before = envBefore;
        map->after = envAfter;

        if (hi <= lo)
            return map;

        lo = juce::jmax ((int64_t) 0, lo - GainMap::hop);
        hi += GainMap::hop * 2;
        map->origin = lo;

        const auto n = (size_t) ((hi - lo) / GainMap::hop + 1);
        map->gains.resize (n);
        for (size_t i = 0; i < n; ++i)
        {
            const auto pos = lo + (int64_t) i * GainMap::hop;
            map->gains[i] = juce::Decibels::decibelsToGain (totalDbAt (s, pos, sr), -120.0f);
        }
        return map;
    }

    void sortEnvelope (EditState& s)
    {
        std::stable_sort (s.envelope.begin(), s.envelope.end(), [] (auto& a, auto& b) { return a.pos < b.pos; });
    }

    void sortBlobs (EditState& s)
    {
        std::sort (s.blobs.begin(), s.blobs.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    }

    EditState resetRegion (const EditState& in, int64_t a, int64_t b, ResetWhat what, double sr)
    {
        if (b <= a)
            return in;

        EditState s = in;

        if (what == resetEverything || what == resetRanges)
        {
            std::vector<RangeEdit> out;
            for (auto& r : s.ranges)
            {
                if (r.end <= a || r.start >= b) { out.push_back (r); continue; }
                if (r.start < a) out.push_back ({ r.start, a, r.db });
                if (r.end > b)   out.push_back ({ b, r.end, r.db });
            }
            s.ranges = std::move (out);
        }

        if ((what == resetEverything || what == resetEnvelope) && ! s.envelope.empty())
        {
            const auto fade = (int64_t) (edgeFadeSeconds * sr * 2.0);
            const auto pa = a - fade, pb = b + fade;
            const auto va = envelopeDbAt (s, pa);
            const auto vb = envelopeDbAt (s, pb);

            std::vector<EnvPoint> out;
            for (auto& p : s.envelope)
                if (p.pos < pa || p.pos > pb)
                    out.push_back (p);

            out.push_back ({ pa, va });
            out.push_back ({ a, 0.0f });
            out.push_back ({ b, 0.0f });
            out.push_back ({ pb, vb });
            s.envelope = std::move (out);
            sortEnvelope (s);

            // Drop points that no longer do anything (a flat 0 dB line).
            bool allZero = std::all_of (s.envelope.begin(), s.envelope.end(), [] (auto& p) { return std::abs (p.db) < 0.001f; });
            if (allZero)
                s.envelope.clear();
        }

        if (what == resetEverything || what == resetBlobs)
        {
            std::vector<Blob> out;
            for (auto& bl : s.blobs)
            {
                if (bl.end <= a || bl.start >= b || bl.db == 0.0f) { out.push_back (bl); continue; }
                if (bl.start < a) out.push_back ({ bl.start, a, bl.db });
                out.push_back ({ juce::jmax (bl.start, a), juce::jmin (bl.end, b), 0.0f });
                if (bl.end > b) out.push_back ({ b, bl.end, bl.db });
            }
            s.blobs = std::move (out);
            sortBlobs (s);
        }

        return s;
    }

    EditState resetAll (const EditState& in)
    {
        EditState s = in;
        s.ranges.clear();
        s.envelope.clear();
        for (auto& b : s.blobs)
            b.db = 0.0f;
        return s;
    }
}
