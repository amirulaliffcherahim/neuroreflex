#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

#include "Presenter.hpp"

using namespace Nr;

namespace
{
bool eq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

struct Rig
{
    Presenter p;
    uint32_t  now = 0;
    void step(uint32_t ms = 10) { now += ms; p.tick(now); }
    bool toGo()
    {
        for (int i = 0; i < 1300 && !eq(p.frame(now).title, "GO"); ++i) step();
        return eq(p.frame(now).title, "GO");
    }
    void answer(uint32_t reactionMs)
    {
        ASSERT_TRUE(toGo());
        step(reactionMs);
        p.press(Button::R1, now);
    }
    void finishSession(uint32_t reactionMs)
    {
        p.press(Button::R1, now);
        for (uint32_t g = 0; g < 200000 && p.frame(now).layout != Layout::RESULT; ++g) {
            step();
            if (eq(p.frame(now).title, "GO")) { step(reactionMs); p.press(Button::R1, now); }
        }
    }
};

double lum(uint32_t c)
{
    auto ch = [](uint32_t v) {
        const double s = v / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * ch((c >> 16) & 0xFF) + 0.7152 * ch((c >> 8) & 0xFF) + 0.0722 * ch(c & 0xFF);
}
double contrast(uint32_t a, uint32_t b)
{
    const double la = lum(a), lb = lum(b);
    return ((la > lb ? la : lb) + 0.05) / ((la > lb ? lb : la) + 0.05);
}
} // namespace

// ---- layout is named, so the GUI never infers a screen from its text -------

TEST(NrDesign, EachScreenReportsItsLayout)
{
    Rig r;
    EXPECT_EQ(r.p.frame(0).layout, Layout::INTRO);
    r.p.press(Button::R1, 0);
    r.step();
    EXPECT_EQ(r.p.frame(r.now).layout, Layout::WAIT);
    ASSERT_TRUE(r.toGo());
    EXPECT_EQ(r.p.frame(r.now).layout, Layout::GO);
    r.step(250);
    r.p.press(Button::L1, r.now);
    EXPECT_EQ(r.p.frame(r.now).layout, Layout::FEEDBACK);
}

// ---- bar height scale -------------------------------------------------------

TEST(NrDesign, BarHeightRisesWithTimeAndStaysInsideItsBand)
{
    uint8_t prev = 0;
    for (uint32_t ms = 0; ms <= 3000; ms += 25) {
        const uint8_t h = barHeight(ms);
        EXPECT_GE(h, kBarMinH);
        EXPECT_LE(h, kBarMaxH);
        EXPECT_GE(h, prev);                        // never shrinks as reactions slow
        prev = h;
    }
    EXPECT_EQ(barHeight(0), kBarMinH);             // clamped, not wrapped
    EXPECT_EQ(barHeight(60000), kBarMaxH);
}

TEST(NrDesign, LapseLineSitsExactlyOnTheFiveHundredMillisecondBar)
{
    EXPECT_EQ(kLapseLineH, barHeight(Pvt::kLapseMs));
    EXPECT_LT(barHeight(Pvt::kLapseMs - 50), kLapseLineH);
    EXPECT_GT(barHeight(Pvt::kLapseMs + 50), kLapseLineH);
}

// ---- intro -------------------------------------------------------------------

TEST(NrDesign, IntroShowsTheReticleInTheAccentColourAndAPrimaryPrompt)
{
    Presenter p;
    const Frame f = p.frame(0);
    EXPECT_TRUE(f.reticle);
    EXPECT_EQ(f.reticleColor, f.accent);
    EXPECT_EQ(f.progress, -1);                     // nothing is running yet
    EXPECT_EQ(f.gaugeMs, 0u);
    EXPECT_EQ(f.barCount, 0u);
    EXPECT_EQ(f.sub3Color, f.accent);              // "R1 start" is the one call to action
}

// ---- waiting: progress + a dim reticle -------------------------------------

TEST(NrDesign, SessionProgressGrowsFromZeroTowardsAHundred)
{
    Rig r;
    r.p.press(Button::R1, 0);
    r.step();
    const int first = r.p.frame(r.now).progress;
    EXPECT_GE(first, 0);
    EXPECT_LE(first, 1);

    while (r.now < 90000) r.answer(250);           // about half way, ending in feedback
    const int mid = r.p.frame(r.now).progress;
    EXPECT_GE(mid, 49);
    EXPECT_LE(mid, 65);

    while (r.now < 160000) r.answer(250);
    const int late = r.p.frame(r.now).progress;
    EXPECT_GT(late, mid);
    EXPECT_LE(late, 100);
}

TEST(NrDesign, WaitingShowsAReticleButTheGoScreenHidesProgressAndReticle)
{
    Rig r;
    r.p.press(Button::R1, 0);
    r.step();
    EXPECT_TRUE(r.p.frame(r.now).reticle);
    ASSERT_TRUE(r.toGo());
    const Frame go = r.p.frame(r.now);
    EXPECT_FALSE(go.reticle);
    EXPECT_EQ(go.progress, -1);                    // a bar on solid green would vanish
}

TEST(NrDesign, TooSoonTurnsTheReticleYellowAsWellAsTheWords)
{
    Rig r;
    r.p.press(Button::R1, 0);
    r.step(300);
    const uint32_t calm = r.p.frame(r.now).reticleColor;
    r.p.press(Button::L1, r.now);
    const Frame f = r.p.frame(r.now);
    ASSERT_TRUE(eq(f.title, "TOO SOON"));
    EXPECT_NE(f.reticleColor, calm);
    EXPECT_TRUE(eq(f.sub, "wait for GO"));
}

// ---- feedback gauge ---------------------------------------------------------

TEST(NrDesign, FeedbackGaugeCarriesTheReactionTimeAndTheVerdictColour)
{
    Rig r;
    r.p.press(Button::R1, 0);
    ASSERT_TRUE(r.toGo());
    r.step(300);
    r.p.press(Button::L1, r.now);
    const Frame ok = r.p.frame(r.now);
    EXPECT_GE(ok.gaugeMs, 300u);
    EXPECT_LE(ok.gaugeMs, 312u);
    EXPECT_EQ(ok.gaugeColor, ok.bigColor);
    EXPECT_NE(ok.progress, -1);                    // progress stays visible between trials

    Rig s;
    s.p.press(Button::R1, 0);
    ASSERT_TRUE(s.toGo());
    s.step(650);
    s.p.press(Button::L1, s.now);
    const Frame slow = s.p.frame(s.now);
    EXPECT_GE(slow.gaugeMs, 650u);
    EXPECT_NE(slow.gaugeColor, ok.gaugeColor);     // fast and slow never look the same
}

// ---- result strip -----------------------------------------------------------

TEST(NrDesign, ResultStripHasOneBarPerTrialCappedAtTheDisplayWidth)
{
    Rig r;
    r.finishSession(260);
    r.p.press(Button::L1, r.now);                  // verdict -> details (L1 wraps back)
    const Frame f = r.p.frame(r.now);
    ASSERT_EQ(f.layout, Layout::DETAILS);
    EXPECT_GE(f.barCount, 8u);
    EXPECT_LE(f.barCount, kBars);
    for (uint32_t i = 0; i < f.barCount; ++i) {
        EXPECT_GE(f.barH[i], kBarMinH);
        EXPECT_LE(f.barH[i], kBarMaxH);
    }
}

TEST(NrDesign, SlowTrialsAreFlaggedAsLapsesAndFastOnesAreNot)
{
    auto bits = [](uint32_t n) { return n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u); };

