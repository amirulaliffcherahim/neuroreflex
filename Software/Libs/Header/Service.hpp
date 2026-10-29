#ifndef __SERVICE_HPP__
#define __SERVICE_HPP__

#include "SDK/Kernel/KernelProviderService.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"

#include "Readiness.hpp"

class Service
{
public:
    Service(SDK::Kernel& kernel);

    virtual ~Service() = default;

    void run();

private:
    SDK::Kernel&             mKernel;
    bool                     mGUIStarted;

    void onStartGUI();
    void onStopGUI();
    void playHaptic(uint32_t kind);
    void loadHistory();
    void saveHistory();
    void sendHistory();

    Rd::History mHistory;
    bool        mHistoryLoaded = false;

    static uint32_t ParseVersion(const char* str);
};

#endif

