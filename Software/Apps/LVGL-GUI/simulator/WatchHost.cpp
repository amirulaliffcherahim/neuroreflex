/**
 ******************************************************************************
 * @file    WatchHost.cpp
 * @brief   The SDK's LvglHost with the display shown inside the UNA watch (see WatchHost.hpp).
 ******************************************************************************
 */

#include "WatchHost.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include <SDL.h>

#include "SDK/GUI/Config.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Port/LVGL/LvglPort.hpp"
#include "SDK/Simulator/Components/InstanceSensorLayer.hpp"
#include "SDK/Simulator/Kernel/Mock/System.hpp"

#define LOG_MODULE_PRX      "WatchHost"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace Nr::Sim
{

namespace
{

/// The kernel reports a CLICK for a press shorter than this (HWButtons.cpp).
constexpr uint32_t kClickMaxMs = 500;

/// Texture format with bytes R, G, B, A in memory order. Spelled out because
/// the alias SDL_PIXELFORMAT_RGBA32 only exists from SDL 2.0.5 and the copy
/// TouchGFX ships is 2.0.4.
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
constexpr Uint32 kTextureFormat = SDL_PIXELFORMAT_RGBA8888;
#else
constexpr Uint32 kTextureFormat = SDL_PIXELFORMAT_ABGR8888;
#endif

using Btn = SDK::Message::EventButton;
namespace Mock     = SDK::Simulator::Mock;

// The watch photo (Tests/Sim/watch/watch_640.rgba, raw RGBA 640 x 640) and
// where its round display sits, measured from the stock face's gauge arcs.
constexpr int    kWatchPx = 640;
constexpr double kDispCx  = 320.32;
constexpr double kDispCy  = 333.12;
constexpr double kDispR   = 196.84;
const char* const kWatchFile = WATCH_RGBA_PATH;

/// The bezel spot of each button (L1, L2, R1, R2) in the photo, for the press glow.
constexpr int kBtnSpot[4][2] = { { 62, 200 }, { 62, 462 }, { 578, 190 }, { 578, 462 } };

/// Expand one ABGR2222 byte (A[7:6] B[5:4] G[3:2] R[1:0]) to 8-bit channels.
inline uint32_t abgr2222ToRgba32(uint8_t px)
{
    const uint32_t r = (px & 0x03) * 85u;
    const uint32_t g = ((px >> 2) & 0x03) * 85u;
    const uint32_t b = ((px >> 4) & 0x03) * 85u;
    // kTextureFormat: bytes R, G, B, A in memory order.
    return SDL_BYTEORDER == SDL_LIL_ENDIAN ? (0xFFu << 24) | (b << 16) | (g << 8) | r
                                           : (r << 24) | (g << 16) | (b << 8) | 0xFFu;
}

} // namespace

WatchHost::WatchHost(SDK::App::DualAppComm& comm, const SDK::Kernel& kernel, const Options& options)
    : mComm(comm)
    , mKernel(kernel)
    , mOptions(options)
{
}

WatchHost::~WatchHost()
{
    shutdown();
}

bool WatchHost::init()
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        LOG_ERROR("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    mSdlReady = true;   // whatever fails below, shutdown() releases SDL

    const int scale = mOptions.scale > 0 ? mOptions.scale : 1;
    (void)scale;
    mWindow = SDL_CreateWindow(mOptions.title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               kWatchPx, kWatchPx, SDL_WINDOW_SHOWN);
    if (!mWindow) {
        LOG_ERROR("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    mRenderer = SDL_CreateRenderer(mWindow, -1, 0);
    if (!mRenderer) {
        LOG_ERROR("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    mTexture = SDL_CreateTexture(mRenderer, kTextureFormat, SDL_TEXTUREACCESS_STREAMING,
                                 SDK::LVGL::kDisplayWidth, SDK::LVGL::kDisplayHeight);
    if (!mTexture) {
        LOG_ERROR("SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    if (!loadWatch()) {
        LOG_ERROR("watch image %s missing: showing the bare display\n", kWatchFile);
    }

    SDK::LVGL::Port::GetInstance().setFrameHook(&WatchHost::frameHook, this);
    Mock::SystemGUI::SetStopHandler(&WatchHost::stopFromSystem);

    LOG_INFO("Window %dx%d (x%d). Keys: 1=L1 2=L2 3=R1 4=R2 (q/w/e/r press, a/s/d/f release), Esc quits.\n",
             SDK::LVGL::kDisplayWidth, SDK::LVGL::kDisplayHeight, scale);
    return true;
}

void WatchHost::start()
{
    // What the kernel sends when it brings a GUI to the front.
    sendToService(SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN);
    sendToGui(SDK::MessageType::COMMAND_APP_GUI_RESUME);

    mTicking    = true;
    mStarted    = true;
    mTickThread = std::thread(&WatchHost::tickThread, this);
}

void WatchHost::run()
{
    SDK::LVGL::Port::GetInstance().run();
}

void WatchHost::shutdown()
{
    if (mTicking.exchange(false) && mTickThread.joinable()) {
        mTickThread.join();
    }

    if (mStarted) {
        // The GUI has already stopped (run() returned); now the service. Only
        // once start() told it the GUI was running: an init() that failed
        // never did, and the service thread was never started.
        mStarted = false;
        sendToService(SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP);
        sendToService(SDK::MessageType::COMMAND_APP_STOP);
    }

    for (SDL_Texture** t : { &mTexture, &mWatch, &mMask, &mGlare }) {
        if (*t) {
            SDL_DestroyTexture(*t);
            *t = nullptr;
        }
    }
    if (mRenderer) {
        SDL_DestroyRenderer(mRenderer);
        mRenderer = nullptr;
    }
    if (mWindow) {
        SDL_DestroyWindow(mWindow);
        mWindow = nullptr;
    }
    if (mSdlReady) {
        mSdlReady = false;
        SDL_Quit();
    }
}

// --- hooks -----------------------------------------------------------------

void WatchHost::frameHook(void* ctx)
{
    auto* self = static_cast<WatchHost*>(ctx);
    // The GUI thread is running again; the tick thread may post once more.
    // (It may run for a button event too, so at most one extra tick queues.)
    self->mTickPending = false;
    self->pumpEvents();
    self->present(false);
}

void WatchHost::stopFromSystem()
{
    // The app called sys.exit(), or the kernel-side stop reached the GUI.
    SDK::LVGL::Port::GetInstance().stop();
}

// --- window ----------------------------------------------------------------

void WatchHost::pumpEvents()
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                requestStop();
                break;

            case SDL_KEYDOWN: {
                if (e.key.repeat) {
                    break;
                }
                const int32_t key = e.key.keysym.sym;
                Btn::Id    id;
                Btn::Event event;
                if (key == SDLK_ESCAPE) {
                    requestStop();
                } else if (keyToButton(key, id)) {
                    const auto i = static_cast<unsigned>(id);
                    mPressed[i]     = true;
                    mPressedAtMs[i] = mKernel.sys.getTimeMs();
                    sendButton(id, Btn::Event::PRESS);
                } else if (keyToRawEvent(key, id, event)) {
                    // One press or release, no click: the rows a TouchGFX
                    // simulator feeds straight through as codes.
                    sendButton(id, event);
                } else if (key > 0 && key < 0x80) {
                    // Simulator-only keys, e.g. '5' raises a wrist-motion event.
                    Instance::SensorLayer::getInstance().handlerButtons(static_cast<uint8_t>(key));
                }
            } break;

            case SDL_KEYUP: {
                Btn::Id id;
                if (keyToButton(e.key.keysym.sym, id)) {
                    const auto i = static_cast<unsigned>(id);
                    if (mPressed[i]) {
                        mPressed[i] = false;
                        if (mKernel.sys.getTimeMs() - mPressedAtMs[i] < kClickMaxMs) {
                            sendButton(id, Btn::Event::CLICK);
                        }
                        sendButton(id, Btn::Event::RELEASE);
                    }
                }
            } break;

            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_EXPOSED) {
                    present(true);
                }
                break;

            default:
                break;
        }
    }
}

void WatchHost::present(bool force)
{
    auto& port = SDK::LVGL::Port::GetInstance();
    if (!force && port.frameCount() == mShownFrame) {
        return;
    }
    mShownFrame = port.frameCount();

    void* pixels = nullptr;
    int   pitch  = 0;
    if (SDL_LockTexture(mTexture, nullptr, &pixels, &pitch) != 0) {
        return;
    }
    const uint8_t* src = port.frame();
    for (int y = 0; y < SDK::LVGL::kDisplayHeight; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pixels) + y * pitch);
        for (int x = 0; x < SDK::LVGL::kDisplayWidth; ++x) {
            row[x] = abgr2222ToRgba32(*src++);
        }
    }
    SDL_UnlockTexture(mTexture);

    SDL_SetRenderDrawColor(mRenderer, 22, 22, 26, 255);
    SDL_RenderClear(mRenderer);
    if (!mWatch) {
        SDL_RenderCopy(mRenderer, mTexture, nullptr, nullptr);
        SDL_RenderPresent(mRenderer);
        return;
    }
    // The photo, the display square over its screen, a mask that restores the
    // photo outside the round display (hiding the square's corners), then the
    // glass sheen.
    SDL_RenderCopy(mRenderer, mWatch, nullptr, nullptr);
    const int d = static_cast<int>(std::lround(kDispR * 2));
    SDL_Rect disp { static_cast<int>(std::lround(kDispCx - kDispR)),
                    static_cast<int>(std::lround(kDispCy - kDispR)), d, d };
    SDL_RenderCopy(mRenderer, mTexture, nullptr, &disp);
    SDL_RenderCopy(mRenderer, mMask, nullptr, nullptr);
    SDL_RenderCopy(mRenderer, mGlare, nullptr, &disp);

    // Pressed buttons glow on the bezel. mPressed is indexed by button id:
    // SW1 = L1, SW2 = R1, SW3 = L2, SW4 = R2.
    static const int spotOf[4] = { 0, 2, 1, 3 };
    SDL_SetRenderDrawBlendMode(mRenderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < 4; ++i) {
        if (!mPressed[i]) {
            continue;
        }
        const int* p = kBtnSpot[spotOf[i]];
        for (int r = 16; r > 0; r -= 4) {
            SDL_SetRenderDrawColor(mRenderer, 255, 200, 0, static_cast<Uint8>(200 - r * 9));
            SDL_Rect g { p[0] - r, p[1] - r * 2, r * 2, r * 4 };
            SDL_RenderFillRect(mRenderer, &g);
        }
    }
    SDL_RenderPresent(mRenderer);
}