    Rig fast;
    fast.finishSession(250);
    fast.p.press(Button::L1, fast.now);
    const Frame ff = fast.p.frame(fast.now);
    ASSERT_GT(ff.barCount, 0u);
    EXPECT_EQ(ff.barLapseMask & bits(ff.barCount), 0u);

    Rig slow;
    slow.finishSession(650);
    slow.p.press(Button::L1, slow.now);
    const Frame sf = slow.p.frame(slow.now);
    ASSERT_GT(sf.barCount, 0u);
    const uint32_t all = bits(sf.barCount);
    EXPECT_EQ(sf.barLapseMask & all, all);         // every bar is a lapse
    for (uint32_t i = 0; i < sf.barCount; ++i) {
        EXPECT_GT(sf.barH[i], kLapseLineH);        // and sits above the lapse line
    }
}

TEST(NrDesign, BarsAreInTheOrderTheTrialsHappenedOldestFirst)
{
    Rig r;
    r.p.press(Button::R1, 0);
    for (uint32_t ms : {200u, 300u, 400u, 550u, 250u, 250u, 250u, 250u}) r.answer(ms);
    // finish the session quickly with fast answers
    for (uint32_t g = 0; g < 200000 && r.p.frame(r.now).layout != Layout::RESULT; ++g) {
        r.step();
        if (eq(r.p.frame(r.now).title, "GO")) { r.step(250); r.p.press(Button::R1, r.now); }
    }
    r.p.press(Button::L1, r.now);
    const Frame f = r.p.frame(r.now);
    ASSERT_EQ(f.layout, Layout::DETAILS);
    ASSERT_GE(f.barCount, 4u);
    const uint32_t skip = f.barCount < 30 ? 0 : 0;   // whole session fits unless capped
    (void)skip;
    if (f.barCount < kBars) {
        EXPECT_LT(f.barH[0], f.barH[1]);           // 200 < 300
        EXPECT_LT(f.barH[1], f.barH[2]);           // 300 < 400
        EXPECT_LT(f.barH[2], f.barH[3]);           // 400 < 550
        EXPECT_TRUE(f.barLapseMask & (1u << 3));   // 550 is a lapse
        EXPECT_FALSE(f.barLapseMask & (1u << 2));
    }
}

