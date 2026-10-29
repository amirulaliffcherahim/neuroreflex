/**
 ******************************************************************************
 * @file    PvtEngine.hpp
 * @brief   PVT-B (Psychomotor Vigilance Task, brief) trial engine and scoring.
 *
 * Pure logic: no kernel, no display. Time is passed in, so the same engine
 * runs in the simulator, on the watch and in host tests.
 *
 * Flow: WAITING (random delay) -> STIMULUS (counter runs, press to stop)
 *       -> FEEDBACK (result shown) -> WAITING ... until the session is over.
 ******************************************************************************
 */

#ifndef PVT_ENGINE_HPP
#define PVT_ENGINE_HPP

#include <cstdint>

namespace Pvt
{

/// A response this slow or slower is a lapse (Basner & Dinges 2011).
constexpr uint32_t kLapseMs = 500;

/// A stimulus nobody answered is closed after this long and scored a lapse.
constexpr uint32_t kTimeoutMs = 2000;

/// Delay before a stimulus: kMinDelayMs .. kMinDelayMs + kDelaySpanMs - 1.
constexpr uint32_t kMinDelayMs  = 2000;
constexpr uint32_t kDelaySpanMs = 8000;

/// How long a result stays on screen before the next wait starts.
constexpr uint32_t kFeedbackMs = 1000;

enum class Phase : uint8_t { IDLE, WAITING, STIMULUS, FEEDBACK, DONE };

enum class Verdict : uint8_t { NOT_ENOUGH_DATA, READY, CAUTION, FATIGUED };

/// What the last trial was, for the screen.
enum class Outcome : uint8_t { NONE, OK, LAPSE, FALSE_START };

struct Summary
{
    uint32_t trials      = 0;   ///< Answered or timed-out stimuli
    uint32_t lapses      = 0;   ///< Of those, how many were >= kLapseMs
    uint32_t falseStarts = 0;   ///< Presses while nothing was showing
    uint32_t medianMs    = 0;   ///< Median response time (0 if no trials)
    uint32_t fastestMs   = 0;   ///< Fastest response (0 if no trials)
    Verdict  verdict     = Verdict::NOT_ENOUGH_DATA;
};

class Engine
{
public:
    /// Most trials a session can hold (3 min / 3 s shortest cycle, with margin).
    static constexpr uint32_t kMaxTrials = 96;

    /// Fewest trials the verdict is willing to judge on.
    static constexpr uint32_t kMinTrials = 8;

    /// @param durationMs  How long the session runs before it ends.
    /// @param seed        Seeds the random delays; same seed, same session.
    Engine(uint32_t durationMs, uint32_t seed);

    void start(uint32_t nowMs);

    /// Advance time. Call often; the engine moves phases when due.
    void tick(uint32_t nowMs);

    /// A button press at nowMs.
    void press(uint32_t nowMs);

    Phase   phase() const   { return mPhase; }
    Outcome outcome() const { return mOutcome; }

    /// Response time of the last answered or timed-out stimulus.
    uint32_t lastMs() const { return mLastMs; }

    /// Counter value to draw while a stimulus is showing.
    uint32_t stimulusElapsedMs(uint32_t nowMs) const;

    /// Session time left, 0 once over.
    uint32_t remainingMs(uint32_t nowMs) const;

    Summary summary() const;

    /// Response time of the i-th recorded trial, oldest first; 0 if out of range.
    uint32_t responseMs(uint32_t i) const;

private:
    void     scheduleNext(uint32_t nowMs);
    uint32_t nextRandom();
    void     record(uint32_t ms);

    uint32_t mDurationMs;
    uint32_t mRng;
    uint32_t mStartMs        = 0;
    uint32_t mStimulusAtMs   = 0;
    uint32_t mFeedbackEndMs  = 0;
    Phase    mPhase          = Phase::IDLE;
    Outcome  mOutcome        = Outcome::NONE;
    uint32_t mLastMs         = 0;
    uint32_t mFalseStarts    = 0;
    uint32_t mCount          = 0;
    uint16_t mResponses[kMaxTrials] = {};
};

} // namespace Pvt

#endif // PVT_ENGINE_HPP
