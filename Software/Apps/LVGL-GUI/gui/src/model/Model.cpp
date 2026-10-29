/**
 ******************************************************************************
 * @file    Model.cpp
 * @brief   GUI-side state of Buttons and its link to the kernel.
 ******************************************************************************
 */

#include "gui/model/Model.hpp"
#include "gui/model/ModelListener.hpp"

#include "Commands.hpp"
#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Port/LVGL/LvglPort.hpp"

#define LOG_MODULE_PRX      "Model"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

Model::Model()
    : modelListener(nullptr)
    , mKernel(SDK::KernelProviderGUI::GetInstance().getKernel())
{
    // The port owns the kernel's lifecycle slot and forwards each event here.
    SDK::LVGL::Port::GetInstance().setAppLifeCycleCallback(this);
    SDK::LVGL::Port::GetInstance().setCustomMessageHandler(this);

#if defined(SIMULATOR)
    LOG_INFO("Application is running through simulator!\n");
    LOG_INFO("Keys: 1 = L1, 2 = L2, 3 = R1 (start / answer), 4 = R2 (back), Esc = close\n");
#endif
}

void Model::exitApp()
{
    LOG_INFO("Manually exiting the application\n");
    SDK::LVGL::Port::GetInstance().setAppLifeCycleCallback(nullptr);
    SDK::LVGL::Port::GetInstance().setCustomMessageHandler(nullptr);

    // No return on the watch: the kernel stops the GUI process. In the
    // simulator this makes the port's frame loop return.
    mKernel.sys.exit();
}

void Model::haptic(uint32_t kind)
{
    SDK::send_msg<CustomMessage::Haptic>(mKernel, static_cast<CustomMessage::HapticKind>(kind));
}

void Model::requestHistory()
{
    SDK::send_msg<CustomMessage::GetHistory>(mKernel);
}

void Model::saveResult(const Rd::Entry& e)
{
    SDK::send_msg<CustomMessage::AddResult>(mKernel, e);
}

bool Model::customMessageHandler(SDK::MessageBase* msg)
{
    if (msg->getType() == CustomMessage::HISTORY) {
        const Rd::History& h = static_cast<CustomMessage::History*>(msg)->history;
        // Trust boundary: the service read this from flash.
        if (h.count <= Rd::kMaxHistory && modelListener) {
            modelListener->onHistory(h);
        }
    }
    return true;
}

void Model::onStart()
{
    LOG_INFO("called\n");
}

void Model::onResume()
{
    LOG_INFO("called\n");
}

void Model::onSuspend()
{
    LOG_INFO("called\n");
}

void Model::onStop()
{
    LOG_INFO("called\n");
}
