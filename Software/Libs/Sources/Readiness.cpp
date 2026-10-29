#include "Readiness.hpp"

namespace Rd
{

void History::push(const Entry& x)
{
    if (count == kMaxHistory) {
        for (uint32_t i = 1; i < kMaxHistory; ++i) {
            e[i - 1] = e[i];
        }
        --count;
    }
    e[count++] = x;
}

uint32_t baselineMs(const History& h)
{
    if (h.count < kCalibration) {
        return 0;
    }
    const uint32_t n    = h.count < kBaselineN ? h.count : kBaselineN;
    const uint32_t from = h.count - n;
    uint16_t v[kBaselineN];
    for (uint32_t i = 0; i < n; ++i) {
        v[i] = h.e[from + i].medianMs;
    }
    for (uint32_t i = 1; i < n; ++i) {            // insertion sort, n <= 7
        const uint16_t x = v[i];
        uint32_t j = i;
        for (; j > 0 && v[j - 1] > x; --j) {
            v[j] = v[j - 1];
        }
        v[j] = x;
    }
    return (n % 2) ? v[n / 2] : (static_cast<uint32_t>(v[n / 2 - 1]) + v[n / 2]) / 2;
}

Reading assess(const History& before, uint32_t medianMs, uint32_t lapses, uint32_t trials,
               uint32_t minTrials)
{
    Reading r;
    if (trials < minTrials || trials == 0) {
        return r;                                  // NO_DATA
    }
    r.lapsePct = (lapses * 100 + trials / 2) / trials;
    r.testNo   = before.count + 1;

    const uint32_t base = baselineMs(before);
    if (base == 0) {
        // Still learning. A lapse-heavy test is a bad sign on day one too.
        r.state = r.lapsePct >= kDrainedLapsePct ? State::DRAINED : State::LEARNING;
        r.score = static_cast<uint8_t>((r.testNo * 100) / kCalibration > 100 ? 100 : (r.testNo * 100) / kCalibration);
        return r;
    }

    r.baselineMs = base;
    const int32_t diff = static_cast<int32_t>(medianMs) - static_cast<int32_t>(base);
    // Round half away from zero so +9.6 % reads as 10 %.
    r.deltaPct = (diff * 100 + (diff >= 0 ? 1 : -1) * static_cast<int32_t>(base / 2)) / static_cast<int32_t>(base);

    if (r.deltaPct >= kDrainedSlowerPct || r.lapsePct >= kDrainedLapsePct) {
        r.state = State::DRAINED;
    } else if (r.deltaPct <= -kPrimedFasterPct && r.lapsePct <= kPrimedLapsePct) {
        r.state = State::PRIMED;
    } else {
        r.state = State::NORMAL;
    }

    // Gauge: 70 at your normal, +3 per percent faster, -3 per percent slower,
    // -1 per lapse percent. Clamped so the arc never wraps.
    int32_t s = 70 - 3 * r.deltaPct - static_cast<int32_t>(r.lapsePct);
    if (s < 4) s = 4;
    if (s > 100) s = 100;
    r.score = static_cast<uint8_t>(s);
    return r;
}

} // namespace Rd
