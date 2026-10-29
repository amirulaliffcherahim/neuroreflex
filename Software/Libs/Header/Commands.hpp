#pragma once

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

#include <cstdint>

#include "Readiness.hpp"

#pragma pack(push, 4)

namespace CustomMessage {

// GUI --> Service: only the Service may drive the buzzer and vibro motor.
constexpr SDK::MessageType::Type HAPTIC = 0x00000001;
// GUI --> Service: send me the saved history.
constexpr SDK::MessageType::Type GET_HISTORY = 0x00000002;
// GUI --> Service: one finished test; append it and save.
constexpr SDK::MessageType::Type ADD_RESULT = 0x00000003;
// Service --> GUI: the saved history (reply to GET_HISTORY and ADD_RESULT).
constexpr SDK::MessageType::Type HISTORY = 0x00000004;

enum class HapticKind : uint32_t { STIMULUS = 0, LAPSE = 1, FALSE_START = 2, DONE = 3 };

struct Haptic : public SDK::MessageBase {
    uint32_t kind;

    Haptic() : SDK::MessageBase(HAPTIC), kind(0) {}
    explicit Haptic(HapticKind k) : Haptic() { kind = static_cast<uint32_t>(k); }
};

struct GetHistory : public SDK::MessageBase {
    GetHistory() : SDK::MessageBase(GET_HISTORY) {}
};

struct AddResult : public SDK::MessageBase {
    Rd::Entry entry;

    AddResult() : SDK::MessageBase(ADD_RESULT), entry() {}
    explicit AddResult(const Rd::Entry& e) : AddResult() { entry = e; }
};

struct History : public SDK::MessageBase {
    Rd::History history;

    History() : SDK::MessageBase(HISTORY), history() {}
    explicit History(const Rd::History& h) : History() { history = h; }
};

// The kernel's message pools stop at 256 bytes.
static_assert(sizeof(History) <= 256, "History message must fit the kernel's 256-byte pool");

} // namespace CustomMessage

#pragma pack(pop)
