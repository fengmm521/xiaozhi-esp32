#ifndef _PUSH_CONFIG_WEB_H_
#define _PUSH_CONFIG_WEB_H_

#include <sdkconfig.h>

#if CONFIG_PUSH_CHANNEL_ENABLED

#include <esp_http_server.h>
#include <string>

class PushChannel;

class PushConfigWeb {
public:
    PushConfigWeb(PushChannel* push_channel);
    ~PushConfigWeb();

    // 启动配置 Web 服务器
    void Start();

    // 停止配置 Web 服务器
    void Stop();

    // 是否正在运行
    bool IsRunning() const { return server_ != nullptr; }

private:
    PushChannel* push_channel_;
    httpd_handle_t server_ = nullptr;

    // HTTP 处理函数
    static esp_err_t handle_root(httpd_req_t* req);
    static esp_err_t handle_config(httpd_req_t* req);
    static esp_err_t handle_status(httpd_req_t* req);
    static esp_err_t handle_test(httpd_req_t* req);
};

#endif // CONFIG_PUSH_CHANNEL_ENABLED

#endif // _PUSH_CONFIG_WEB_H_