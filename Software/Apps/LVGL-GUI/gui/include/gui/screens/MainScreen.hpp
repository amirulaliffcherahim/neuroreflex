#ifndef MAIN_SCREEN_HPP
#define MAIN_SCREEN_HPP

#include <cstdint>
#include <memory>

#include "lvgl.h"

#include "SDK/GUI/LVGL/Buttons.hpp"

#include "Presenter.hpp"
#include "gui/model/Model.hpp"
#include "gui/model/ModelListener.hpp"

class MainScreen : public ModelListener
{
public:
    explicit MainScreen(Model& model);
    ~MainScreen();

    MainScreen(const MainScreen&)            = delete;
    MainScreen& operator=(const MainScreen&) = delete;

    lv_obj_t* root() const { return mRoot; }

    void onHistory(const Rd::History& h) override;

private:
    void onKey(uint8_t code);
    void onTick();
    void render();
    void deliver(Nr::Cue cue);
    static void place(lv_obj_t* label, int32_t y);

    static void keyEventCb(lv_event_t* e);
    static void tickCb(lv_timer_t* t);

    Model&      mModel;
    lv_obj_t*   mRoot  = nullptr;
    lv_obj_t*   mTitle = nullptr;
    lv_obj_t*   mBig   = nullptr;
    lv_obj_t*   mSub   = nullptr;
    lv_obj_t*   mSub2  = nullptr;
    lv_obj_t*   mSub3  = nullptr;
    lv_timer_t* mTimer = nullptr;

    // Reticle (intro and wait), session progress, feedback gauge, result strip.
    lv_obj_t* mRing = nullptr;
    lv_obj_t* mTickTop = nullptr;
    lv_obj_t* mTickBottom = nullptr;
    lv_obj_t* mTickLeft = nullptr;
    lv_obj_t* mTickRight = nullptr;
    lv_obj_t* mDot = nullptr;
    lv_obj_t* mProgTrack = nullptr;
    lv_obj_t* mProgFill = nullptr;
    lv_obj_t* mGaugeTrack = nullptr;
    lv_obj_t* mGaugeFill = nullptr;
    lv_obj_t* mGaugeMark = nullptr;
    lv_obj_t* mLapseLine = nullptr;
    lv_obj_t* mBars[Nr::kBars] = {};
    lv_obj_t* mScoreTrack = nullptr;   ///< readiness arc, grey
    lv_obj_t* mScoreArc   = nullptr;   ///< readiness arc, coloured part
    lv_obj_t* mScoreTicks[3] = {};     ///< drained | normal | primed boundaries
    lv_obj_t* mLabel   = nullptr;      ///< small heading
    lv_obj_t* mPill    = nullptr;      ///< START call-to-action pill
    lv_obj_t* mDots[3] = {};           ///< result page dots

    Nr::Presenter mPresenter;
    Nr::Frame     mShown;              ///< what is on screen, to repaint only on change
    bool          mHaveShown = false;

    std::unique_ptr<SDK::LVGL::Buttons> mButtons;
};

#endif
