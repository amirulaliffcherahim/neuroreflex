#include <gtest/gtest.h>

#include "PvtEngine.hpp"

using namespace Pvt;

namespace
{
constexpr uint32_t kSession = 180000;   // 3 minutes
constexpr uint32_t kSeed    = 1234;

/// Tick 1 ms at a time until the stimulus shows; returns when. Fails the test
/// (returns 0) if it never appears within the longest legal delay.
uint32_t runToStimulus(Engine& e, uint32_t from)
{
    for (uint32_t t = from; t <= from + kMinDelayMs + kDelaySpanMs + 1; ++t) {
        e.tick(t);
        if (e.phase() == Phase::STIMULUS) {
            return t;
        }
    }
    return 0;
}

/// Run one full answered trial with the given reaction time. Returns the
/// time at which the feedback ended and waiting resumed.
uint32_t answerTrial(Engine& e, uint32_t from, uint32_t reactionMs)
{
    const uint32_t shown = runToStimulus(e, from);
    if (shown == 0) {
        // The only legitimate reason no stimulus came: the session ended.
        EXPECT_EQ(e.phase(), Phase::DONE);
        return from + kMinDelayMs + kDelaySpanMs + 1;   // where the scan stopped
    }
    e.press(shown + reactionMs);
    const uint32_t after = shown + reactionMs;
    e.tick(after + kFeedbackMs);
    return after + kFeedbackMs;
}
} // namespace

TEST(PvtEngine, StartsIdleAndBeginsWaitingOnStart)
{
    Engine e(kSession, kSeed);
    EXPECT_EQ(e.phase(), Phase::IDLE);
    e.start(0);
    EXPECT_EQ(e.phase(), Phase::WAITING);
}

TEST(PvtEngine, StimulusAppearsAfterTheRandomDelayWindow)
{
    Engine e(kSession, kSeed);
    e.start(0);
    e.tick(kMinDelayMs - 1);
    EXPECT_EQ(e.phase(), Phase::WAITING);          // never earlier than the minimum
    const uint32_t shown = runToStimulus(e, kMinDelayMs - 1);
    ASSERT_NE(shown, 0u);
    EXPECT_GE(shown, kMinDelayMs);
    EXPECT_LT(shown, kMinDelayMs + kDelaySpanMs);
}

TEST(PvtEngine, SameSeedGivesSameDelaysDifferentSeedDiffers)
{
    Engine a(kSession, 7), b(kSession, 7), c(kSession, 8);
    a.start(0); b.start(0); c.start(0);
    const uint32_t ta = runToStimulus(a, 0);
    const uint32_t tb = runToStimulus(b, 0);
    const uint32_t tc = runToStimulus(c, 0);
    EXPECT_EQ(ta, tb);
    EXPECT_NE(ta, tc);
}

TEST(PvtEngine, PressWhileStimulusShowsRecordsReactionTime)
{
    Engine e(kSession, kSeed);
    e.start(0);
    const uint32_t shown = runToStimulus(e, 0);
    e.press(shown + 273);
    EXPECT_EQ(e.phase(), Phase::FEEDBACK);
    EXPECT_EQ(e.lastMs(), 273u);
    EXPECT_EQ(e.outcome(), Outcome::OK);
}

TEST(PvtEngine, ReactionAtFiveHundredIsALapseJustUnderIsNot)
{
    Engine ok(kSession, kSeed), lapse(kSession, kSeed);
    ok.start(0); lapse.start(0);
    const uint32_t shown = runToStimulus(ok, 0);
    ASSERT_EQ(runToStimulus(lapse, 0), shown);
    ok.press(shown + kLapseMs - 1);
    lapse.press(shown + kLapseMs);
    EXPECT_EQ(ok.outcome(), Outcome::OK);
    EXPECT_EQ(lapse.outcome(), Outcome::LAPSE);
}

