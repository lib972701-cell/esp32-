#include "application.h"
#include "board.h"
#include "cloud_bootstrap.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <cstring>
#include <ctime>
#include "esp_netif_sntp.h"
#include "esp_heap_caps.h"
static constexpr EventBits_t TASKS=1, AUDIO=2;
Application::Application():events_(xEventGroupCreate()) {}
bool Application::ConversationStopped() {
    std::lock_guard<std::mutex> lock(gate_mutex_); return conversation_gate_stopped(&gate_);
}
bool Application::Requested() { std::lock_guard<std::mutex> lock(gate_mutex_); return gate_.requested; }
bool Application::Current(uint32_t generation) { std::lock_guard<std::mutex> lock(gate_mutex_); return conversation_gate_current(&gate_,generation); }
uint32_t Application::Generation() { std::lock_guard<std::mutex> lock(gate_mutex_); return gate_.generation; }
void Application::Schedule(std::function<void()> fn) {
    { std::lock_guard<std::mutex> lock(tasks_mutex_); tasks_.push_back(std::move(fn)); }
    xEventGroupSetBits(events_,TASKS);
}
void Application::RequestConversation(bool start) {
    uint32_t generation;
    {
        std::lock_guard<std::mutex> lock(gate_mutex_);
        if(gate_.requested==start)return;
        if(start)generation=conversation_gate_start(&gate_);
        else { conversation_gate_stop(&gate_); generation=gate_.generation; }
    }
    if(!ready_.load()) {
        if(!start) {
            std::lock_guard<std::mutex> lock(gate_mutex_); conversation_gate_closed(&gate_);
        }
        ESP_LOGI("cloud_ai","KEY0 intent pending network initialization: %d",start); return;
    }
    if(!start)audio_.SetConversationEnabled(false);
    Schedule([this,start,generation]() {
        if(start)OpenConversation(generation);
        else if(!Requested())CloseConversation();
    });
}
void Application::SetState(DeviceState state) {
    {
        std::lock_guard<std::mutex> lock(gate_mutex_);
        if((state==kDeviceStateListening||state==kDeviceStateSpeaking)&&!gate_.requested)return;
        state_.store(state);
        audio_.EnableVoiceProcessing(state==kDeviceStateListening);
    }
    if(state==kDeviceStateListening)protocol_->SendStartListening(kListeningModeAutoStop);
    const char* names[]={"starting","idle","connecting","listening","speaking"};
    ESP_LOGI("cloud_ai","STATE: %s",names[state]);
}
void Application::OpenConversation(uint32_t generation) {
    if(!Current(generation)||!protocol_)return;
    if(!protocol_->IsAudioChannelOpened()) {
        SetState(kDeviceStateConnecting);
        if(!protocol_->OpenAudioChannel()) {
            if(Current(generation))RequestConversation(false);
            SetState(kDeviceStateIdle); return;
        }
    }
    bool current;
    {
        /* 与停止意图共用锁，防止连接完成恰好与切换并发时重新打开采播。 */
        std::lock_guard<std::mutex> lock(gate_mutex_);
        current=conversation_gate_current(&gate_,generation);
        if(current)audio_.SetConversationEnabled(true);
    }
    if(!current) { CloseConversation(); return; }
    SetState(kDeviceStateListening);
    ESP_LOGI("cloud_ai","CONVERSATION_STARTED");
}
void Application::CloseConversation() {
    audio_.EnableVoiceProcessing(false); audio_.SetConversationEnabled(false);
    if(protocol_) {
        if(protocol_->IsAudioChannelOpened())protocol_->SendAbortSpeaking(kAbortReasonNone);
        protocol_->CloseAudioChannel();
    }
    SetState(kDeviceStateIdle);
    {
        std::lock_guard<std::mutex> lock(gate_mutex_);
        conversation_gate_closed(&gate_);
    }
    ESP_LOGI("cloud_ai","CONVERSATION_EXITED; fall_sampling remains active");
}
void Application::Start() {
    auto& board=Board::GetInstance();
    auto* codec=board.GetAudioCodec();
    if(!events_||!codec) { ESP_LOGE("cloud_ai","Audio unavailable; fall_sampling continues"); return; }
    audio_.Initialize(codec);
    if(!codec->input_enabled()||!codec->output_enabled()) {
        codec->EnableInput(false);codec->EnableOutput(false);
        ESP_LOGE("cloud_ai","Audio start failed; fall_sampling continues");return;
    }
    if(audio_.Start()!=ESP_OK) { ESP_LOGE("cloud_ai","Audio task allocation failed; fall_sampling continues");return; }
    audio_.SetConversationEnabled(false);
    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available=[this]() { xEventGroupSetBits(events_,AUDIO); };
    audio_.SetCallbacks(callbacks);
    if(!board.StartNetwork())return;
    ESP_LOGI("cloud_ai","Before TLS: unix=%lld internal_free=%u external_free=%u",
             static_cast<long long>(time(nullptr)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    // TLS checks UTC certificate dates. Voice must not depend on the DTU clock.
    if(time(nullptr)<1704067200) {
        esp_sntp_config_t clock=ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
        esp_err_t clock_error=esp_netif_sntp_init(&clock);
        if(clock_error==ESP_OK) {
            clock_error=esp_netif_sntp_sync_wait(pdMS_TO_TICKS(20000));
            esp_netif_sntp_deinit();
        }
        ESP_LOGI("cloud_ai","Wi-Fi clock: %s unix=%lld",esp_err_to_name(clock_error),
                 static_cast<long long>(time(nullptr)));
    }
    // 该接口仅用于服务发现和设备激活，不升级固件。
    CloudBootstrap bootstrap;
    for(unsigned attempt=0;;attempt++) {
        if(bootstrap.CheckVersion()) {
            if(!bootstrap.HasActivationCode()&&!bootstrap.HasActivationChallenge())break;
            ESP_LOGW("cloud_ai","Activate device: %s (%s)",bootstrap.GetActivationCode().c_str(),bootstrap.GetActivationMessage().c_str());
            if(bootstrap.Activate()==ESP_OK)continue;
        }
        ESP_LOGW("cloud_ai","Service discovery pending, retry=%u; fall_sampling continues",attempt+1);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    if(bootstrap.HasWebsocketConfig()&&!bootstrap.HasMqttConfig())protocol_=std::make_unique<WebsocketProtocol>();
    else protocol_=std::make_unique<MqttProtocol>();
    protocol_->OnNetworkError([this](const std::string& message) {
        ESP_LOGW("cloud_ai","%s",message.c_str());RequestConversation(false);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        const auto generation=audio_.ConversationGeneration();
        if(Requested()&&state_.load()==kDeviceStateSpeaking)
            audio_.PushPacketToDecodeQueue(std::move(packet),false,generation);
    });
    protocol_->OnAudioChannelClosed([this]() {
        const auto generation=Generation();
        Schedule([this,generation]() {
            if(Generation()!=generation||protocol_->IsAudioChannelOpened())return;
            RequestConversation(false);SetState(kDeviceStateIdle);
        });
    });
    protocol_->OnIncomingJson([this](const cJSON* root) {
        if(!Requested())return;
        auto* type=cJSON_GetObjectItem(root,"type");if(!cJSON_IsString(type))return;
        const auto generation=Generation();
        if(strcmp(type->valuestring,"tts")==0) {
            auto* state=cJSON_GetObjectItem(root,"state");if(!cJSON_IsString(state))return;
            if(strcmp(state->valuestring,"start")==0)Schedule([this,generation]() { if(Current(generation))SetState(kDeviceStateSpeaking); });
            else if(strcmp(state->valuestring,"stop")==0)Schedule([this,generation]() {
                for(unsigned wait=0;wait<600&&Current(generation)&&!audio_.IsIdle();wait++)vTaskDelay(pdMS_TO_TICKS(10));
                if(Current(generation))SetState(kDeviceStateListening);
            });
        }
    });
    if(xTaskCreatePinnedToCore([](void* self) { static_cast<Application*>(self)->MainEventLoop(); },
                              "cloud_events",8192,this,3,nullptr,0)!=pdPASS) {
        ESP_LOGE("cloud_ai","Event task allocation failed; fall_sampling continues");return;
    }
    protocol_->Start(); state_.store(kDeviceStateIdle); ready_.store(true);
    const auto generation=Generation();Schedule([this,generation]() { OpenConversation(generation); });
    ESP_LOGI("cloud_ai","KEY0 ready: short=start, hold 2s=exit");
}
void Application::MainEventLoop() {
    for(;;) {
        auto bits=xEventGroupWaitBits(events_,TASKS|AUDIO,pdTRUE,pdFALSE,pdMS_TO_TICKS(1000));
        if(bits&TASKS) {
            std::deque<std::function<void()>> work;
            { std::lock_guard<std::mutex> lock(tasks_mutex_);work.swap(tasks_); }
            for(auto& fn:work)fn();
        }
        if(bits&AUDIO)while(auto packet=audio_.PopPacketFromSendQueue()) {
            if(Requested()&&state_.load()==kDeviceStateListening&&!protocol_->SendAudio(std::move(packet))) {
                RequestConversation(false);break;
            }
        }
        if(ready_.load()&&Requested()&&state_.load()!=kDeviceStateConnecting&&!protocol_->IsAudioChannelOpened())RequestConversation(false);
    }
}
