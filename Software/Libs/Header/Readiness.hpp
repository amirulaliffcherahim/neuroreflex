/**
 ******************************************************************************
 * @file    Readiness.hpp
 * @brief   Turns one PVT-B result into a call: compared with *your* normal,
 *          not with a population cut-off.
 *
 * Reaction speed differs a lot between people, so a fixed millisecond line
 * would call a naturally slow person "fatigued" every day. Sleep research
 * reads the PVT against the same person's own rested level instead; this
 * module does the same. The first kCalibration tests learn the normal
 * (median of the last kBaselineN medians); after that every test is scored
 * as a percentage against it. Lapses still count on their own: a session
 * with one lapse in five is a bad sign whatever the median says.
 *
 * Pure logic, no kernel: the history is passed in.
 ******************************************************************************
 */

#ifndef NR_READINESS_HPP
#define NR_READINESS_HPP

#include <cstdint>

namespace Rd
{

constexpr uint32_t kMaxHistory  = 14;   ///< tests kept (two weeks of daily checks)
constexpr uint32_t kBaselineN   = 7;    ///< most recent tests that define "normal"
constexpr uint32_t kCalibration = 3;    ///< tests needed before a personal call

// Thresholds against the personal normal. Day-to-day noise of a rested
// person's PVT median is a few percent; 10 % slower is a clear dip.
constexpr int32_t  kDrainedSlowerPct = 10;
constexpr int32_t  kPrimedFasterPct  = 4;
constexpr uint32_t kDrainedLapsePct  = 20;
constexpr uint32_t kPrimedLapsePct   = 5;

struct Entry
{
    uint16_t medianMs = 0;
    uint8_t  lapsePct = 0;
    uint8_t  state    = 0;   ///< State the test was given, for the trend colours
};

struct History
{
    uint32_t count = 0;
    Entry    e[kMaxHistory] = {};

    /// Append, dropping the oldest when full.
    void push(const Entry& x);
    /// i = 0 is the oldest kept.
    const Entry& at(uint32_t i) const { return e[i]; }
};

enum class State : uint8_t { NONE = 0, LEARNING, PRIMED, NORMAL, DRAINED };

struct Reading
{
    State    state      = State::NONE;
    uint32_t baselineMs = 0;      ///< 0 while learning
    int32_t  deltaPct   = 0;      ///< + slower, - faster than normal
    uint32_t lapsePct   = 0;
    uint32_t testNo     = 0;      ///< 1-based count including this test (for LEARNING)
    uint8_t  score      = 0;      ///< 0..100 for the gauge
};

/// Median of the last kBaselineN medians in @p h, 0 if fewer than kCalibration.
uint32_t baselineMs(const History& h);

/// Judge a finished test against the history *before* it.
/// @p trials < @p minTrials gives NONE (too short to judge, not saved).
Reading assess(const History& before, uint32_t medianMs, uint32_t lapses, uint32_t trials,
               uint32_t minTrials);

} // namespace Rd

#endif
