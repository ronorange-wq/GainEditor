#include "EditModel.h"
#include <algorithm>
#include <cmath>

//==============================================================================
bool EditState::operator== (const EditState& o) const
{
    auto sameRanges = ranges.size() == o.ranges.size()
        && std::equal (ranges.begin(), ranges.end(), o.ranges.begin(), [] (const RangeEdit& x, const RangeEdit& y)
               { return x.start == y.start && x.end == y.end && juce::exactlyEqual (x.db, y.db); });
    auto sameEnv = envelope.size() == o.envelope.size()
        && std::equal (envelope.begin(), envelope.end(), o.envelope.begin(), [] (const EnvPoint& x, const EnvPoint& y)
               { return x.pos == y.pos && juce::exactlyEqual (x.db, y.db); });
    auto sameSpots = spots.size() == o.spots.size()
        && std::equal (spots.begin(), spots.end(), o.spots.begin(), [] (const SpotEdit& x, const SpotEdit& y)
               { return x.start == y.start && x.end == y.end && juce::exactlyEqual (x.db, y.db) && x.kind == y.kind; });
    return sameRanges && sameEnv && sameSpots && sibFilter == o.sibFilter && juce::exactlyEqual (sibFreq, o.sibFreq);
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
    v.setProperty ("format", 2, nullptr);
    v.setProperty ("sibFilter", sibFilter, nullptr);
    v.setProperty ("sibFreq", sibFreq, nullptr);
    v.setProperty ("ranges", encodeList (ranges, [] (juce::MemoryOutputStream& o, const RangeEdit& r)
                   { o.writeInt64 (r.start); o.writeInt64 (r.end); o.writeFloat (r.db); }), nullptr);
    v.setProperty ("envelope", encodeList (envelope, [] (juce::MemoryOutputStream& o, const EnvPoint& p)
                   { o.writeInt64 (p.pos); o.writeFloat (p.db); }), nullptr);
    v.setProperty ("spots", encodeList (spots, [] (juce::MemoryOutputStream& o, const SpotEdit& b)
                   { o.writeInt64 (b.start); o.writeInt64 (b.end); o.writeFloat (b.db); o.writeByte ((char) b.kind); }), nullptr);
    return v;
}

