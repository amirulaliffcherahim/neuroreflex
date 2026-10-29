#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

#include "Presenter.hpp"

using namespace Nr;

namespace
{
// WCAG relative luminance on the display's real 4-level channels.
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
    const double hi = la > lb ? la : lb, lo = la > lb ? lb : la;
    return (hi + 0.05) / (lo + 0.05);
}

void expectReadable(const Frame& f, const char* what)
{
    if (f.title[0])  EXPECT_GE(contrast(f.titleColor, f.bg), 4.5) << what << " title";
    if (f.big[0])    EXPECT_GE(contrast(f.bigColor,   f.bg), 4.5) << what << " big";
    if (f.sub[0])    EXPECT_GE(contrast(f.subColor,   f.bg), 4.5) << what << " sub";
}

bool eq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }
} // namespace

TEST(NrContrast, EveryScreenMeetsFourPointFiveToOne)
{
    Presenter p;
    uint32_t now = 0;
    expectReadable(p.frame(now), "intro");

    p.press(Button::R1, now);
    for (int i = 0; i < 2000 && !eq(p.frame(now).title, "GO"); ++i) {
        now += 10;
        p.tick(now);
        if (i == 5) {
            p.press(Button::L1, now);              // early press -> TOO SOON
            expectReadable(p.frame(now), "too soon");
        }
        if (i == 1) {
            expectReadable(p.frame(now), "wait");
        }
    }
    ASSERT_TRUE(eq(p.frame(now).title, "GO"));
    expectReadable(p.frame(now), "go");

    now += 700;
    p.press(Button::L1, now);
    expectReadable(p.frame(now), "slow");
}

TEST(NrContrast, ResultVerdictsAreReadableAndNeverColourOnly)
{
    for (uint32_t reaction : {240u, 650u}) {
        Presenter p;
        uint32_t now = 0;
        p.press(Button::R1, now);
        for (uint32_t g = 0; g < 200000 && p.frame(now).layout != Layout::RESULT; ++g) {
            now += 10;
            p.tick(now);
            if (eq(p.frame(now).title, "GO")) {
                now += reaction;
                p.press(Button::R1, now);
            }
        }
        const Frame f = p.frame(now);
        ASSERT_EQ(f.layout, Layout::RESULT);
        expectReadable(f, "result");
        EXPECT_GT(std::strlen(f.title), 5u);       // the word carries the meaning
        EXPECT_GT(std::strlen(f.sub), 8u);         // and so does the action line
    }
}

TEST(NrContrast, TextFitsTheLabelsAndBezelSafeWidth)
{
    Presenter p;
    const Frame f = p.frame(0);
    EXPECT_LT(std::strlen(f.sub),  sizeof f.sub);
    // Lines 2 and 3 sit beside the R2 bezel arc, so they must stay narrow
    // (measured: 184 px collided with it, 19 chars at 172 px did not).
    EXPECT_LE(std::strlen(f.sub),  19u);
    EXPECT_LE(std::strlen(f.sub2), 12u);
    EXPECT_LE(std::strlen(f.sub3), 12u);
}

TEST(NrContrast, NoBezelHintIsDrawnInTheSameColourAsTheBackground)
{
    Presenter p;
    uint32_t now = 0;
    p.press(Button::R1, now);
    for (int i = 0; i < 2000 && !eq(p.frame(now).title, "GO"); ++i) {
        now += 10;
        p.tick(now);
    }
    const Frame f = p.frame(now);
    ASSERT_TRUE(eq(f.title, "GO"));
    // The GO screen is green; a GREEN hint on it is invisible.
    EXPECT_NE(f.l1, Hint::GREEN);
    EXPECT_NE(f.l2, Hint::GREEN);
    EXPECT_NE(f.r1, Hint::GREEN);
}