bool WatchHost::loadWatch()
{
    std::vector<uint8_t> px(static_cast<size_t>(kWatchPx) * kWatchPx * 4);
    FILE* f = std::fopen(kWatchFile, "rb");
    if (!f) {
        return false;
    }
    const size_t got = std::fread(px.data(), 1, px.size(), f);
    std::fclose(f);
    if (got != px.size()) {
        return false;
    }

    auto make = [this](const std::vector<uint8_t>& rgba, int w, int h) {
        SDL_Texture* t = SDL_CreateTexture(mRenderer, kTextureFormat, SDL_TEXTUREACCESS_STATIC, w, h);
        if (t) {
            SDL_UpdateTexture(t, nullptr, rgba.data(), w * 4);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        }
        return t;
    };
    mWatch = make(px, kWatchPx, kWatchPx);

    // Mask: clear inside the display circle, a thin dark rim at its edge, and
    // the photo itself everywhere else (so the display square's corners vanish).
    std::vector<uint8_t> mask(px.size(), 0);
    for (int y = 0; y < kWatchPx; ++y) {
        for (int x = 0; x < kWatchPx; ++x) {
            const double dx = x + 0.5 - kDispCx, dy = y + 0.5 - kDispCy;
            const double r  = std::sqrt(dx * dx + dy * dy);
            const size_t i  = (static_cast<size_t>(y) * kWatchPx + x) * 4;
            uint8_t* m = &mask[i];
            if (r < kDispR) {
                continue;                                       // display shows
            }
            if (r < kDispR + 3.0) {                             // anti-aliased rim
                m[0] = m[1] = m[2] = 6;
                m[3] = static_cast<uint8_t>(r - kDispR >= 1.0 ? 255 : (r - kDispR) * 255);
            } else {
                m[0] = px[i]; m[1] = px[i + 1]; m[2] = px[i + 2]; m[3] = px[i + 3];
            }
        }
    }
    mMask = make(mask, kWatchPx, kWatchPx);

    // Glass sheen: a soft diagonal band across the upper left, as in the photo.
    const int n = 128;
    std::vector<uint8_t> glare(static_cast<size_t>(n) * n * 4, 0);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const double t = (x + y * 1.18) / n;
            double a = 0;
            if (t < 0.95) a = 22.0 * (t < 0.85 ? 1.0 : (0.95 - t) / 0.10);
            if (t < 0.50) a += 8.0;
            const double cx = x + 0.5 - n / 2.0, cy = y + 0.5 - n / 2.0;
            if (cx * cx + cy * cy >= (n / 2.0) * (n / 2.0)) a = 0;   // glass only
            uint8_t* g = &glare[(static_cast<size_t>(y) * n + x) * 4];
            g[0] = g[1] = g[2] = 255;
            g[3] = static_cast<uint8_t>(a);
        }
    }
    mGlare = make(glare, n, n);
    if (mGlare) {
        SDL_SetTextureBlendMode(mGlare, SDL_BLENDMODE_ADD);
    }
    return mWatch && mMask;
}

