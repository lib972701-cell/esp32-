#pragma once
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "audio_service.h"
#include "conversation_gate.h"
#include "protocol.h"
enum DeviceState { kDeviceStateStarting, kDeviceStateIdle, kDeviceStateConnecting,
                   kDeviceStateListening, kDeviceStateSpeaking };
class Application {
public:
    static Application& GetInstance() { static Application app; return app; }
    void Start();
    void RequestConversation(bool start);
    void Schedule(std::function<void()> callback);
    DeviceState GetDeviceState() const { return state_.load(); }
    bool ConversationStopped();
    bool IsReady() const { return ready_.load(); }
private:
    Application();
    void MainEventLoop();
    bool Requested();
    bool Current(uint32_t generation);
    uint32_t Generation();
    void OpenConversation(uint32_t generation);
    void CloseConversation();
    void SetState(DeviceState state);
    EventGroupHandle_t events_;
    std::atomic<DeviceState> state_{kDeviceStateStarting};
    std::atomic<bool> ready_{false};
    std::mutex gate_mutex_, tasks_mutex_;
    conversation_gate_t gate_{};
    std::deque<std::function<void()>> tasks_;
    std::unique_ptr<Protocol> protocol_;
    AudioService audio_;
};
