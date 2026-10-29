#include "gui/screens/MainScreen.hpp"

#include "SDK/GUI/Button.hpp"
#include "SDK/GUI/LVGL/Draw.hpp"

#include "gui/Assets.hpp"

namespace Draw = SDK::LVGL::Draw;
namespace Btn  = SDK::GUI::Button;

namespace
{
constexpr uint32_t kFrameMs = 20;   // 50 Hz: finer than a reaction time needs

// The bezel arcs sit on radius 113, so content stays inside roughly radius 100.
constexpr int32_t kReticleCx = 120;
constexpr int32_t kReticleCy = 100;
constexpr int32_t kReticleR  = 36;

// Session progress: a thin ring just inside the bezel button arcs.
constexpr int32_t kProgR = 104, kProgW = 4;
constexpr int32_t kGaugeX = 40, kGaugeW = 160, kGaugeY = 136, kGaugeH = 12;
constexpr int32_t kStripX = 40, kStripBase = 136, kBarW = 4, kBarPitch = 5;
constexpr int32_t kTrendBase = 150, kTrendW = 11, kTrendPitch = 14;  // up to 14 wide bars
constexpr int32_t kDetailBase = 150;

// Readiness arc: a 270-degree gauge opening at the bottom, the hero of the result.
constexpr int32_t kScoreR = 92, kScoreW = 14, kScoreA0 = 240, kScoreSpan = 240;
// Where the gauge crosses from drained to normal (score 40) and normal to primed (82),
// matching Readiness.cpp: 70 at your normal, 3 points per percent.
constexpr int32_t kScoreTicks[2] = { 40, 82 };

SDK::LVGL::Buttons::Color hint(Nr::Hint h)
{
    return static_cast<SDK::LVGL::Buttons::Color>(h);
}

void setColor(lv_obj_t* o, uint32_t c)
{
    lv_obj_set_style_bg_color(o, Draw::rgb(c), LV_PART_MAIN);
}
} // namespace

