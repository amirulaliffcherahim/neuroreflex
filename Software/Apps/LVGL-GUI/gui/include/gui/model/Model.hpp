#ifndef MODEL_HPP
#define MODEL_HPP

#include <cstdint>

#include "SDK/Interfaces/ICustomMessageHandler.hpp"
#include "SDK/Interfaces/IGuiLifeCycleCallback.hpp"
#include "SDK/Kernel/Kernel.hpp"

#include "Readiness.hpp"

class ModelListener;

class Model : public SDK::Interface::IGuiLifeCycleCallback,
              public SDK::Interface::ICustomMessageHandler
{
public:
    Model();

    void bind(ModelListener* listener) { modelListener = listener; }

    void     exitApp();
    uint32_t nowMs() const { return mKernel.sys.getTimeMs(); }

    /// Ask the Service to play a cue (kind = CustomMessage::HapticKind).
    void haptic(uint32_t kind);

    /// Ask the Service for the saved history (answered via onHistory).
    void requestHistory();
    /// Hand a finished test to the Service to append and save.
    void saveResult(const Rd::Entry& e);

protected:
    ModelListener*     modelListener;
    const SDK::Kernel& mKernel;

    void onStart() override;
    void onResume() override;
    void onSuspend() override;
    void onStop() override;

    bool customMessageHandler(SDK::MessageBase* msg) override;
};

#endif
