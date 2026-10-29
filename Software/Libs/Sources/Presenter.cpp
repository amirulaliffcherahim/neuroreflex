#include "Presenter.hpp"

#include <cstdio>
#include <cstring>

#include "SDK/GUI/Color.hpp"

namespace Nr
{

namespace C = SDK::GUI::Color;

namespace
{
constexpr uint32_t kEarlyNoticeMs = 1000;

// One accent for chrome, two for outcome, neutrals for everything else. Every
// text colour clears 4.5:1 on black; bar and gauge colours clear 3:1.
constexpr uint32_t kAccent = C::CYAN_PURE;    // 9.3:1  titles, start prompt, reticle
constexpr uint32_t kOk     = C::GREEN;        // 8.5:1  a good answer
constexpr uint32_t kLapse  = C::ROSE;         // 6.6:1  a lapse
constexpr uint32_t kDrained= C::YELLOW_DARK;  // 6.2:1  DRAINED: the UNA orange, a caution not an alarm
constexpr uint32_t kCaution= C::YELLOW;       // 10.8:1 early press, CAUTION
constexpr uint32_t kTrack  = C::GRAY_DARK;    // 2.0:1  empty gauge, only needs to be seen
constexpr uint32_t kMuted  = C::GRAY;         // 5.3:1  secondary text
constexpr uint32_t kPrimed = C::CYAN;         // 9.9:1  PRIMED: sharper than normal
constexpr uint32_t kLearn  = C::PERIWINKLE;   // 6.2:1  LEARNING: building your normal

uint32_t stateColor(Rd::State s)
{
    switch (s) {
        case Rd::State::PRIMED:   return kPrimed;
        case Rd::State::NORMAL:   return kOk;
        case Rd::State::DRAINED:  return kDrained;
        case Rd::State::LEARNING: return kLearn;
        default:                  return kMuted;
    }
}

void put(char* dst, size_t n, const char* s) { std::snprintf(dst, n, "%s", s); }

template <typename T>
bool sameBytes(const T& a, const T& b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }
} // namespace

bool Frame::operator==(const Frame& o) const
{
    // Bars past barCount are scratch; compare only what would be drawn.
    if (layout != o.layout || bg != o.bg || accent != o.accent || okColor != o.okColor ||
        lapseColor != o.lapseColor || trackColor != o.trackColor || guideColor != o.guideColor ||
        titleColor != o.titleColor || bigColor != o.bigColor || subColor != o.subColor ||
        sub3Color != o.sub3Color || reticle != o.reticle || reticleColor != o.reticleColor ||
        progress != o.progress || gaugeMs != o.gaugeMs || gaugeColor != o.gaugeColor ||
        barCount != o.barCount || barLapseMask != o.barLapseMask || guideH != o.guideH ||
        score != o.score || scoreColor != o.scoreColor || labelColor != o.labelColor || page != o.page ||
        l1 != o.l1 || l2 != o.l2 || r1 != o.r1 || r2 != o.r2) {
        return false;
    }
    if (std::strcmp(label, o.label) || std::strcmp(title, o.title) || std::strcmp(big, o.big) || std::strcmp(sub, o.sub) ||
        std::strcmp(sub2, o.sub2) || std::strcmp(sub3, o.sub3)) {
        return false;
    }
    return std::memcmp(barH, o.barH, barCount) == 0 &&
           std::memcmp(barColor, o.barColor, barCount * sizeof barColor[0]) == 0;
}

Presenter::Presenter() : mEngine(kSessionMs, 1) {}

void Presenter::begin(uint32_t nowMs)
{
    ++mRuns;
    mEngine = Pvt::Engine(kSessionMs, nowMs + mRuns * 7919u + 1u);
    mEngine.start(nowMs);
    mPrev         = Pvt::Phase::WAITING;
    mEarlyUntilMs = 0;
    mMode         = Mode::RUN;
}

/// Compare the engine's phase with the last one seen and name the cue for
/// the change. @p pressed says the change came from a button.
Cue Presenter::syncPhase(bool pressed, uint32_t nowMs)
{
    using Pvt::Phase;
    const Phase now = mEngine.phase();
    Cue cue = Cue::NONE;

    if (now != mPrev) {
        if (now == Phase::STIMULUS) {
            cue = Cue::STIMULUS;
        } else if (now == Phase::FEEDBACK && mEngine.outcome() == Pvt::Outcome::LAPSE) {
            cue = Cue::LAPSE;
        } else if (now == Phase::DONE) {
            cue = Cue::DONE;
            mMode = Mode::RESULT;
            mPage = Page::VERDICT;
            const Pvt::Summary s = mEngine.summary();
            mReading = Rd::assess(mHistory, s.medianMs, s.lapses, s.trials, Pvt::Engine::kMinTrials);
            if (mReading.state != Rd::State::NONE) {
                mPending.medianMs = static_cast<uint16_t>(s.medianMs > 0xFFFF ? 0xFFFF : s.medianMs);
                mPending.lapsePct = static_cast<uint8_t>(mReading.lapsePct);
                mPending.state    = static_cast<uint8_t>(mReading.state);
                mPendingSave      = true;
                // Show it in the trend at once; the service's copy replaces this.
                mHistory.push(mPending);
            }
        }
        mPrev = now;
    } else if (pressed && now == Phase::WAITING && mEngine.outcome() == Pvt::Outcome::FALSE_START) {
        cue = Cue::FALSE_START;                // same phase: a jumped gun
        mEarlyUntilMs = nowMs + kEarlyNoticeMs;
    }
    return cue;
}

Reply Presenter::press(Button b, uint32_t nowMs)
{
    Reply r;
    switch (mMode) {
        case Mode::INTRO:
            if (b == Button::R1)      { begin(nowMs); }
            else if (b == Button::R2) { r.exit = true; }
            else if (b == Button::L2 && mHistory.count > 0) { mMode = Mode::HISTORY; }
            break;

        case Mode::HISTORY:
            if (b == Button::R1)      { begin(nowMs); }
            else                      { mMode = Mode::INTRO; }
            break;

        case Mode::RESULT:
            if (b == Button::R1) {
                begin(nowMs);
            } else if (b == Button::R2) {
                r.exit = true;
            } else if (b == Button::L2) {           // next page
                mPage = mPage == Page::VERDICT ? Page::TREND
                      : mPage == Page::TREND   ? Page::DETAILS : Page::VERDICT;
            } else if (b == Button::L1) {           // previous page
                mPage = mPage == Page::VERDICT ? Page::DETAILS
                      : mPage == Page::DETAILS ? Page::TREND : Page::VERDICT;
            }
            break;

        case Mode::RUN:
            if (b == Button::R2) {
                mMode = Mode::INTRO;           // cancel, do not leave the app
            } else {
                const Pvt::Outcome before = mEngine.outcome();
                const uint32_t falseBefore = mEngine.summary().falseStarts;
                mEngine.press(nowMs);
                const bool jumped = mEngine.summary().falseStarts != falseBefore ||
                                    (before == Pvt::Outcome::FALSE_START &&
                                     mEngine.outcome() == Pvt::Outcome::FALSE_START &&
                                     mEngine.phase() == Pvt::Phase::WAITING);
                r.cue = syncPhase(jumped, nowMs);
            }
            break;
    }
    return r;
}

bool Presenter::takeResult(Rd::Entry& out)
{
    if (!mPendingSave) {
        return false;
    }
    mPendingSave = false;
    out = mPending;
    return true;
}

Cue Presenter::tick(uint32_t nowMs)
{
    if (mMode != Mode::RUN) {
        return Cue::NONE;
    }
    mEngine.tick(nowMs);
    return syncPhase(false, nowMs);
}

Frame Presenter::frame(uint32_t nowMs) const
{
    Frame f;
    f.bg         = C::BLACK;
    f.accent     = kAccent;
    f.okColor    = kOk;
    f.lapseColor = kLapse;
    f.trackColor = kTrack;
    f.guideColor = kMuted;
    f.titleColor = C::WHITE;
    f.bigColor   = C::WHITE;
    f.subColor   = kMuted;
    f.sub3Color  = kMuted;

    if (mMode == Mode::INTRO) {
        f.layout     = Layout::INTRO;
        put(f.label, sizeof f.label, "NEUROREFLEX");
        f.labelColor = kAccent;
        const uint32_t n = mHistory.count;
        if (n == 0) {
            f.reticle      = true;             // first run: the target says what this is
            f.reticleColor = kAccent;
            put(f.sub, sizeof f.sub, "3-min focus check");
        } else if (n < Rd::kCalibration) {
            std::snprintf(f.big, sizeof f.big, "%u/%u", static_cast<unsigned>(n),
                          static_cast<unsigned>(Rd::kCalibration));
            put(f.sub, sizeof f.sub, "baseline tests");
        } else {
            std::snprintf(f.big, sizeof f.big, "%u", static_cast<unsigned>(Rd::baselineMs(mHistory)));
            put(f.sub, sizeof f.sub, "ms  your normal");
        }
        put(f.sub3, sizeof f.sub3, "START");
        f.sub3Color = kAccent;                 // the one call to action (drawn as a pill)
        f.r1 = Hint::AMBER;
        f.r2 = Hint::WHITE;
        if (n > 0) {
            f.l2 = Hint::WHITE;                // trend
        }
        return f;
    }
    if (mMode == Mode::HISTORY) {
        trendFrame(f);
        f.r1 = Hint::AMBER;
        f.r2 = Hint::WHITE;
        f.l1 = f.l2 = Hint::NONE;
        f.page = -1;
        return f;
    }

    if (mMode == Mode::RESULT) {
        switch (mPage) {
            case Page::VERDICT: verdictFrame(f); break;
            case Page::TREND:   trendFrame(f);   break;
            case Page::DETAILS: detailsFrame(f); break;
        }
        f.r1 = Hint::AMBER;                    // again
        f.r2 = Hint::WHITE;                    // done
        f.l1 = Hint::WHITE;                    // page back
        f.l2 = Hint::WHITE;                    // page on
        return f;
    }
    // RUN: session progress is shown everywhere the background allows.
    const uint32_t elapsed = kSessionMs - mEngine.remainingMs(nowMs);
    f.progress = static_cast<int32_t>((elapsed * 100ull) / kSessionMs);
    f.r2 = Hint::RED;                          // cancel, always

    switch (mEngine.phase()) {
        case Pvt::Phase::STIMULUS:
            f.layout     = Layout::GO;
            f.bg         = C::GREEN;
            f.titleColor = C::BLACK;
            f.bigColor   = C::BLACK;
            f.subColor   = C::BLACK;
            f.progress   = -1;                 // a bar would vanish on solid green
            put(f.title, sizeof f.title, "GO");
            std::snprintf(f.big, sizeof f.big, "%u",
                          static_cast<unsigned>(mEngine.stimulusElapsedMs(nowMs)));
            put(f.sub, sizeof f.sub, "press now");
            break;

        case Pvt::Phase::FEEDBACK: {
            const bool lapse = mEngine.outcome() == Pvt::Outcome::LAPSE;
            f.layout = Layout::FEEDBACK;
            put(f.title, sizeof f.title, lapse ? "SLOW" : "NICE");
            f.titleColor = lapse ? kLapse : kOk;
            std::snprintf(f.big, sizeof f.big, "%u", static_cast<unsigned>(mEngine.lastMs()));
            f.bigColor    = lapse ? kLapse : kOk;
            f.gaugeMs     = mEngine.lastMs();
            f.gaugeColor  = f.bigColor;
            put(f.sub, sizeof f.sub, "ms");
            break;
        }

        case Pvt::Phase::WAITING:
        default:
            f.layout       = Layout::WAIT;
            f.reticle      = true;
            f.reticleColor = kMuted;
            if (nowMs < mEarlyUntilMs) {
                put(f.title, sizeof f.title, "TOO SOON");
                f.titleColor   = kCaution;
                f.reticleColor = kCaution;
            } else {
                put(f.title, sizeof f.title, "WAIT");
            }
            put(f.sub, sizeof f.sub, "wait for GO");
            break;
    }
    return f;
}

void Presenter::verdictFrame(Frame& f) const
{
    const Rd::Reading& rd = mReading;
    const uint32_t col = stateColor(rd.state);
    f.layout     = Layout::RESULT;
    f.page       = 0;
    f.titleColor = col;
    f.bigColor   = C::WHITE;
    f.score      = rd.score;
    f.scoreColor = col;
    f.subColor   = C::WHITE;                   // the action
    f.sub3Color  = kMuted;                     // what the number means

    switch (rd.state) {
        case Rd::State::PRIMED:
            put(f.title, sizeof f.title, "PRIMED");
            put(f.sub,   sizeof f.sub,   "Go hard today");
            break;
        case Rd::State::NORMAL:
            put(f.title, sizeof f.title, "NORMAL");
            put(f.sub,   sizeof f.sub,   "Train as planned");
            break;
        case Rd::State::DRAINED:
            put(f.title, sizeof f.title, "DRAINED");
            put(f.sub,   sizeof f.sub,   "Easy day or rest");
            break;
        case Rd::State::LEARNING:
            put(f.title, sizeof f.title, "LEARNING");
            put(f.sub,   sizeof f.sub,   "Retest tomorrow");
            break;
        default:
            put(f.title, sizeof f.title, "TOO SHORT");
            put(f.sub,   sizeof f.sub,   "Not enough taps");
            f.score = -1;
            break;
    }

    if (rd.state == Rd::State::LEARNING ||
        (rd.state == Rd::State::DRAINED && rd.baselineMs == 0)) {
        std::snprintf(f.big, sizeof f.big, "%u/%u", static_cast<unsigned>(rd.testNo),
                      static_cast<unsigned>(Rd::kCalibration));
        put(f.sub2, sizeof f.sub2, "baseline tests");
    } else if (rd.state != Rd::State::NONE) {
        // Four glyphs at most so the number fits inside the arc.
        int d = static_cast<int>(rd.deltaPct);
        if (d > 99) d = 99;
        if (d < -99) d = -99;
        std::snprintf(f.big, sizeof f.big, "%+d%%", d);
        put(f.sub2, sizeof f.sub2, "vs your normal");
    }
    put(f.sub3, sizeof f.sub3, "");
}
void Presenter::trendFrame(Frame& f) const
{
    f.layout     = Layout::TREND;
    f.page       = 1;
    f.labelColor = kMuted;
    f.subColor   = C::WHITE;
    f.sub3Color  = kMuted;

    const uint32_t n    = mHistory.count;
    const uint32_t base = Rd::baselineMs(mHistory);
    std::snprintf(f.label, sizeof f.label, n == 1 ? "LAST TEST" : "LAST %u TESTS", static_cast<unsigned>(n));
    f.barCount = n;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const Rd::Entry& e = mHistory.at(i);
        sum += e.medianMs;
        // A test that matched your normal sits on the guide line; slower is taller.
        uint32_t h = kTrendNormalH;
        if (base) {
            h = (static_cast<uint32_t>(e.medianMs) * kTrendNormalH) / base;
        }
        if (h < kBarMinH) h = kBarMinH;
        if (h > kTrendMaxH) h = kTrendMaxH;
        f.barH[i]     = static_cast<uint8_t>(h);
        f.barColor[i] = stateColor(static_cast<Rd::State>(e.state));
    }
    if (base) {
        f.guideH = kTrendNormalH;
        std::snprintf(f.sub, sizeof f.sub, "normal %u ms", static_cast<unsigned>(base));
        put(f.sub2, sizeof f.sub2, "taller = slower");
    } else if (n) {
        std::snprintf(f.sub, sizeof f.sub, "avg %u ms", static_cast<unsigned>(sum / n));
        put(f.sub2, sizeof f.sub2, "learning you");
    } else {
        put(f.sub, sizeof f.sub, "no tests yet");
    }
}
void Presenter::detailsFrame(Frame& f) const
{
    const Pvt::Summary s = mEngine.summary();
    f.layout     = Layout::DETAILS;
    f.page       = 2;
    put(f.label, sizeof f.label, "MEDIAN MS");
    f.labelColor = kMuted;
    std::snprintf(f.big, sizeof f.big, "%u", static_cast<unsigned>(s.medianMs));
    f.bigColor   = C::WHITE;
    std::snprintf(f.sub,  sizeof f.sub,  "lapses %u of %u", static_cast<unsigned>(s.lapses),
                  static_cast<unsigned>(s.trials));
    std::snprintf(f.sub2, sizeof f.sub2, "too early %u", static_cast<unsigned>(s.falseStarts));
    f.subColor   = C::WHITE;
    f.sub3Color  = kMuted;

    // The newest kBars trials, oldest of those on the left.
    const uint32_t total = s.trials;
    const uint32_t from  = total > kBars ? total - kBars : 0;
    f.barCount = total - from;
    f.guideH   = kLapseLineH;
    for (uint32_t i = 0; i < f.barCount; ++i) {
        const uint32_t ms = mEngine.responseMs(from + i);
        f.barH[i] = barHeight(ms);
        const bool lapse = ms >= Pvt::kLapseMs;
        if (lapse) {
            f.barLapseMask |= 1u << i;
        }
        f.barColor[i] = lapse ? kLapse : kOk;
    }
}
} // namespace Nr