void WatchHost::requestStop()
{
    if (mStopRequested) {
        return;
    }
    // Same order as the kernel: the GUI is taken off screen, then stopped.
    // The port handles COMMAND_APP_STOP by calling sys.exit(), which reaches
    // stopFromSystem() and ends run(). The request is latched only once both
    // commands are queued: after a stall the queue may be full, and the next
    // Escape or close must then be able to try again.
    if (sendToGui(SDK::MessageType::COMMAND_APP_GUI_SUSPEND) &&
        sendToGui(SDK::MessageType::COMMAND_APP_STOP)) {
        mStopRequested = true;
    }
}

// --- kernel emulation --------------------------------------------------------

void WatchHost::tickThread()
{
    const auto period = std::chrono::milliseconds(1000 / SDK::GUI::Config::kFrameRate);
    uint32_t frame = 0;
    while (mTicking) {
        std::this_thread::sleep_for(period);
        if (mTickPending) {
            // The GUI thread has not run since the last tick (a breakpoint, a
            // window drag on Windows); more ticks would only fill the queue
            // and crowd out the stop commands.
            continue;
        }
        auto msg = SDK::make_msg<SDK::Message::EventGuiTick>(mKernel);
        if (!msg) {
            continue;
        }
        msg->timestamp   = mKernel.sys.getTimeMs();
        msg->frameNumber = frame++;
        if (!mComm.sendToGui(msg.get())) {
            continue;   // the guard releases it
        }
        mTickPending = true;
        msg.release();  // the GUI releases it after handling
    }
}

