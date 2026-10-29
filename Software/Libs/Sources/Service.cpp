#include "SDK/Messages/SensorLayerMessages.hpp"

#include "Service.hpp"
#include "Commands.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

Service::Service(SDK::Kernel& kernel)
    : mKernel(SDK::KernelProviderService::GetInstance().getKernel())
    , mGUIStarted(false)
{}

void Service::run()
{
    LOG_INFO("thread started\n");

    while (true) {
        SDK::MessageBase *msg;
        if (mKernel.comm.getMessage(msg, 1000)) {
            // Command handling
            switch (msg->getType()) {
                // Kernel messages
                case SDK::MessageType::COMMAND_APP_STOP:
                    LOG_INFO("Force exit from the application\n");
                    // We must release message because this is the last event.
                    mKernel.comm.releaseMessage(msg);
                    return;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                    LOG_INFO("GUI is now running\n");
                    onStartGUI();
                    break;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                    LOG_INFO("GUI has stopped\n");
                    onStopGUI();
                    break;

                case CustomMessage::HAPTIC:
                    playHaptic(static_cast<CustomMessage::Haptic*>(msg)->kind);
                    break;

                case CustomMessage::GET_HISTORY:
                    loadHistory();
                    sendHistory();
                    break;

                case CustomMessage::ADD_RESULT: {
                    loadHistory();
                    Rd::Entry e = static_cast<CustomMessage::AddResult*>(msg)->entry;
                    // Trust boundary: the file outlives this build, keep values sane.
                    if (e.medianMs >= 100 && e.medianMs <= 2000 && e.lapsePct <= 100) {
                        mHistory.push(e);
                        saveHistory();
                    } else {
                        LOG_WARNING("result out of range, not saved (median %u)\n", e.medianMs);
                    }
                    sendHistory();
                } break;

                default:
                    break;
            }

            // Release message after processing
            mKernel.comm.releaseMessage(msg);
        }
    }

    LOG_INFO("thread stopped\n");
}

void Service::onStartGUI()
{
    LOG_INFO("GUI started\n");
    mGUIStarted = true;
}

void Service::onStopGUI()
{
    LOG_INFO("GUI stopped\n");
    mGUIStarted = false;
}

namespace
{
constexpr const char* kHistoryFile = "history.bin";
constexpr uint32_t    kHistoryMagic = 0x3152524Eu;   // "NRR1"
} // namespace

void Service::loadHistory()
{
    if (mHistoryLoaded) {
        return;
    }
    mHistoryLoaded = true;
    mHistory = Rd::History{};

    auto file = mKernel.fs.file(kHistoryFile);
    if (!file || !file->open(false)) {
        LOG_INFO("no history yet\n");
        return;
    }
    struct { uint32_t magic; Rd::History h; } buf {};
    size_t got = 0;
    const bool ok = file->read(reinterpret_cast<char*>(&buf), sizeof buf, got);
    file->close();
    if (!ok || got != sizeof buf || buf.magic != kHistoryMagic || buf.h.count > Rd::kMaxHistory) {
        LOG_WARNING("history file unreadable, starting fresh\n");
        return;
    }
    for (uint32_t i = 0; i < buf.h.count; ++i) {
        const Rd::Entry& e = buf.h.e[i];
        if (e.medianMs < 100 || e.medianMs > 2000 || e.lapsePct > 100) {
            LOG_WARNING("history entry %u out of range, starting fresh\n", i);
            return;
        }
    }
    mHistory = buf.h;
    LOG_INFO("history loaded: %u tests\n", mHistory.count);
}

void Service::saveHistory()
{
    auto file = mKernel.fs.file(kHistoryFile);
    if (!file || !file->open(true, true)) {
        LOG_ERROR("cannot open %s for writing\n", kHistoryFile);
        return;
    }
    struct { uint32_t magic; Rd::History h; } buf { kHistoryMagic, mHistory };
    size_t put = 0;
    if (!file->write(reinterpret_cast<const char*>(&buf), sizeof buf, put) || put != sizeof buf) {
        LOG_ERROR("history write failed\n");
    }
    file->flush();
    file->close();
}

void Service::sendHistory()
{
    SDK::send_msg<CustomMessage::History>(mKernel, mHistory);
}

uint32_t Service::ParseVersion(const char* str)
{
    if (str == nullptr) {
        return 0;
    }

    typedef union {
        struct {
            uint8_t patch;
            uint8_t minor;
            uint8_t major;
        }ver;
        uint32_t u32;
    } FirmwareVersion_t;

    FirmwareVersion_t v{};

    int major, minor, patch;

    if (sscanf(str, "%d.%d.%d", &major, &minor, &patch) == 3) {
        v.ver.major = static_cast<uint8_t>(major);
        v.ver.minor = static_cast<uint8_t>(minor);
        v.ver.patch = static_cast<uint8_t>(patch);
        return v.u32;
    }

    return 0;
}

void Service::playHaptic(uint32_t kind)
{
    using Vibro  = SDK::Message::RequestVibroPlay;
    using Buzzer = SDK::Message::RequestBuzzerPlay;

    uint8_t effect = Vibro::Effect::SHARP_CLICK_100;   // STIMULUS: crisp tick
    uint32_t beepMs = 0;
    if (kind == static_cast<uint32_t>(CustomMessage::HapticKind::LAPSE)) {
        effect = Vibro::Effect::ALERT_750MS_100;
        beepMs = 300;
    } else if (kind == static_cast<uint32_t>(CustomMessage::HapticKind::FALSE_START)) {
        effect = Vibro::Effect::DOUBLE_CLICK_100;
    } else if (kind == static_cast<uint32_t>(CustomMessage::HapticKind::DONE)) {
        effect = Vibro::Effect::STRONG_BUZZ_100;
        beepMs = 150;
    }

    if (auto* v = mKernel.comm.allocateMessage<Vibro>()) {
        v->notes[0].effect = effect;
        v->notesCount = 1;
        mKernel.comm.sendMessage(v);
        mKernel.comm.releaseMessage(v);
    }
    if (beepMs != 0) {
        if (auto* b = mKernel.comm.allocateMessage<Buzzer>()) {
            b->notes[0].volume = 66;
            b->notes[0].time   = beepMs;
            b->notesCount = 1;
            mKernel.comm.sendMessage(b);
            mKernel.comm.releaseMessage(b);
        }
    }
}
