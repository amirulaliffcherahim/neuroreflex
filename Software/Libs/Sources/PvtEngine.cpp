#include "PvtEngine.hpp"

namespace Pvt
{

namespace
{
/// Share of trials that are lapses, in percent, at which the verdict worsens.
constexpr uint32_t kCautionPercent  = 10;
constexpr uint32_t kFatiguedPercent = 20;
} // namespace

Engine::Engine(uint32_t durationMs, uint32_t seed)
    : mDurationMs(durationMs)
    // Scramble small seeds so neighbouring seeds give unrelated delays;
    // xorshift must never hold zero.
    , mRng((seed * 2654435761u) ^ 0x9E3779B9u)
{
    if (mRng == 0) {
        mRng = 1;
    }
}

uint32_t Engine::nextRandom()
{
    mRng ^= mRng << 13;
    mRng ^= mRng >> 17;
    mRng ^= mRng << 5;
    return mRng;
}

void Engine::scheduleNext(uint32_t nowMs)
{
    mStimulusAtMs = nowMs + kMinDelayMs + (nextRandom() % kDelaySpanMs);
}

void Engine::record(uint32_t ms)
{
    if (mCount < kMaxTrials) {
        mResponses[mCount++] = static_cast<uint16_t>(ms > 0xFFFF ? 0xFFFF : ms);
    }
}

void Engine::start(uint32_t nowMs)
{
    mStartMs     = nowMs;
    mPhase       = Phase::WAITING;
    mOutcome     = Outcome::NONE;
    mLastMs      = 0;
    mFalseStarts = 0;
    mCount       = 0;
    scheduleNext(nowMs);
}

uint32_t Engine::remainingMs(uint32_t nowMs) const
{
    if (mPhase == Phase::IDLE) {
        return mDurationMs;
    }
    const uint32_t elapsed = nowMs - mStartMs;
    return elapsed >= mDurationMs ? 0 : mDurationMs - elapsed;
}

uint32_t Engine::stimulusElapsedMs(uint32_t nowMs) const
{
    if (mPhase != Phase::STIMULUS || nowMs < mStimulusAtMs) {
        return 0;
    }
    return nowMs - mStimulusAtMs;
}

void Engine::tick(uint32_t nowMs)
{
    // A session is never cut off mid-stimulus: it ends between trials, or
    // when the answer buffer is full.
    const bool over = remainingMs(nowMs) == 0 || mCount >= kMaxTrials;

    switch (mPhase) {
        case Phase::WAITING:
            if (over) {
                mPhase = Phase::DONE;
            } else if (nowMs >= mStimulusAtMs) {
                mPhase = Phase::STIMULUS;
            }
            break;

        case Phase::STIMULUS:
            if (nowMs - mStimulusAtMs >= kTimeoutMs) {
                record(kTimeoutMs);
                mLastMs        = kTimeoutMs;
                mOutcome       = Outcome::LAPSE;
                mPhase         = Phase::FEEDBACK;
                mFeedbackEndMs = nowMs + kFeedbackMs;
            }
            break;

        case Phase::FEEDBACK:
            if (nowMs >= mFeedbackEndMs) {
                if (over) {
                    mPhase = Phase::DONE;
                } else {
                    mPhase = Phase::WAITING;
                    scheduleNext(nowMs);
                }
            }
            break;

        case Phase::IDLE:
        case Phase::DONE:
            break;
    }
}

void Engine::press(uint32_t nowMs)
{
    switch (mPhase) {
        case Phase::WAITING:
            ++mFalseStarts;
            mOutcome = Outcome::FALSE_START;
            scheduleNext(nowMs);               // a jumped gun restarts the wait
            break;

        case Phase::STIMULUS: {
            const uint32_t ms = nowMs - mStimulusAtMs;
            record(ms);
            mLastMs        = ms;
            mOutcome       = ms >= kLapseMs ? Outcome::LAPSE : Outcome::OK;
            mPhase         = Phase::FEEDBACK;
            mFeedbackEndMs = nowMs + kFeedbackMs;
            break;
        }

        case Phase::IDLE:
        case Phase::FEEDBACK:
        case Phase::DONE:
            break;
    }
}

uint32_t Engine::responseMs(uint32_t i) const
{
    return i < mCount ? mResponses[i] : 0;
}

Summary Engine::summary() const
{
    Summary s;
    s.trials      = mCount;
    s.falseStarts = mFalseStarts;
    if (mCount == 0) {
        return s;
    }

    uint16_t sorted[kMaxTrials];
    for (uint32_t i = 0; i < mCount; ++i) {
        sorted[i] = mResponses[i];
        if (mResponses[i] >= kLapseMs) {
            ++s.lapses;
        }
    }
    for (uint32_t i = 1; i < mCount; ++i) {    // insertion sort, n <= 96
        const uint16_t v = sorted[i];
        uint32_t j = i;
        for (; j > 0 && sorted[j - 1] > v; --j) {
            sorted[j] = sorted[j - 1];
        }
        sorted[j] = v;
    }

    s.fastestMs = sorted[0];
    s.medianMs  = (mCount % 2 == 1)
                      ? sorted[mCount / 2]
                      : (static_cast<uint32_t>(sorted[mCount / 2 - 1]) + sorted[mCount / 2]) / 2;

    if (mCount < kMinTrials) {
        s.verdict = Verdict::NOT_ENOUGH_DATA;
    } else if (s.lapses * 100 >= mCount * kFatiguedPercent) {
        s.verdict = Verdict::FATIGUED;
    } else if (s.lapses * 100 >= mCount * kCautionPercent) {
        s.verdict = Verdict::CAUTION;
    } else {
        s.verdict = Verdict::READY;
    }
    return s;
}

} // namespace Pvt
