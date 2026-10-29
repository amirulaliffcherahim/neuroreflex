/**
 ******************************************************************************
 * @file    Presenter.hpp
 * @brief   NeuroReflex screen logic: buttons in, a drawable Frame and haptic
 *          cues out. No LVGL, no kernel: time is passed in, so the whole
 *          session can be driven and checked on the host.
 ******************************************************************************
 */

#ifndef NR_PRESENTER_HPP
#define NR_PRESENTER_HPP

#include <cstdint>

#include "PvtEngine.hpp"
#include "Readiness.hpp"

namespace Nr
{

constexpr uint32_t kSessionMs = 3 * 60 * 1000;

enum class Button : uint8_t { L1, L2, R1, R2 };

/// Same values as SDK::LVGL::Buttons::Color so the GUI can cast.
enum class Hint : uint8_t { NONE = 0, WHITE, AMBER, RED, GREEN };

/// Same values as CustomMessage::HapticKind (+1 for NONE).
enum class Cue : uint8_t { NONE = 0, STIMULUS, LAPSE, FALSE_START, DONE };

/// Which composition the GUI should draw. Named, so the GUI never infers it from text.
/// RESULT is the verdict page; TREND and DETAILS are the pages behind it.
enum class Layout : uint8_t { INTRO, WAIT, GO, FEEDBACK, RESULT, TREND, DETAILS };

/// Trend strip: bar height for a test that matched your normal exactly, and the cap.
constexpr uint8_t kTrendNormalH = 36;
constexpr uint8_t kTrendMaxH    = 66;

/// Result strip: one bar per trial, newest on the right.
constexpr uint32_t kBars        = 32;
constexpr uint8_t  kBarMinH     = 4;
constexpr uint8_t  kBarMaxH     = 40;
constexpr uint32_t kBarScaleMs  = 1000;   ///< a response this slow draws the tallest bar

/// Bar height in pixels for a response time; shared by the strip and the gauge.
constexpr uint8_t barHeight(uint32_t ms)
{
    const uint32_t c = ms > kBarScaleMs ? kBarScaleMs : ms;
    return static_cast<uint8_t>(kBarMinH + (c * (kBarMaxH - kBarMinH)) / kBarScaleMs);
}

/// Where the 500 ms lapse threshold falls on that same scale.
constexpr uint8_t kLapseLineH = barHeight(Pvt::kLapseMs);

struct Frame
{
    Layout   layout = Layout::INTRO;

    // Palette for this frame: chrome accent, state colours, neutrals.
    uint32_t bg         = 0;
    uint32_t accent     = 0;   ///< chrome: titles, call to action, intro reticle
    uint32_t okColor    = 0;   ///< good response
    uint32_t lapseColor = 0;   ///< lapse
    uint32_t trackColor = 0;   ///< empty part of a gauge or progress bar
    uint32_t guideColor = 0;   ///< the 500 ms lapse marker

    uint32_t titleColor = 0;
    uint32_t bigColor   = 0;
    uint32_t subColor   = 0;
    uint32_t sub3Color  = 0;
    char     label[24]  = {};   ///< small-caps heading at the top
    uint32_t labelColor = 0;
    int8_t   page       = -1;   ///< result page 0..2 for the page dots, -1 = none
    char     title[20]  = {};
    char     big[12]    = {};
    char     sub[28]    = {};
    char     sub2[28]   = {};
    char     sub3[28]   = {};

    bool     reticle      = false;
    uint32_t reticleColor = 0;

    int32_t  progress = -1;     ///< session percent 0..100, -1 = not shown

    uint32_t gaugeMs    = 0;    ///< feedback gauge fill, 0 = none
    uint32_t gaugeColor = 0;

    uint32_t barCount    = 0;
    uint8_t  barH[kBars] = {};
    uint32_t barLapseMask = 0;  ///< bit i set = bar i is a lapse
    uint32_t barColor[kBars] = {};
    uint8_t  guideH = 0;        ///< guide line height above the strip base, 0 = none

    int32_t  score      = -1;   ///< readiness gauge 0..100, -1 = not shown
    uint32_t scoreColor = 0;

    Hint     l1 = Hint::NONE, l2 = Hint::NONE, r1 = Hint::NONE, r2 = Hint::NONE;

    /// Equal when they would draw the same picture (unused bar slots ignored).
    bool operator==(const Frame& o) const;
    bool operator!=(const Frame& o) const { return !(*this == o); }
};

struct Reply
{
    bool exit = false;
    Cue  cue  = Cue::NONE;
};

class Presenter
{
public:
    Presenter();

    Reply press(Button b, uint32_t nowMs);
    Cue   tick(uint32_t nowMs);
    Frame frame(uint32_t nowMs) const;

    /// The saved history, from the service (at start and after each save).
    void setHistory(const Rd::History& h) { mHistory = h; }
    const Rd::History& history() const { return mHistory; }

    /// A finished test waiting to be saved; true once per test.
    bool takeResult(Rd::Entry& out);

    /// The call made on the last finished test.
    const Rd::Reading& reading() const { return mReading; }

private:
    enum class Mode : uint8_t { INTRO, RUN, RESULT, HISTORY };
    enum class Page : uint8_t { VERDICT, TREND, DETAILS };

    void begin(uint32_t nowMs);
    Cue  syncPhase(bool pressed, uint32_t nowMs);

    Mode        mMode = Mode::INTRO;
    Pvt::Engine mEngine;
    Pvt::Phase  mPrev = Pvt::Phase::IDLE;
    uint32_t    mRuns = 0;
    uint32_t    mEarlyUntilMs = 0;   ///< show TOO SOON until this time
    Page        mPage = Page::VERDICT;
    Rd::History mHistory;
    Rd::Reading mReading;
    Rd::Entry   mPending;
    bool        mPendingSave = false;

    void verdictFrame(Frame& f) const;
    void trendFrame(Frame& f) const;
    void detailsFrame(Frame& f) const;
};

} // namespace Nr

#endif

