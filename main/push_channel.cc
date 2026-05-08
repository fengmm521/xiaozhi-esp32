#include "push_channel.h"
#include "push_config_web.h"
#include "board.h"
#include "settings.h"
#include "system_info.h"
#include "audio_codec.h"

#include <esp_log.h>
#include <cJSON.h>
#include <arpa/inet.h>
#include <opus_decoder.h>

#define TAG "PushChannel"

// Opus 解码参数（和推送服务器匹配）
#define PUSH_OPUS_SAMPLE_RATE 16000
#define PUSH_OPUS_CHANNELS 1
#define PUSH_OPUS_FRAME_DURATION_MS 60

// 最大重连失败次数
#define MAX_RECONNECT_FAILURES 3

PushChannel::PushChannel() {
    event_group_ = xEventGroupCreate();

    // 从 NVS 读取推送服务器配置
    Settings settings("push_channel", false);
    server_url_ = settings.GetString("url");
    token_ = settings.GetString("token");

    ESP_LOGI(TAG, "Push server URL from NVS: %s", server_url_.empty() ? "(not set)" : server_url_.c_str());

    // 创建 Opus 解码器
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(PUSH_OPUS_SAMPLE_RATE, PUSH_OPUS_CHANNELS, PUSH_OPUS_FRAME_DURATION_MS);

    // 创建重连定时器
    esp_timer_create_args_t timer_args = {
        .callback = ReconnectTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "push_reconnect",
        .skip_unhandled_events = true
    };
    esp_timer_create(&timer_args, &reconnect_timer_);

    // 创建配置 Web 服务器
    config_web_ = std::make_unique<PushConfigWeb>(this);
}

PushChannel::~PushChannel() {
    Stop();

    if (reconnect_timer_ != nullptr) {
        esp_timer_stop(reconnect_timer_);
        esp_timer_delete(reconnect_timer_);
    }

    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
    }
}

void PushChannel::Start() {
    // 检查是否有服务器配置
    if (server_url_.empty()) {
        ESP_LOGI(TAG, "No push server configured, starting config web");
        StartConfigWeb();
        SetState(kPushChannelWaitingConfig);
        return;
    }

    // 尝试连接
    SetState(kPushChannelConnecting);

    if (!Connect()) {
        reconnect_failures_++;
        ESP_LOGW(TAG, "Connection failed (attempt %d/%d)", reconnect_failures_, MAX_RECONNECT_FAILURES);

        if (reconnect_failures_ >= MAX_RECONNECT_FAILURES) {
            ESP_LOGI(TAG, "Max failures reached, starting config web");
            StartConfigWeb();
            SetState(kPushChannelWaitingConfig);
            return;
        }

        // 启动重连定时器
        SetState(kPushChannelDisconnected);
        StartReconnectTimer();
        return;
    }

    // 连接成功，重置失败计数
    reconnect_failures_ = 0;
    SetState(kPushChannelConnected);
    StopConfigWeb();  // 停止配置服务器

    ESP_LOGI(TAG, "PushChannel started successfully");
}

void PushChannel::Stop() {
    ESP_LOGI(TAG, "Stopping PushChannel...");

    StopReconnectTimer();
    StopAudioPlayTask();
    Disconnect();
    StopConfigWeb();

    SetState(kPushChannelDisconnected);
}

void PushChannel::Resume() {
    if (state_ == kPushChannelDisconnected || state_ == kPushChannelWaitingConfig) {
        ESP_LOGI(TAG, "Resuming PushChannel...");
        Start();
    } else if (state_ == kPushChannelConnected) {
        ESP_LOGI(TAG, "PushChannel already connected, no need to resume");
    }
}

void PushChannel::Abort() {
    if (state_ == kPushChannelPlaying) {
        ESP_LOGI(TAG, "Aborting playback...");
        StopAudioPlayTask();

        // 清空音频队列
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            audio_queue_.clear();
        }

        SetState(kPushChannelConnected);
    }
}

void PushChannel::SetState(PushChannelState state) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != state) {
        state_ = state;
        ESP_LOGI(TAG, "State changed to: %d", state);

        if (on_state_changed_) {
            on_state_changed_(state);
        }
    }
}

void PushChannel::SetServerUrl(const std::string& url, const std::string& token) {
    // 保存到 NVS
    Settings settings("push_channel", true);
    settings.SetString("url", url);
    settings.SetString("token", token);

    server_url_ = url;
    token_ = token;

    ESP_LOGI(TAG, "Push server URL saved: %s", url.c_str());

    // 重置失败计数
    reconnect_failures_ = 0;

    // 尝试连接
    StopConfigWeb();
    Start();
}