TEST(NrDesign, StripNeverExceedsItsWidthEvenForVeryFastSessions)
{
    Presenter p;
    uint32_t now = 0;
    p.press(Button::R1, now);
    for (uint32_t g = 0; g < 400000 && p.frame(now).layout != Layout::RESULT; ++g) {
        now += 10;
        p.tick(now);
        if (eq(p.frame(now).title, "GO")) { now += 10; p.press(Button::R1, now); }   // very fast
    }
    p.press(Button::L1, now);
    const Frame f = p.frame(now);
    ASSERT_EQ(f.layout, Layout::DETAILS);
    EXPECT_LE(f.barCount, kBars);
}

TEST(NrDesign, VerdictWordGaugeAndNumberShareTheStateColourNotTheChromeAccent)
{
    Rig r;
    r.finishSession(250);
    const Frame f = r.p.frame(r.now);
    EXPECT_EQ(f.scoreColor, f.titleColor);         // word and gauge read as one
    EXPECT_NE(f.titleColor, f.accent);             // the verdict must not be mistaken for chrome
}

// ---- readability of every new colour ---------------------------------------

TEST(NrDesign, AccentAndBarColoursAreVisibleOnTheirGround)
{
    Presenter p;
    const Frame f = p.frame(0);
    EXPECT_GE(contrast(f.accent, f.bg), 4.5);
    EXPECT_GE(contrast(f.okColor, f.bg), 3.0);     // non-text: WCAG 1.4.11
    EXPECT_GE(contrast(f.lapseColor, f.bg), 3.0);
    EXPECT_GE(contrast(f.trackColor, f.bg), 1.5);  // a track only has to be seen, not read
    EXPECT_NE(f.okColor, f.lapseColor);
}

// ---- equality drives redraw -------------------------------------------------

TEST(NrDesign, IdenticalFramesCompareEqualAndAnyDifferenceDoesNot)
{
    Presenter p;
    const Frame a = p.frame(0);
    const Frame b = p.frame(0);
    EXPECT_TRUE(a == b);
    Frame c = a;
    c.progress = 42;
    EXPECT_FALSE(a == c);
    Frame d = a;
    d.barCount = 3;
    EXPECT_FALSE(a == d);
    Frame e = a;
    e.barH[5] = 9;
    e.barCount = 6;
    Frame g = e;
    g.barH[5] = 10;
    EXPECT_FALSE(e == g);
    Frame h = a;
    std::snprintf(h.sub, sizeof h.sub, "different");
    EXPECT_FALSE(a == h);
}

TEST(NrDesign, UnusedBarSlotsDoNotAffectEquality)
{
    Frame a, b;
    a.barCount = b.barCount = 2;
    a.barH[0] = b.barH[0] = 10;
    a.barH[1] = b.barH[1] = 20;
    a.barH[7] = 99;                                // beyond barCount: ignored
    EXPECT_TRUE(a == b);
}
