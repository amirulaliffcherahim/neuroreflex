#include <gtest/gtest.h>

#include <cstring>

#include "Commands.hpp"
#include "Presenter.hpp"
#include "Readiness.hpp"

using namespace Rd;

namespace
{
History withMedians(std::initializer_list<uint16_t> ms)
{
    History h;
    for (uint16_t m : ms) {
        Entry e;
        e.medianMs = m;
        e.state    = static_cast<uint8_t>(State::NORMAL);
        h.push(e);
    }
    return h;
}
} // namespace

// ---- baseline ---------------------------------------------------------------

TEST(Readiness, NoBaselineUntilThreeTests)
{
    EXPECT_EQ(baselineMs(withMedians({})), 0u);
    EXPECT_EQ(baselineMs(withMedians({300, 310})), 0u);
    EXPECT_EQ(baselineMs(withMedians({300, 310, 320})), 310u);
}

TEST(Readiness, BaselineIsTheMedianOfTheLastSevenSoOneBadDayDoesNotMoveIt)
{
    // Seven normal days and one awful one: the normal barely moves.
    EXPECT_EQ(baselineMs(withMedians({300, 302, 298, 301, 299, 303, 300, 600})), 301u);
    // Old tests fall out of the window.
    EXPECT_EQ(baselineMs(withMedians({900, 900, 900, 300, 300, 300, 300, 300, 300, 300})), 300u);
}

TEST(Readiness, HistoryKeepsTheNewestFourteen)
{
    History h;
    for (uint16_t i = 0; i < 20; ++i) {
        Entry e;
        e.medianMs = static_cast<uint16_t>(200 + i);
        h.push(e);
    }
    EXPECT_EQ(h.count, kMaxHistory);
    EXPECT_EQ(h.at(0).medianMs, 206);                    // oldest kept
    EXPECT_EQ(h.at(kMaxHistory - 1).medianMs, 219);      // newest
}

// ---- the call ---------------------------------------------------------------

TEST(Readiness, TooFewTrialsIsNoData)
{
    EXPECT_EQ(assess(withMedians({}), 300, 0, 5, 8).state, State::NONE);
}

TEST(Readiness, FirstThreeTestsAreLearningAndCountUp)
{
    const Reading a = assess(withMedians({}), 300, 0, 30, 8);
    EXPECT_EQ(a.state, State::LEARNING);
    EXPECT_EQ(a.testNo, 1u);
    const Reading c = assess(withMedians({300, 300}), 300, 0, 30, 8);
    EXPECT_EQ(c.testNo, 3u);
    EXPECT_EQ(c.score, 100);
}

TEST(Readiness, LapseHeavyFirstTestIsStillDrained)
{
    // 7 lapses in 30 = 23 %: a bad sign whether or not we know your normal.
    EXPECT_EQ(assess(withMedians({}), 450, 7, 30, 8).state, State::DRAINED);
}

TEST(Readiness, AgainstYourNormal)
{
    const History h = withMedians({300, 300, 300, 300, 300});
    EXPECT_EQ(assess(h, 300, 0, 30, 8).state, State::NORMAL);
    EXPECT_EQ(assess(h, 285, 0, 30, 8).state, State::PRIMED);     // 5 % faster
    EXPECT_EQ(assess(h, 330, 0, 30, 8).state, State::DRAINED);    // 10 % slower
    EXPECT_EQ(assess(h, 320, 1, 30, 8).state, State::NORMAL);     // +7 %: a normal wobble
}

TEST(Readiness, AFastButSloppyTestIsNotPrimed)
{
    // Quick median but 3 lapses in 30 (10 %): not a "go hard" day.
    const History h = withMedians({300, 300, 300});
    EXPECT_EQ(assess(h, 280, 3, 30, 8).state, State::NORMAL);
}

TEST(Readiness, SameSpeedMeansDifferentThingsForDifferentPeople)
{
    // 320 ms is a great day for a 360 ms person and a bad one for a 280 ms person.
    EXPECT_EQ(assess(withMedians({360, 360, 360}), 320, 0, 30, 8).state, State::PRIMED);
    EXPECT_EQ(assess(withMedians({280, 280, 280}), 320, 0, 30, 8).state, State::DRAINED);
}

TEST(Readiness, DeltaRoundsAndScoreTracksIt)
{
    const History h = withMedians({300, 300, 300});
    const Reading slow = assess(h, 336, 0, 30, 8);
    EXPECT_EQ(slow.deltaPct, 12);
    EXPECT_EQ(slow.baselineMs, 300u);
    const Reading fast = assess(h, 279, 0, 30, 8);
    EXPECT_EQ(fast.deltaPct, -7);
    EXPECT_GT(fast.score, assess(h, 300, 0, 30, 8).score);
    EXPECT_LT(slow.score, assess(h, 300, 0, 30, 8).score);
    EXPECT_GE(assess(h, 1500, 30, 30, 8).score, 4);           // clamped, never wraps
}

TEST(Readiness, HistoryMessageFitsTheKernelPool)
{
    EXPECT_LE(sizeof(CustomMessage::History), 256u);
}