bool PushChannel::Connect() {
    auto& board = Board::GetInstance();
    websocket_ = board.CreateWebSocket();

    // 设置认证头
    if (!token_.empty()) {
        std::string auth = token_;
        if (auth.find(" ") == std::string::npos) {
            auth = "Bearer " + auth;
        }
        websocket_->SetHeader("Authorization", auth.c_str());
    }

    // 设置设备标识头
    websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    websocket_->SetHeader("Client-Id", board.GetUuid().c_str());
    websocket_->SetHeader("Channel-Type", "push");

    // 设置数据接收回调
    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        HandleIncomingData(data, len, binary);
    });

    // 设置断开回调
    websocket_->OnDisconnected([this]() {
        ESP_LOGW(TAG, "WebSocket disconnected");
        if (state_ == kPushChannelConnected || state_ == kPushChannelPlaying) {
            SetState(kPushChannelDisconnected);
            StopAudioPlayTask();

            // 尝试重连
            reconnect_failures_++;
            if (reconnect_failures_ >= MAX_RECONNECT_FAILURES) {
                StartConfigWeb();
                SetState(kPushChannelWaitingConfig);
            } else {
                StartReconnectTimer();
            }
        }
    });

    ESP_LOGI(TAG, "Connecting to %s...", server_url_.c_str());
    if (!websocket_->Connect(server_url_.c_str())) {
        ESP_LOGE(TAG, "Failed to connect to push server");
        if (websocket_ != nullptr) {
            delete websocket_;
            websocket_ = nullptr;
        }
        return false;
    }

    // 发送握手消息
    SendHello();

    // 等待服务器响应（3秒超时）
    EventBits_t bits = xEventGroupWaitBits(
        event_group_,
        PUSH_CHANNEL_HELLO_EVENT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(3000)
    );

    if (!(bits & PUSH_CHANNEL_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Timeout waiting for server hello");
        Disconnect();
        return false;
    }

    ESP_LOGI(TAG, "Connected to push server");
    return true;
}

void PushChannel::Disconnect() {
    if (websocket_ != nullptr) {
        websocket_->Close();
        delete websocket_;
        websocket_ = nullptr;
    }
}

void PushChannel::SendHello() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddStringToObject(root, "channel", "push");
    cJSON_AddStringToObject(root, "device_id", SystemInfo::GetMacAddress().c_str());
    cJSON_AddStringToObject(root, "client_id", Board::GetInstance().GetUuid().c_str());

    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", PUSH_OPUS_SAMPLE_RATE);
    cJSON_AddNumberToObject(audio_params, "channels", PUSH_OPUS_CHANNELS);
    cJSON_AddNumberToObject(audio_params, "frame_duration", PUSH_OPUS_FRAME_DURATION_MS);
    cJSON_AddItemToObject(root, "audio_params", audio_params);

    auto json_str = cJSON_PrintUnformatted(root);
    websocket_->Send(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Sent hello message");
}

void PushChannel::HandleIncomingData(const char* data, size_t len, bool binary) {
    if (binary) {
        // 二进制数据 = Opus 音频帧
        // 解析帧格式: [1字节类型][1字节保留][2字节长度][payload]

        if (len < 4) {
            ESP_LOGW(TAG, "Invalid binary frame size: %d", len);
            return;
        }

        uint8_t type = data[0];
        uint16_t payload_size = ntohs(*(uint16_t*)(data + 2));

        if (payload_size > len - 4) {
            ESP_LOGW(TAG, "Payload size mismatch");
            payload_size = len - 4;
        }

        if (type == 0) {  // 类型 0 = Opus 音频
            std::vector<uint8_t> opus_data(data + 4, data + 4 + payload_size);

            {
                std::lock_guard<std::mutex> lock(audio_mutex_);
                if (audio_queue_.size() < 100) {
                    audio_queue_.emplace_back(PushAudioPacket{std::move(opus_data)});
                    audio_cv_.notify_one();
                }
            }
        }
    } else {
        // JSON 文本数据
        HandleIncomingJson(data, len);
    }
}