MainScreen::MainScreen(Model& model)
    : mModel(model)
{
    mRoot = lv_obj_create(nullptr);
    Draw::applyScreen(mRoot);
    lv_obj_add_event_cb(mRoot, &MainScreen::keyEventCb, LV_EVENT_KEY, this);

    // Shapes first so text always draws on top of them.
    mRing = Draw::arc(mRoot, kReticleCx, kReticleCy, kReticleR, 3, 0, 360, 0xC0C0C0, false);
    mTickTop    = Draw::box(mRoot, kReticleCx - 1, kReticleCy - kReticleR - 10, 3, 14, 0xC0C0C0, 1);
    mTickBottom = Draw::box(mRoot, kReticleCx - 1, kReticleCy + kReticleR - 4,  3, 14, 0xC0C0C0, 1);
    mTickLeft   = Draw::box(mRoot, kReticleCx - kReticleR - 10, kReticleCy - 1, 14, 3, 0xC0C0C0, 1);
    mTickRight  = Draw::box(mRoot, kReticleCx + kReticleR - 4,  kReticleCy - 1, 14, 3, 0xC0C0C0, 1);
    mDot        = Draw::dot(mRoot, kReticleCx, kReticleCy, 4, 0xC0C0C0);

    mProgTrack = Draw::arc(mRoot, 120, 120, kProgR, kProgW, 0, 360, 0x404040, false);
    mProgFill  = Draw::arc(mRoot, 120, 120, kProgR, kProgW, 0, 1, 0xC0C0C0);

    mGaugeTrack = Draw::box(mRoot, kGaugeX, kGaugeY, kGaugeW, kGaugeH, 0x404040, LV_RADIUS_CIRCLE);
    mGaugeFill  = Draw::box(mRoot, kGaugeX, kGaugeY, kGaugeH, kGaugeH, 0xC0C0C0, LV_RADIUS_CIRCLE);
    mGaugeMark  = Draw::box(mRoot, kGaugeX + kGaugeW / 2 - 1, kGaugeY - 5, 3, kGaugeH + 10, 0xC0C0C0, 0);

    mScoreTrack = Draw::arc(mRoot, 120, 120, kScoreR, kScoreW, kScoreA0, kScoreA0 + kScoreSpan, 0x404040);
    mScoreArc   = Draw::arc(mRoot, 120, 120, kScoreR, kScoreW, kScoreA0, kScoreA0 + 1, 0xC0C0C0);
    for (int i = 0; i < 2; ++i) {
        // Black notches cut the track into its three zones.
        const int32_t a = kScoreA0 + (kScoreTicks[i] * kScoreSpan) / 100;
        mScoreTicks[i] = Draw::arc(mRoot, 120, 120, kScoreR, kScoreW + 4, a, a + 2, 0x000000, false);
    }
    mScoreTicks[2] = nullptr;

    mLapseLine = Draw::box(mRoot, kStripX, kStripBase - Nr::kLapseLineH, Nr::kBars * kBarPitch, 1,
                           0x808080, 0);
    for (uint32_t i = 0; i < Nr::kBars; ++i) {
        mBars[i] = Draw::box(mRoot, kStripX + static_cast<int32_t>(i) * kBarPitch,
                             kStripBase - Nr::kBarMinH, kBarW, Nr::kBarMinH, 0x00C000, 2);
    }

    mPill = Draw::box(mRoot, 70, 168, 100, 34, 0x00C0C0, LV_RADIUS_CIRCLE);
    for (int i = 0; i < 3; ++i) {
        mDots[i] = Draw::dot(mRoot, 108 + i * 12, 214, 3, 0x808080);
    }

    // The corners of the 240 px face are cut by the bezel: keep text central.
    mLabel = Draw::label(mRoot, &nr_18, "", 40, 30,  160);
    mTitle = Draw::label(mRoot, &nr_28, "", 30, 10,  180);
    mBig   = Draw::label(mRoot, &nr_64, "", 10, 78,  220);
    mSub   = Draw::label(mRoot, &nr_18, "", 20, 150, 200);
    mSub2  = Draw::label(mRoot, &nr_18, "", 20, 174, 200);
    mSub3  = Draw::label(mRoot, &nr_18, "", 20, 198, 200);
    lv_obj_set_style_text_letter_space(mLabel, 2, LV_PART_MAIN);
    mButtons = std::make_unique<SDK::LVGL::Buttons>(mRoot);

    bind(&mModel);
    mModel.bind(this);

    mTimer = lv_timer_create(&MainScreen::tickCb, kFrameMs, this);
    mModel.requestHistory();
    render();
}

MainScreen::~MainScreen()
{
    if (mTimer) {
        lv_timer_delete(mTimer);
    }
    mModel.bind(nullptr);
    lv_obj_delete(mRoot);
}

void MainScreen::keyEventCb(lv_event_t* e)
{
    auto* self = static_cast<MainScreen*>(lv_event_get_user_data(e));
    self->onKey(static_cast<uint8_t>(lv_event_get_key(e)));
}

void MainScreen::tickCb(lv_timer_t* t)
{
    static_cast<MainScreen*>(lv_timer_get_user_data(t))->onTick();
}

void MainScreen::deliver(Nr::Cue cue)
{
    if (cue != Nr::Cue::NONE) {
        // Cue is HapticKind shifted by one (NONE = 0).
        mModel.haptic(static_cast<uint32_t>(cue) - 1);
    }
}

void MainScreen::onKey(uint8_t code)
{
    // A reaction is judged on the press itself, not on the click after release.
    Nr::Button b;
    switch (code) {
        case Btn::L1_PRESS: b = Nr::Button::L1; break;
        case Btn::L2_PRESS: b = Nr::Button::L2; break;
        case Btn::R1_PRESS: b = Nr::Button::R1; break;
        case Btn::R2_PRESS: b = Nr::Button::R2; break;
        default: return;
    }
    const Nr::Reply r = mPresenter.press(b, mModel.nowMs());
    deliver(r.cue);
    if (r.exit) {
        mModel.exitApp();
        return;
    }
    render();
}

