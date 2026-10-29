#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>

#include "Presenter.hpp"

using namespace Nr;

namespace
{
/// Advance the clock in 10 ms steps (the GUI's real tick), collecting cues.
struct Rig
{
    Presenter p;
    uint32_t  now = 0;
    uint32_t  stimulusCues = 0, lapseCues = 0, falseCues = 0, doneCues = 0;

    void count(Cue c)
    {
        stimulusCues += c == Cue::STIMULUS;
        lapseCues    += c == Cue::LAPSE;
        falseCues    += c == Cue::FALSE_START;
        doneCues     += c == Cue::DONE;
    }
    void step(uint32_t ms = 10) { now += ms; count(p.tick(now)); }
    void hit(Button b) { count(p.press(b, now).cue); }

    /// Step until the frame shows the stimulus (big counter on the GO screen).
    bool runToGo(uint32_t capMs = 12000)
    {
        for (uint32_t i = 0; i < capMs / 10; ++i) {
            step();
            if (std::strcmp(p.frame(now).title, "GO") == 0) {
                return true;
            }
        }
        return false;
    }
    void start() { hit(Button::R1); }
};

bool eq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }
} // namespace

TEST(NrIntro, ShowsNameAndOffersStartAndBack)
{
    Rig r;
    const Frame f = r.p.frame(0);
    EXPECT_TRUE(eq(f.label, "NEUROREFLEX"));
    EXPECT_EQ(f.r1, Hint::AMBER);
    EXPECT_EQ(f.r2, Hint::WHITE);
    EXPECT_EQ(f.l1, Hint::NONE);                   // hints only for buttons that act
    EXPECT_EQ(f.l2, Hint::NONE);
}

TEST(NrIntro, BackExitsTheApp)
{
    Rig r;
    EXPECT_TRUE(r.p.press(Button::R2, 0).exit);
}

TEST(NrIntro, ResponseButtonsDoNothingBeforeStart)
{
    Rig r;
    const Reply a = r.p.press(Button::L1, 0);
    EXPECT_FALSE(a.exit);
    EXPECT_EQ(a.cue, Cue::NONE);
    EXPECT_EQ(r.p.frame(0).layout, Layout::INTRO);
}

TEST(NrRun, StartMovesToWaitScreenWithoutAStimulus)
{
    Rig r;
    r.start();
    r.step();
    EXPECT_TRUE(eq(r.p.frame(r.now).title, "WAIT"));
    EXPECT_EQ(r.stimulusCues, 0u);
}

TEST(NrRun, StimulusShowsOnceWithOneCueAndALiveCounter)
{
    Rig r;
    r.start();
    ASSERT_TRUE(r.runToGo());
    EXPECT_EQ(r.stimulusCues, 1u);
    r.step(120);
    EXPECT_EQ(r.stimulusCues, 1u);                 // not re-fired while it shows
    const Frame f = r.p.frame(r.now);
    EXPECT_TRUE(eq(f.title, "GO"));
    EXPECT_GE(std::atoi(f.big), 120);
    EXPECT_LE(std::atoi(f.big), 130);
}

TEST(NrRun, AnyOfThreeButtonsAnswersAndShowsMilliseconds)
{
    for (Button b : {Button::L1, Button::L2, Button::R1}) {
        Rig r;
        r.start();
        ASSERT_TRUE(r.runToGo());
        r.step(250);
        r.hit(b);
        const Frame f = r.p.frame(r.now);
        EXPECT_TRUE(eq(f.title, "NICE")) << f.title;
        EXPECT_GE(std::atoi(f.big), 250);
        EXPECT_LE(std::atoi(f.big), 262);
        EXPECT_EQ(f.bigColor, 0x00C000u);          // green
        EXPECT_EQ(r.lapseCues, 0u);
    }
}

TEST(NrRun, SlowAnswerIsALapseWithItsOwnCueAndWarningColour)
{
    Rig r;
    r.start();
    ASSERT_TRUE(r.runToGo());
    r.step(700);
    r.hit(Button::L1);
    const Frame f = r.p.frame(r.now);
    EXPECT_TRUE(eq(f.title, "SLOW"));
    EXPECT_EQ(r.lapseCues, 1u);
    EXPECT_EQ(f.bigColor, 0xC08080u);              // rose warning, not the green of a good answer
}

TEST(NrRun, UnansweredStimulusTimesOutWithALapseCue)
{
    Rig r;
    r.start();
    ASSERT_TRUE(r.runToGo());
    r.step(2100);
    EXPECT_EQ(r.lapseCues, 1u);
    EXPECT_TRUE(eq(r.p.frame(r.now).title, "SLOW"));
}

TEST(NrRun, PressingTooEarlySaysSoAndKeepsWaiting)
{
    Rig r;
    r.start();
    r.step(500);
    r.hit(Button::L2);
    EXPECT_EQ(r.falseCues, 1u);
    EXPECT_TRUE(eq(r.p.frame(r.now).title, "TOO SOON"));
    r.step(1500);                                  // message lingers briefly, then waiting
    EXPECT_EQ(r.stimulusCues, 0u);
}