EditState EditState::fromValueTree (const juce::ValueTree& v)
{
    EditState s;
    if (! v.hasType ("EDITS"))
        return s;
    s.sibFilter = (bool) v.getProperty ("sibFilter", false);
    s.sibFreq = (float) v.getProperty ("sibFreq", 5000.0f);

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
    if (v.hasProperty ("spots"))
    {
        auto mb = decode (v.getProperty ("spots").toString());
        juce::MemoryInputStream in (mb, false);
        while (in.getNumBytesRemaining() >= 21)
        {
            SpotEdit b;
            b.start = in.readInt64(); b.end = in.readInt64(); b.db = in.readFloat(); b.kind = (int) in.readByte();
            s.spots.push_back (b);
        }
    }
    else if (v.hasProperty ("blobs"))
    {
        // version 1.0 / 1.1: blobs that had a gain become locked spots
        auto mb = decode (v.getProperty ("blobs").toString());
        juce::MemoryInputStream in (mb, false);
        while (in.getNumBytesRemaining() >= 20)
        {
            SpotEdit b;
            b.start = in.readInt64(); b.end = in.readInt64(); b.db = in.readFloat();
            if (std::abs (b.db) > 0.0001f)
                s.spots.push_back (b);
        }
    }

    EditOps::sortEnvelope (s);
    EditOps::sortSpots (s);
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

        const auto d = juce::jmin ((double) (pos - start), (double) (end - 1 - pos));
        if (d >= fade)
            return 1.0f;

        return (float) (0.5 - 0.5 * std::cos (d / fade * juce::MathConstants<double>::pi));
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

    float spotDbAt (const EditState& s, int64_t pos, double sr)
    {
        float db = 0.0f;
        for (auto& b : s.spots)
        {
            if (b.start > pos) break;          // sorted by start
            if (s.sibFilter && SpotEdit::isSibilance (b.kind)) continue;   // goes to the highs only
            if (pos < b.end)
                db += b.db * edgeWeight (pos, b.start, b.end, sr);
        }
        return db;
    }

    float sibOnlyDbAt (const EditState& s, int64_t pos, double sr)
    {
        float db = 0.0f;
        for (auto& b : s.spots)
        {
            if (b.start > pos) break;
            if (SpotEdit::isSibilance (b.kind) && pos < b.end)
                db += b.db * edgeWeight (pos, b.start, b.end, sr);
        }
        return db;
    }

    float displayDbAt (const EditState& s, int64_t pos, double sr)
    {
        return totalDbAt (s, pos, sr) + (s.sibFilter ? sibOnlyDbAt (s, pos, sr) : 0.0f);
    }

    float totalDbAt (const EditState& s, int64_t pos, double sr)
    {
        return rangeDbAt (s, pos, sr) + envelopeDbAt (s, pos) + spotDbAt (s, pos, sr);
    }

    std::shared_ptr<GainMap> buildGainMap (const EditState& s, double sr)
    {
        auto map = std::make_shared<GainMap>();

        int64_t lo = std::numeric_limits<int64_t>::max(), hi = 0;
        auto extend = [&] (int64_t a, int64_t b) { lo = juce::jmin (lo, a); hi = juce::jmax (hi, b); };

        for (auto& r : s.ranges)   if (std::abs (r.db) > 0.0f) extend (r.start, r.end);
        for (auto& b : s.spots)    if (std::abs (b.db) > 0.0f && ! (s.sibFilter && SpotEdit::isSibilance (b.kind))) extend (b.start, b.end);
        for (auto& p : s.envelope) extend (p.pos, p.pos + 1);

        map->before = juce::Decibels::decibelsToGain (envelopeDbAt (s, std::numeric_limits<int64_t>::min() / 2));
        map->after  = juce::Decibels::decibelsToGain (envelopeDbAt (s, std::numeric_limits<int64_t>::max() / 2));

        if (hi <= lo)
            return map;

        lo = juce::jmax ((int64_t) 0, lo - GainMap::hop);
        hi += GainMap::hop * 2;
        map->origin = lo;

        const auto n = (size_t) ((hi - lo) / GainMap::hop + 1);
        std::vector<float> db (n, 0.0f);

        // envelope: walk the points once
        if (! s.envelope.empty())
            for (size_t i = 0; i < n; ++i)
                db[i] = envelopeDbAt (s, lo + (int64_t) i * GainMap::hop);

        // ranges and spots: add each one over its own span only
        auto addRegion = [&] (int64_t a, int64_t b, float value)
        {
            if (std::abs (value) <= 0.0f) return;
            const auto i0 = (size_t) juce::jmax ((int64_t) 0, (a - lo) / GainMap::hop);
            const auto i1 = (size_t) juce::jmin ((int64_t) n - 1, (b - lo) / GainMap::hop + 1);
            for (auto i = i0; i <= i1; ++i)
                db[i] += value * edgeWeight (lo + (int64_t) i * GainMap::hop, a, b, sr);
        };
        for (auto& r : s.ranges) addRegion (r.start, r.end, r.db);
        for (auto& b : s.spots)
            if (! (s.sibFilter && SpotEdit::isSibilance (b.kind)))
                addRegion (b.start, b.end, b.db);

        map->gains.resize (n);
        for (size_t i = 0; i < n; ++i)
            map->gains[i] = juce::Decibels::decibelsToGain (db[i], -120.0f);
        return map;
    }

    std::shared_ptr<GainMap> buildSibMap (const EditState& s, double sr)
    {
        auto map = std::make_shared<GainMap>();
        map->hpFreq = s.sibFreq;
        if (! s.sibFilter)
            return map;

        int64_t lo = std::numeric_limits<int64_t>::max(), hi = 0;
        for (auto& b : s.spots)
            if (SpotEdit::isSibilance (b.kind) && std::abs (b.db) > 0.0f) { lo = juce::jmin (lo, b.start); hi = juce::jmax (hi, b.end); }
        if (hi <= lo)
            return map;

        lo = juce::jmax ((int64_t) 0, lo - GainMap::hop);
        hi += GainMap::hop * 2;
        map->origin = lo;
        const auto n = (size_t) ((hi - lo) / GainMap::hop + 1);
        std::vector<float> db (n, 0.0f);
        for (auto& b : s.spots)
        {
            if (! SpotEdit::isSibilance (b.kind)) continue;
            const auto i0 = (size_t) juce::jmax ((int64_t) 0, (b.start - lo) / GainMap::hop);
            const auto i1 = (size_t) juce::jmin ((int64_t) n - 1, (b.end - lo) / GainMap::hop + 1);
            for (auto i = i0; i <= i1; ++i)
                db[i] += b.db * edgeWeight (lo + (int64_t) i * GainMap::hop, b.start, b.end, sr);
        }
        map->gains.resize (n);
        for (size_t i = 0; i < n; ++i)
            map->gains[i] = juce::Decibels::decibelsToGain (db[i], -120.0f);
        return map;
    }

    void sortEnvelope (EditState& s)
    {
        std::stable_sort (s.envelope.begin(), s.envelope.end(), [] (auto& a, auto& b) { return a.pos < b.pos; });
    }

    void sortSpots (EditState& s)
    {
        std::stable_sort (s.spots.begin(), s.spots.end(), [] (auto& a, auto& b)
                          { return a.start != b.start ? a.start < b.start : a.end > b.end; });
    }

    int findSpot (const EditState& s, int64_t start, int64_t end, int kind)
    {
        for (int i = 0; i < (int) s.spots.size(); ++i)
            if (s.spots[(size_t) i].sameRegion (start, end) && s.spots[(size_t) i].kind == kind)
                return i;
        return -1;
    }

    int ensureSpot (EditState& s, int64_t start, int64_t end, int kind)
    {
        auto i = findSpot (s, start, end, kind);
        if (i >= 0)
            return i;
        s.spots.push_back ({ start, end, 0.0f, kind });
        sortSpots (s);
        return findSpot (s, start, end, kind);
    }

    void pruneSpots (EditState& s)
    {
        s.spots.erase (std::remove_if (s.spots.begin(), s.spots.end(), [] (const SpotEdit& b)
                                       { return std::abs (b.db) < 0.0005f || b.end <= b.start; }),
                       s.spots.end());
    }

    EditState resetRegion (const EditState& in, int64_t a, int64_t b, ResetWhat what, double sr)
    {
        if (b <= a)
            return in;

        EditState s = in;

        auto clip = [a, b] (auto& list)
        {
            using T = typename std::decay_t<decltype (list)>::value_type;
            std::vector<T> out;
            for (auto& r : list)
            {
                if (r.end <= a || r.start >= b) { out.push_back (r); continue; }
                if (r.start < a) { T p = r; p.end = a;   out.push_back (p); }
                if (r.end > b)   { T p = r; p.start = b; out.push_back (p); }
            }
            list = std::move (out);
        };

        if (what == resetEverything || what == resetRanges)
            clip (s.ranges);

        if (what == resetEverything || what == resetSpots)
        {
            clip (s.spots);
            sortSpots (s);
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

            if (std::all_of (s.envelope.begin(), s.envelope.end(), [] (auto& p) { return std::abs (p.db) < 0.001f; }))
                s.envelope.clear();
        }

        return s;
    }

    EditState resetAll (const EditState& in)
    {
        EditState s;
        s.sibFilter = in.sibFilter;
        s.sibFreq = in.sibFreq;
        return s;
    }
}
