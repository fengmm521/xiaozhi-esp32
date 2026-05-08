#ifndef _PUSH_CHANNEL_H_
#define _PUSH_CHANNEL_H_

#include <sdkconfig.h>

#if CONFIG_PUSH_CHANNEL_ENABLED

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <list>

#include <opus_decoder.h>
#include <web_socket.h>

// 前向声明
class PushConfigWeb;

// 推送通道状态
enum PushChannelState {
    kPushChannelDisconnected,       // 断开连接
    kPushChannelConnecting,         // 正在连接
    kPushChannelConnected,          // 已连接，等待推送
    kPushChannelPlaying,            // 正在播放推送音频
    kPushChannelWaitingConfig,      // 等待配置（配置Web已启动）
};

// 推送音频包
struct PushAudioPacket {
    std::vector<uint8_t> payload;
};

class PushChannel {
public:
    PushChannel();
    ~PushChannel();

    // 启动推送通道
    void Start();

    // 停止推送通道
    void Stop();

    // 恢复推送通道
    void Resume();

    // 获取当前状态
    PushChannelState GetState() const { return state_; }

    // 是否正在播放
    bool IsPlaying() const { return state_ == kPushChannelPlaying; }

    // 是否已连接
    bool IsConnected() const { return state_ == kPushChannelConnected || state_ == kPushChannelPlaying; }

    // 是否等待配置
    bool IsWaitingConfig() const { return state_ == kPushChannelWaitingConfig; }

    // 设置服务器地址（来自配置Web）
    void SetServerUrl(const std::string& url, const std::string& token);

    // 获取当前服务器地址
    std::string GetServerUrl() const { return server_url_; }

    // 设置回调
    void OnStateChanged(std::function<void(PushChannelState)> callback);
    void OnPlayStarted(std::function<void()> callback);
    void OnPlayFinished(std::function<void()> callback);

    // 打断当前播放
    void Abort();

private:
    // WebSocket 连接
    WebSocket* websocket_ = nullptr;

    // 状态
    PushChannelState state_ = kPushChannelDisconnected;
    std::mutex state_mutex_;

    // 音频解码器
    std::unique_ptr<OpusDecoderWrapper> opus_decoder_;

    // 配置
    std::string server_url_;
    std::string token_;
    int reconnect_failures_ = 0;

    // 音频播放队列
    std::list<PushAudioPacket> audio_queue_;
    std::mutex audio_mutex_;
    std::condition_variable audio_cv_;
    TaskHandle_t audio_play_task_ = nullptr;

    // 配置 Web 服务器
    std::unique_ptr<PushConfigWeb> config_web_;

    // 回调
    std::function<void(PushChannelState)> on_state_changed_;
    std::function<void()> on_play_started_;
    std::function<void()> on_play_finished_;

    // 事件组
    EventGroupHandle_t event_group_ = nullptr;
    #define PUSH_CHANNEL_HELLO_EVENT (1 << 0)
    #define PUSH_CHANNEL_STOP_EVENT  (1 << 1)

    // 内部方法
    bool Connect();
    void Disconnect();
    void SetState(PushChannelState state);
    void HandleIncomingData(const char* data, size_t len, bool binary);
    void HandleIncomingJson(const char* data, size_t len);
    void DecodeAndPlayAudio(const std::vector<uint8_t>& opus_data);
    void AudioPlayLoop();
    void StopAudioPlayTask();
    void SendHello();

    // 定时重连
    esp_timer_handle_t reconnect_timer_ = nullptr;
    void StartReconnectTimer();
    void StopReconnectTimer();
    static void ReconnectTimerCallback(void* arg);

    // 配置 Web 控制
    void StartConfigWeb();
    void StopConfigWeb();
};

#endif // CONFIG_PUSH_CHANNEL_ENABLED

#endif // _PUSH_CHANNEL_H_