TEST(NrRun, CoverageOfNumbersNeverOverflowsTheBuffer)
{
    Rig r;
    r.start();
    ASSERT_TRUE(r.runToGo());
    r.step(1990);
    EXPECT_LE(std::strlen(r.p.frame(r.now).big), 4u);
}

TEST(NrRun, BackDuringRunCancelsToIntroInsteadOfExiting)
{
    Rig r;
    r.start();
    r.step(500);
    const Reply rep = r.p.press(Button::R2, r.now);
    EXPECT_FALSE(rep.exit);
    EXPECT_EQ(r.p.frame(r.now).layout, Layout::INTRO);
}

TEST(NrRun, ShowsRunningHintsOnlyOnResponseButtons)
{
    Rig r;
    r.start();
    r.step();
    const Frame f = r.p.frame(r.now);
    EXPECT_EQ(f.r2, Hint::RED);                    // cancel
    EXPECT_EQ(f.r1, Hint::NONE);                   // no hint while waiting
}

TEST(NrSession, FullSessionWithGoodReactionsEndsReady)
{
    Rig r;
    r.start();
    for (uint32_t guard = 0; guard < 200000 && r.p.frame(r.now).layout != Layout::RESULT; ++guard) {
        r.step();
        if (eq(r.p.frame(r.now).title, "GO")) {
            r.step(240);
            r.hit(Button::R1);
        }
    }
    const Frame f = r.p.frame(r.now);
    ASSERT_EQ(f.layout, Layout::RESULT);
    EXPECT_TRUE(eq(f.title, "LEARNING")) << f.title;   // first test ever: building your normal
    EXPECT_EQ(r.doneCues, 1u);
    EXPECT_GE(r.now, kSessionMs);
}

TEST(NrSession, SlowReactionsEndFatigued)
{
    Rig r;
    r.start();
    for (uint32_t guard = 0; guard < 200000 && r.p.frame(r.now).layout != Layout::RESULT; ++guard) {
        r.step();
        if (eq(r.p.frame(r.now).title, "GO")) {
            r.step(650);
            r.hit(Button::R1);
        }
    }
    EXPECT_TRUE(eq(r.p.frame(r.now).title, "DRAINED"));   // lapse-heavy even with no normal yet
    EXPECT_GE(r.lapseCues, 8u);
}

TEST(NrSession, ResultOffersAgainAndBackAndRestartsFresh)
{
    Rig r;
    r.start();
    for (uint32_t guard = 0; guard < 200000 && r.p.frame(r.now).layout != Layout::RESULT; ++guard) {
        r.step();
        if (eq(r.p.frame(r.now).title, "GO")) { r.step(240); r.hit(Button::R1); }
    }
    const Frame f = r.p.frame(r.now);
    EXPECT_EQ(f.r1, Hint::AMBER);
    EXPECT_EQ(f.r2, Hint::WHITE);
    r.hit(Button::R1);
    r.step();
    EXPECT_TRUE(eq(r.p.frame(r.now).title, "WAIT"));
    EXPECT_TRUE(r.p.press(Button::R2, r.now).exit == false);   // mid-run back cancels
}

TEST(NrSession, ResultShowsMedianLapsesAndEarlyPresses)
{
    Rig r;
    r.start();
    r.step(300);
    r.hit(Button::L1);                             // one early press
    for (uint32_t g = 0; g < 200000 && r.p.frame(r.now).layout != Layout::RESULT; ++g) {
        r.step();
        if (eq(r.p.frame(r.now).title, "GO")) { r.step(300); r.hit(Button::R1); }
    }
    ASSERT_EQ(r.p.frame(r.now).layout, Layout::RESULT);
    r.hit(Button::L2);                             // trend
    r.hit(Button::L2);                             // details of this test
    const Frame f = r.p.frame(r.now);
    ASSERT_EQ(f.layout, Layout::DETAILS);
    EXPECT_GE(std::atoi(f.big), 300);              // median
    EXPECT_LE(std::atoi(f.big), 320);
    EXPECT_NE(std::strstr(f.sub, "lapses 0 of"), nullptr) << f.sub;
    EXPECT_TRUE(eq(f.sub2, "too early 1")) << f.sub2;
}

TEST(NrSession, ButtonsAreIgnoredWhileFeedbackShows)
{
    Rig r;
    r.start();
    ASSERT_TRUE(r.runToGo());
    r.step(250);
    r.hit(Button::L1);
    const uint32_t cuesBefore = r.falseCues + r.lapseCues;
    r.hit(Button::L1);                             // double tap during feedback
    EXPECT_EQ(r.falseCues + r.lapseCues, cuesBefore);
    EXPECT_TRUE(eq(r.p.frame(r.now).title, "NICE"));
}