bool WatchHost::sendToGui(SDK::MessageType::Type type)
{
    auto msg = SDK::make_msg(mKernel, type);
    if (msg && mComm.sendToGui(msg.get())) {
        msg.release();
        return true;
    }
    return false;
}

void WatchHost::sendToService(SDK::MessageType::Type type)
{
    auto msg = SDK::make_msg(mKernel, type);
    if (msg && mComm.sendToService(msg.get())) {
        msg.release();
    }
}

void WatchHost::sendButton(Btn::Id id, Btn::Event event)
{
    auto msg = SDK::make_msg<Btn>(mKernel);
    if (!msg) {
        return;
    }
    msg->timestamp = mKernel.sys.getTimeMs();
    msg->id        = id;
    msg->event     = event;
    if (mComm.sendToGui(msg.get())) {
        msg.release();
    }
}

bool WatchHost::keyToButton(int32_t key, Btn::Id& id)
{
    // The click row of SDK/GUI/Button.hpp: 1=L1 2=L2 3=R1 4=R2. Here a key
    // stands for the physical button, so its down and up become PRESS and
    // RELEASE (and a CLICK when short), where a TouchGFX simulator would pass
    // the digit through as the click code alone.
    // Button ids follow the hardware: SW1=L1, SW2=R1, SW3=L2, SW4=R2.
    switch (key) {
        case SDLK_1: id = Btn::Id::SW1; return true;
        case SDLK_2: id = Btn::Id::SW3; return true;
        case SDLK_3: id = Btn::Id::SW2; return true;
        case SDLK_4: id = Btn::Id::SW4; return true;
        default:     return false;
    }
}

bool WatchHost::keyToRawEvent(int32_t key, Btn::Id& id, Btn::Event& event)
{
    // The press row (q w e r) and the release row (a s d f) of Button.hpp,
    // one column per button in the order L1 L2 R1 R2.
    switch (key) {
        case SDLK_q: id = Btn::Id::SW1; event = Btn::Event::PRESS;   return true;
        case SDLK_w: id = Btn::Id::SW3; event = Btn::Event::PRESS;   return true;
        case SDLK_e: id = Btn::Id::SW2; event = Btn::Event::PRESS;   return true;
        case SDLK_r: id = Btn::Id::SW4; event = Btn::Event::PRESS;   return true;
        case SDLK_a: id = Btn::Id::SW1; event = Btn::Event::RELEASE; return true;
        case SDLK_s: id = Btn::Id::SW3; event = Btn::Event::RELEASE; return true;
        case SDLK_d: id = Btn::Id::SW2; event = Btn::Event::RELEASE; return true;
        case SDLK_f: id = Btn::Id::SW4; event = Btn::Event::RELEASE; return true;
        default:     return false;
    }
}

} // namespace Nr::Sim