// ---- presenter: verdict, pages, save ---------------------------------------

namespace
{
using namespace Nr;
bool eq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

struct Session
{
    Presenter p;
    uint32_t  now = 0;
    void finish(uint32_t reactionMs)
    {
        p.press(Button::R1, now);
        for (uint32_t g = 0; g < 200000 && p.frame(now).layout != Layout::RESULT; ++g) {
            now += 10;
            p.tick(now);
            if (eq(p.frame(now).title, "GO")) { now += reactionMs; p.press(Button::R1, now); }
        }
    }
};
} // namespace

TEST(NrVerdict, FirstEverTestSaysLearningAndQueuesASave)
{
    Session r;
    r.finish(300);
    const Frame f = r.p.frame(r.now);
    ASSERT_EQ(f.layout, Layout::RESULT);
    EXPECT_TRUE(eq(f.title, "LEARNING"));
    EXPECT_TRUE(eq(f.big, "1/3"));
    Entry e;
    ASSERT_TRUE(r.p.takeResult(e));
    EXPECT_GE(e.medianMs, 300);
    EXPECT_LE(e.medianMs, 320);
    EXPECT_FALSE(r.p.takeResult(e));                         // once per test
}

TEST(NrVerdict, SlowDayAgainstYourNormalSaysDrainedWithAnAction)
{
    Session r;
    r.p.setHistory(withMedians({300, 300, 300, 300}));
    r.finish(360);                                           // ~+20 %
    const Frame f = r.p.frame(r.now);
    EXPECT_TRUE(eq(f.title, "DRAINED"));
    EXPECT_TRUE(eq(f.sub, "Easy day or rest"));
    EXPECT_EQ(f.big[0], '+');
    EXPECT_NE(std::strchr(f.big, '%'), nullptr);
    EXPECT_GE(f.score, 0);
    EXPECT_LT(f.score, 70);
}

TEST(NrVerdict, FastDaySaysPrimed)
{
    Session r;
    r.p.setHistory(withMedians({340, 340, 340}));
    r.finish(290);
    const Frame f = r.p.frame(r.now);
    EXPECT_TRUE(eq(f.title, "PRIMED"));
    EXPECT_TRUE(eq(f.sub, "Go hard today"));
    EXPECT_EQ(f.big[0], '-');
    EXPECT_GT(f.score, 70);
}

TEST(NrVerdict, L2PagesThroughTrendAndDetailsAndBack)
{
    Session r;
    r.p.setHistory(withMedians({300, 300, 300}));
    r.finish(300);
    EXPECT_EQ(r.p.frame(r.now).layout, Layout::RESULT);
    r.p.press(Button::L2, r.now);
    const Frame t = r.p.frame(r.now);
    EXPECT_EQ(t.layout, Layout::TREND);
    EXPECT_EQ(t.barCount, 4u);                               // 3 saved + this one
    EXPECT_EQ(t.guideH, kTrendNormalH);
    r.p.press(Button::L2, r.now);
    const Frame d = r.p.frame(r.now);
    EXPECT_EQ(d.layout, Layout::DETAILS);
    EXPECT_GT(d.barCount, 8u);
    EXPECT_NE(std::strstr(d.sub, "lapses"), nullptr);
    r.p.press(Button::L2, r.now);
    EXPECT_EQ(r.p.frame(r.now).layout, Layout::RESULT);
    r.p.press(Button::L1, r.now);
    EXPECT_EQ(r.p.frame(r.now).layout, Layout::DETAILS);     // L1 goes back
}

TEST(NrVerdict, TrendBarsSitOnTheGuideAtYourNormalAndRiseWhenSlower)
{
    Presenter p;
    History h = withMedians({300, 300, 300});
    Entry slow;
    slow.medianMs = 360;
    slow.state    = static_cast<uint8_t>(State::DRAINED);
    h.push(slow);
    p.setHistory(h);
    p.press(Button::L2, 0);                                  // intro -> trend
    const Frame f = p.frame(0);
    ASSERT_EQ(f.layout, Layout::TREND);
    EXPECT_EQ(f.barH[0], kTrendNormalH);
    EXPECT_GT(f.barH[3], kTrendNormalH);
    EXPECT_NE(f.barColor[3], f.barColor[0]);                 // drained day stands out
}

TEST(NrVerdict, IntroShowsLearningProgressThenYourNormal)
{
    Presenter a;
    EXPECT_TRUE(a.frame(0).reticle);                         // first run: explain, no number
    EXPECT_TRUE(eq(a.frame(0).sub, "3-min focus check"));
    EXPECT_EQ(a.frame(0).l2, Hint::NONE);                    // no trend to show yet
    Presenter b;
    b.setHistory(withMedians({310, 300, 320}));
    EXPECT_TRUE(eq(b.frame(0).big, "310"));
    EXPECT_TRUE(eq(b.frame(0).sub, "ms  your normal"));
    EXPECT_EQ(b.frame(0).l2, Hint::WHITE);
}