TEST(PvtEngine, PressBeforeStimulusIsAFalseStartAndKeepsWaiting)
{
    Engine e(kSession, kSeed);
    e.start(0);
    e.press(500);
    EXPECT_EQ(e.phase(), Phase::WAITING);
    EXPECT_EQ(e.outcome(), Outcome::FALSE_START);
    EXPECT_EQ(e.summary().falseStarts, 1u);
    EXPECT_EQ(e.summary().trials, 0u);             // not an answered trial
}

TEST(PvtEngine, FalseStartRestartsTheDelayFromThePress)
{
    Engine e(kSession, kSeed);
    e.start(0);
    e.press(5000);
    e.tick(5000 + kMinDelayMs - 1);
    EXPECT_EQ(e.phase(), Phase::WAITING);          // a fresh minimum wait applies
}

TEST(PvtEngine, UnansweredStimulusTimesOutAsALapse)
{
    Engine e(kSession, kSeed);
    e.start(0);
    const uint32_t shown = runToStimulus(e, 0);
    e.tick(shown + kTimeoutMs - 1);
    EXPECT_EQ(e.phase(), Phase::STIMULUS);
    e.tick(shown + kTimeoutMs);
    EXPECT_EQ(e.phase(), Phase::FEEDBACK);
    EXPECT_EQ(e.outcome(), Outcome::LAPSE);
    EXPECT_EQ(e.lastMs(), kTimeoutMs);
    EXPECT_EQ(e.summary().trials, 1u);
    EXPECT_EQ(e.summary().lapses, 1u);
}

TEST(PvtEngine, StimulusCounterReportsElapsedSinceItAppeared)
{
    Engine e(kSession, kSeed);
    e.start(0);
    const uint32_t shown = runToStimulus(e, 0);
    EXPECT_EQ(e.stimulusElapsedMs(shown + 120), 120u);
}

TEST(PvtEngine, FeedbackReturnsToWaitingAfterTheFeedbackWindow)
{
    Engine e(kSession, kSeed);
    e.start(0);
    const uint32_t shown = runToStimulus(e, 0);
    e.press(shown + 300);
    e.tick(shown + 300 + kFeedbackMs - 1);
    EXPECT_EQ(e.phase(), Phase::FEEDBACK);
    e.tick(shown + 300 + kFeedbackMs);
    EXPECT_EQ(e.phase(), Phase::WAITING);
}

TEST(PvtEngine, SessionEndsOnceDurationPassesButNeverMidStimulus)
{
    Engine e(20000, kSeed);                        // short session
    e.start(0);
    uint32_t t = 0;
    for (int i = 0; i < 20 && e.phase() != Phase::DONE; ++i) {
        t = answerTrial(e, t, 250);
        e.tick(t);
    }
    EXPECT_EQ(e.phase(), Phase::DONE);
    EXPECT_GE(t, 20000u);
    EXPECT_EQ(e.remainingMs(t), 0u);
}

TEST(PvtEngine, PressAfterDoneChangesNothing)
{
    Engine e(1000, kSeed);
    e.start(0);
    e.tick(1000);
    ASSERT_EQ(e.phase(), Phase::DONE);
    const Summary before = e.summary();
    e.press(1100);
    EXPECT_EQ(e.summary().falseStarts, before.falseStarts);
    EXPECT_EQ(e.phase(), Phase::DONE);
}

TEST(PvtEngine, RemainingTimeCountsDown)
{
    Engine e(kSession, kSeed);
    e.start(1000);
    EXPECT_EQ(e.remainingMs(1000), kSession);
    EXPECT_EQ(e.remainingMs(1000 + 60000), kSession - 60000);
    EXPECT_EQ(e.remainingMs(1000 + kSession + 5), 0u);
}

TEST(PvtEngine, ExposesEachRecordedTrialInOrderAndZeroOutOfRange)
{
    Engine e(kSession, kSeed);
    e.start(0);
    uint32_t t = 0;
    for (uint32_t ms : {200u, 700u, 350u}) {
        t = answerTrial(e, t, ms);
    }
    EXPECT_EQ(e.responseMs(0), 200u);
    EXPECT_EQ(e.responseMs(1), 700u);
    EXPECT_EQ(e.responseMs(2), 350u);
    EXPECT_EQ(e.responseMs(3), 0u);                // not recorded yet
    EXPECT_EQ(e.responseMs(Engine::kMaxTrials + 5), 0u);
}