void MainScreen::onHistory(const Rd::History& h)
{
    mPresenter.setHistory(h);
    render();
}

void MainScreen::onTick()
{
    deliver(mPresenter.tick(mModel.nowMs()));
    Rd::Entry e;
    if (mPresenter.takeResult(e)) {
        mModel.saveResult(e);
    }
    render();
}

void MainScreen::place(lv_obj_t* label, int32_t y)
{
    lv_obj_set_y(label, y);
}

void MainScreen::render()
{
    const Nr::Frame f = mPresenter.frame(mModel.nowMs());

    if (mHaveShown && f == mShown) {
        return;
    }
    mShown     = f;
    mHaveShown = true;

    setColor(mRoot, f.bg);

    auto paint = [](lv_obj_t* l, const char* s, uint32_t c) {
        lv_label_set_text(l, s);
        lv_obj_set_style_text_color(l, Draw::rgb(c), LV_PART_MAIN);
    };
    paint(mLabel, f.label, f.labelColor);
    paint(mTitle, f.title, f.titleColor);
    paint(mBig,   f.big,   f.bigColor);
    paint(mSub,   f.sub,   f.subColor);
    paint(mSub2,  f.sub2,  f.layout == Nr::Layout::RESULT || f.layout == Nr::Layout::TREND ? 0x808080u : f.subColor);
    paint(mSub3,  f.sub3,  f.sub3Color);

    // Where things go depends on the composition, never on the text. Each
    // layout is a vertical stack centred on the round face.
    using L = Nr::Layout;
    int32_t labelY = 30, titleY = 10, bigY = 78, subY = 150, sub2Y = 174, sub3Y = 198;
    switch (f.layout) {
        case L::INTRO:    labelY = 40; bigY = 70; subY = 140; sub3Y = 175; break;
        case L::WAIT:     titleY = 24; subY = 158; break;
        case L::GO:       titleY = 24; bigY = 78; subY = 160; break;
        case L::FEEDBACK: titleY = 26; bigY = 56; subY = 158; break;
        case L::RESULT:   titleY = 52; bigY = 76; sub2Y = 150; subY = 184; break;
        case L::TREND:    labelY = 36; subY = 162; sub2Y = 184; break;
        case L::DETAILS:  labelY = 30; bigY = 46; subY = 160; sub2Y = 182; break;
    }
    // The first-run intro shows the target instead of a number.
    if (f.layout == L::INTRO && f.reticle) {
        labelY = 20;
        subY = 152;
        sub3Y = 178;
    }
    place(mLabel, labelY);
    place(mTitle, titleY);
    place(mBig,   bigY);
    place(mSub,   subY);
    place(mSub2,  sub2Y);
    place(mSub3,  sub3Y);
    lv_obj_set_style_text_font(mBig, &nr_64, LV_PART_MAIN);

    // START pill behind the intro's call to action, its text in black.
    const bool pill = f.layout == L::INTRO && f.sub3[0];
    Draw::setHidden(mPill, !pill);
    if (pill) {
        setColor(mPill, f.sub3Color);
        lv_obj_set_y(mPill, sub3Y - 6);
        lv_obj_set_style_text_color(mSub3, Draw::rgb(0x000000), LV_PART_MAIN);
    }

    // Result page dots.
    for (int i = 0; i < 3; ++i) {
        Draw::setHidden(mDots[i], f.page < 0);
        if (f.page >= 0) {
            setColor(mDots[i], i == f.page ? 0xC0C0C0u : 0x404040u);
        }
    }

    // Reticle (first-run intro and waiting). On the intro it sits higher.
    const bool ret = f.reticle;
    lv_obj_t* const marks[] = {mTickTop, mTickBottom, mTickLeft, mTickRight, mDot};
    Draw::setHidden(mRing, !ret);
    for (lv_obj_t* o : marks) {
        Draw::setHidden(o, !ret);
    }
    if (ret) {
        Draw::setArcColor(mRing, f.reticleColor);
        for (lv_obj_t* o : marks) {
            setColor(o, f.reticleColor);
        }
    }

    // Session progress: a ring that closes clockwise from 12 o'clock.
    const bool prog = f.progress >= 0;
    Draw::setHidden(mProgTrack, !prog);
    Draw::setHidden(mProgFill,  !prog || f.progress == 0);
    if (prog) {
        Draw::setArcColor(mProgTrack, f.trackColor);
        Draw::setArcColor(mProgFill,  f.accent);
        Draw::setArc(mProgFill, 0, 1 + (f.progress * 359) / 100);
    }
    // Feedback gauge: fill is the reaction, the mark is the 500 ms lapse line.
    const bool gauge = f.layout == Nr::Layout::FEEDBACK;
    Draw::setHidden(mGaugeTrack, !gauge);
    Draw::setHidden(mGaugeFill,  !gauge);
    Draw::setHidden(mGaugeMark,  !gauge);
    if (gauge) {
        setColor(mGaugeTrack, f.trackColor);
        setColor(mGaugeFill,  f.gaugeColor);
        setColor(mGaugeMark,  f.guideColor);
        const uint32_t ms = f.gaugeMs > Nr::kBarScaleMs ? Nr::kBarScaleMs : f.gaugeMs;
        const int32_t  w  = kGaugeH + static_cast<int32_t>((ms * (kGaugeW - kGaugeH)) / Nr::kBarScaleMs);
        lv_obj_set_width(mGaugeFill, w);
    }

    // Readiness arc on the verdict page.
    const bool score = f.score >= 0;
    Draw::setHidden(mScoreTrack, !score);
    Draw::setHidden(mScoreArc,   !score);
    for (int i = 0; i < 2; ++i) {
        Draw::setHidden(mScoreTicks[i], !score);
    }
    if (score) {
        const int32_t span = 1 + (f.score * (kScoreSpan - 1)) / 100;
        Draw::setArc(mScoreArc, kScoreA0, kScoreA0 + span);
        Draw::setArcColor(mScoreArc, f.scoreColor);
    }

    // Bar strip, centred: one bar per trial (DETAILS) or per test (TREND).
    const bool trend = f.layout == Nr::Layout::TREND;
    const bool strip = (trend || f.layout == Nr::Layout::DETAILS) && f.barCount > 0;
    const int32_t pitch = trend ? kTrendPitch : kBarPitch;
    const int32_t width = trend ? kTrendW : kBarW;
    const int32_t base  = trend ? kTrendBase : kDetailBase;
    const int32_t used  = static_cast<int32_t>(f.barCount) * pitch - (pitch - width);
    const int32_t x0    = 120 - used / 2;
    Draw::setHidden(mLapseLine, !(strip && f.guideH > 0));
    if (strip && f.guideH > 0) {
        setColor(mLapseLine, f.guideColor);
        lv_obj_set_pos(mLapseLine, x0 - 6, base - f.guideH);
        lv_obj_set_width(mLapseLine, used + 12);
    }
    for (uint32_t i = 0; i < Nr::kBars; ++i) {
        const bool on = strip && i < f.barCount;
        Draw::setHidden(mBars[i], !on);
        if (on) {
            const int32_t h = f.barH[i];
            lv_obj_set_pos(mBars[i], x0 + static_cast<int32_t>(i) * pitch, base - h);
            lv_obj_set_size(mBars[i], width, h);
            setColor(mBars[i], f.barColor[i]);
        }
    }

    mButtons->set(hint(f.l1), hint(f.l2), hint(f.r1), hint(f.r2));
}