void PushChannel::HandleIncomingJson(const char* data, size_t len) {
    auto root = cJSON_Parse(data);
    if (root == nullptr) {
        ESP_LOGW(TAG, "Invalid JSON");
        return;
    }

    auto type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type->valuestring, "hello") == 0) {
        auto status = cJSON_GetObjectItem(root, "status");
        if (cJSON_IsString(status) && strcmp(status->valuestring, "ok") == 0) {
            ESP_LOGI(TAG, "Server hello received");
            xEventGroupSetBits(event_group_, PUSH_CHANNEL_HELLO_EVENT);
        }
    }
    else if (strcmp(type->valuestring, "tts") == 0) {
        auto state = cJSON_GetObjectItem(root, "state");
        if (cJSON_IsString(state)) {
            if (strcmp(state->valuestring, "start") == 0) {
                ESP_LOGI(TAG, "TTS start received");
                SetState(kPushChannelPlaying);

                if (audio_play_task_ == nullptr) {
                    xTaskCreate(
                        [](void* arg) {
                            static_cast<PushChannel*>(arg)->AudioPlayLoop();
                        },
                        "push_audio",
                        4096 * 4,
                        this,
                        5,
                        &audio_play_task_
                    );
                }

                if (on_play_started_) {
                    on_play_started_();
                }
            }
            else if (strcmp(state->valuestring, "stop") == 0) {
                ESP_LOGI(TAG, "TTS stop received");

                {
                    std::unique_lock<std::mutex> lock(audio_mutex_);
                    audio_cv_.wait(lock, [this]() {
                        return audio_queue_.empty();
                    });
                }

                StopAudioPlayTask();
                SetState(kPushChannelConnected);

                if (on_play_finished_) {
                    on_play_finished_();
                }
            }
        }
    }

    cJSON_Delete(root);
}

void PushChannel::AudioPlayLoop() {
    ESP_LOGI(TAG, "Audio play task started");

    auto codec = Board::GetInstance().GetAudioCodec();

    while (state_ == kPushChannelPlaying || !audio_queue_.empty()) {
        PushAudioPacket packet;

        {
            std::unique_lock<std::mutex> lock(audio_mutex_);
            audio_cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                return !audio_queue_.empty() || state_ != kPushChannelPlaying;
            });

            if (audio_queue_.empty()) {
                if (state_ != kPushChannelPlaying) {
                    break;
                }
                continue;
            }

            packet = std::move(audio_queue_.front());
            audio_queue_.pop_front();
        }

        DecodeAndPlayAudio(packet.payload);
    }

    ESP_LOGI(TAG, "Audio play task ended");
    audio_play_task_ = nullptr;
    vTaskDelete(nullptr);
}

void PushChannel::DecodeAndPlayAudio(const std::vector<uint8_t>& opus_data) {
    std::vector<int16_t> pcm_data;
    std::vector<uint8_t> opus_copy = opus_data;  // 复制一份用于 move
    if (!opus_decoder_->Decode(std::move(opus_copy), pcm_data)) {
        ESP_LOGW(TAG, "Opus decode failed");
        return;
    }

    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec == nullptr) {
        return;
    }

    // 输出到扬声器
    codec->OutputData(pcm_data);
}

void PushChannel::StopAudioPlayTask() {
    if (audio_play_task_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(100));

        if (audio_play_task_ != nullptr) {
            vTaskDelete(audio_play_task_);
            audio_play_task_ = nullptr;
        }
    }

    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        audio_queue_.clear();
        audio_cv_.notify_all();
    }
}

void PushChannel::StartReconnectTimer() {
    if (reconnect_timer_ != nullptr) {
        esp_timer_stop(reconnect_timer_);
        esp_timer_start_once(reconnect_timer_, 5 * 1000000);
        ESP_LOGI(TAG, "Reconnect timer started (5 seconds)");
    }
}

void PushChannel::StopReconnectTimer() {
    if (reconnect_timer_ != nullptr) {
        esp_timer_stop(reconnect_timer_);
    }
}

void PushChannel::ReconnectTimerCallback(void* arg) {
    PushChannel* channel = static_cast<PushChannel*>(arg);

    if (channel->state_ == kPushChannelDisconnected) {
        ESP_LOGI(TAG, "Attempting to reconnect...");
        channel->Start();
    }
}

void PushChannel::StartConfigWeb() {
    if (config_web_) {
        config_web_->Start();
    }
}

void PushChannel::StopConfigWeb() {
    if (config_web_) {
        config_web_->Stop();
    }
}

// ========== 回调设置 ==========

void PushChannel::OnStateChanged(std::function<void(PushChannelState)> callback) {
    on_state_changed_ = callback;
}

void PushChannel::OnPlayStarted(std::function<void()> callback) {
    on_play_started_ = callback;
}

void PushChannel::OnPlayFinished(std::function<void()> callback) {
    on_play_finished_ = callback;
}