TEST(PvtEngine, TimedOutTrialIsRecordedAtTheTimeoutValue)
{
    Engine e(kSession, kSeed);
    e.start(0);
    const uint32_t shown = runToStimulus(e, 0);
    e.tick(shown + kTimeoutMs);
    EXPECT_EQ(e.responseMs(0), kTimeoutMs);
}

TEST(PvtSummary, MedianAndFastestOverAnsweredTrials)
{
    Engine e(kSession, kSeed);
    e.start(0);
    uint32_t t = 0;
    for (uint32_t ms : {300u, 200u, 400u}) {
        t = answerTrial(e, t, ms);
    }
    const Summary s = e.summary();
    EXPECT_EQ(s.trials, 3u);
    EXPECT_EQ(s.medianMs, 300u);
    EXPECT_EQ(s.fastestMs, 200u);
}

TEST(PvtSummary, EvenCountMedianIsTheMeanOfTheMiddlePair)
{
    Engine e(kSession, kSeed);
    e.start(0);
    uint32_t t = 0;
    for (uint32_t ms : {200u, 300u, 400u, 500u}) {
        t = answerTrial(e, t, ms);
    }
    EXPECT_EQ(e.summary().medianMs, 350u);
}

TEST(PvtSummary, FewerThanMinimumTrialsGivesNoVerdict)
{
    Engine e(kSession, kSeed);
    e.start(0);
    uint32_t t = 0;
    for (uint32_t i = 0; i + 1 < Engine::kMinTrials; ++i) {
        t = answerTrial(e, t, 250);
    }
    EXPECT_EQ(e.summary().verdict, Verdict::NOT_ENOUGH_DATA);
}

TEST(PvtSummary, NoLapsesIsReady)
{
    Engine e(kSession, kSeed);
    e.start(0);
    uint32_t t = 0;
    for (uint32_t i = 0; i < 20; ++i) {
        t = answerTrial(e, t, 250);
    }
    EXPECT_EQ(e.summary().verdict, Verdict::READY);
}

TEST(PvtSummary, LapseShareDecidesCautionThenFatigued)
{
    // 20 trials: 3 lapses = 15% -> CAUTION; 4 lapses = 20% -> FATIGUED.
    auto run = [](uint32_t lapses) {
        Engine e(kSession, kSeed);
        e.start(0);
        uint32_t t = 0;
        for (uint32_t i = 0; i < 20; ++i) {
            t = answerTrial(e, t, i < lapses ? 700u : 250u);
        }
        return e.summary().verdict;
    };
    EXPECT_EQ(run(1), Verdict::READY);             // 5%
    EXPECT_EQ(run(3), Verdict::CAUTION);           // 15%
    EXPECT_EQ(run(4), Verdict::FATIGUED);          // 20%
}

TEST(PvtSummary, FalseStartsDoNotCountAsTrialsOrLapses)
{
    Engine e(kSession, kSeed);
    e.start(0);
    e.press(100);
    e.press(200);
    const Summary s = e.summary();
    EXPECT_EQ(s.falseStarts, 2u);
    EXPECT_EQ(s.trials, 0u);
    EXPECT_EQ(s.lapses, 0u);
}

TEST(PvtSummary, CapacityIsBoundedAndNeverOverflows)
{
    Engine e(10 * 60 * 1000, kSeed);               // longer than the buffer allows for
    e.start(0);
    uint32_t t = 0;
    for (uint32_t i = 0; i < Engine::kMaxTrials + 20; ++i) {
        if (e.phase() == Phase::DONE) {
            break;
        }
        t = answerTrial(e, t, 250);
    }
    EXPECT_LE(e.summary().trials, Engine::kMaxTrials);
}
