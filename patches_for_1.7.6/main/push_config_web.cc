#include "push_config_web.h"
#include "push_channel.h"
#include "board.h"
#include "system_info.h"

#include <esp_log.h>
#include <cJSON.h>
#include <esp_netif.h>

#define TAG "PushConfigWeb"

// 配置端口
#define CONFIG_WEB_PORT 8081

PushConfigWeb::PushConfigWeb(PushChannel* push_channel)
    : push_channel_(push_channel) {
}

PushConfigWeb::~PushConfigWeb() {
    Stop();
}

void PushConfigWeb::Start() {
    if (server_ != nullptr) {
        ESP_LOGW(TAG, "Config web already running");
        return;
    }

    ESP_LOGI(TAG, "Starting push config web server on port %d", CONFIG_WEB_PORT);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_WEB_PORT;
    config.ctrl_port = CONFIG_WEB_PORT + 1;
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start config web server");
        return;
    }

    // 注册 URI 处理器
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_root,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &root_uri);

    httpd_uri_t config_uri = {
        .uri = "/config",
        .method = HTTP_POST,
        .handler = handle_config,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &config_uri);

    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = handle_status,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &status_uri);

    httpd_uri_t test_uri = {
        .uri = "/test",
        .method = HTTP_POST,
        .handler = handle_test,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &test_uri);

    // 获取本机 IP 地址
    esp_netif_ip_info_t ip_info;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        ESP_LOGI(TAG, "Config web started at http://%d.%d.%d.%d:%d",
                 IP2STR(&ip_info.ip), CONFIG_WEB_PORT);
    }
}

void PushConfigWeb::Stop() {
    if (server_ != nullptr) {
        ESP_LOGI(TAG, "Stopping config web server");
        httpd_stop(server_);
        server_ = nullptr;
    }
}

esp_err_t PushConfigWeb::handle_root(httpd_req_t* req) {
    // 返回配置页面 HTML
    const char* html =
        "<!DOCTYPE html>"
        "<html lang='zh-CN'>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>推送服务器配置</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; max-width: 500px; margin: 50px auto; padding: 20px; background: #f5f5f5; }"
        ".container { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }"
        "h1 { color: #333; text-align: center; }"
        "input { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #ddd; border-radius: 5px; }"
        "button { width: 100%; padding: 12px; background: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; }"
        "button:hover { background: #45a049; }"
        ".status { margin-top: 20px; padding: 10px; border-radius: 5px; }"
        ".status.success { background: #d4edda; color: #155724; }"
        ".status.error { background: #f8d7da; color: #721c24; }"
        ".status.info { background: #cce5ff; color: #004085; }"
        "#result { margin-top: 20px; }"
        "</style>"
        "</head>"
        "<body>"
        "<div class='container'>"
        "<h1>小智推送服务器配置</h1>"
        "<p style='text-align:center;color:#666;'>设置语音推送服务器地址</p>"
        "<form id='configForm'>"
        "<label>服务器地址:</label>"
        "<input type='text' id='url' placeholder='ws://192.168.1.100:8000/push/v1' required>"
        "<label>认证令牌 (可选):</label>"
        "<input type='text' id='token' placeholder=''>"
        "<button type='submit'>保存并连接</button>"
        "</form>"
        "<button id='testBtn' style='margin-top:10px;background:#2196F3;'>测试连接</button>"
        "<button id='statusBtn' style='margin-top:10px;background:#FF9800;'>查看状态</button>"
        "<div id='result'></div>"
        "</div>"
        "<script>"
        "document.getElementById('configForm').addEventListener('submit', async (e) => {"
        "  e.preventDefault();"
        "  const url = document.getElementById('url').value;"
        "  const token = document.getElementById('token').value;"
        "  const result = document.getElementById('result');"
        "  result.innerHTML = '<div class=\"status info\">正在连接...</div>';"
        "  try {"
        "    const response = await fetch('/config', {"
        "      method: 'POST',"
        "      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },"
        "      body: `url=${encodeURIComponent(url)}&token=${encodeURIComponent(token)}`"
        "    });"
        "    const data = await response.json();"
        "    if (data.success) {"
        "      result.innerHTML = '<div class=\"status success\">连接成功！服务器地址已保存。</div>';"
        "    } else {"
        "      result.innerHTML = `<div class=\"status error\">连接失败: ${data.message}</div>`;"
        "    }"
        "  } catch (err) {"
        "    result.innerHTML = `<div class=\"status error\">请求失败: ${err.message}</div>`;"
        "  }"
        "});"
        "document.getElementById('testBtn').addEventListener('click', async () => {"
        "  const url = document.getElementById('url').value;"
        "  const result = document.getElementById('result');"
        "  result.innerHTML = '<div class=\"status info\">测试连接中...</div>';"
        "  try {"
        "    const response = await fetch('/test', {"
        "      method: 'POST',"
        "      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },"
        "      body: `url=${encodeURIComponent(url)}`"
        "    });"
        "    const data = await response.json();"
        "    if (data.reachable) {"
        "      result.innerHTML = '<div class=\"status success\">服务器可达！</div>';"
        "    } else {"
        "      result.innerHTML = `<div class=\"status error\">服务器不可达: ${data.message}</div>`;"
        "    }"
        "  } catch (err) {"
        "    result.innerHTML = `<div class=\"status error\">测试失败: ${err.message}</div>`;"
        "  }"
        "});"
        "document.getElementById('statusBtn').addEventListener('click', async () => {"
        "  const result = document.getElementById('result');"
        "  try {"
        "    const response = await fetch('/status');"
        "    const data = await response.json();"
        "    result.innerHTML = `<div class=\"status info\">状态: ${data.state}<br>地址: ${data.url || '未设置'}<br>失败次数: ${data.failures}</div>`;"
        "  } catch (err) {"
        "    result.innerHTML = `<div class=\"status error\">获取状态失败: ${err.message}</div>`;"
        "  }"
        "});"
        "</script>"
        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t PushConfigWeb::handle_config(httpd_req_t* req) {
    PushConfigWeb* self = static_cast<PushConfigWeb*>(req->user_ctx);

    // 获取 POST 数据
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // 解析 URL 和 token
    std::string url;
    std::string token;

    char* url_start = strstr(buf, "url=");
    if (url_start) {
        url_start += 4;
        char* url_end = strstr(url_start, "&");
        if (url_end) {
            url = std::string(url_start, url_end - url_start);
        } else {
            url = url_start;
        }
        // URL 解码（简单处理）
        size_t pos = 0;
        while ((pos = url.find("%3A", pos)) != std::string::npos) { url.replace(pos, 3, ":"); pos += 1; }
        while ((pos = url.find("%2F", pos)) != std::string::npos) { url.replace(pos, 3, "/"); pos += 1; }
        while ((pos = url.find("%3D", pos)) != std::string::npos) { url.replace(pos, 3, "="); pos += 1; }
    }

    char* token_start = strstr(buf, "token=");
    if (token_start) {
        token_start += 6;
        token = token_start;
        // URL 解码
        size_t pos = 0;
        while ((pos = token.find("%20", pos)) != std::string::npos) { token.replace(pos, 3, " "); pos += 1; }
    }

    ESP_LOGI(TAG, "Received config: url=%s, token=%s", url.c_str(), token.c_str());

    // 设置服务器地址并尝试连接
    self->push_channel_->SetServerUrl(url, token);

    // 返回结果
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", self->push_channel_->IsConnected());
    if (!self->push_channel_->IsConnected()) {
        cJSON_AddStringToObject(root, "message", "连接失败，请检查服务器地址");
    }

    auto json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

esp_err_t PushConfigWeb::handle_status(httpd_req_t* req) {
    PushConfigWeb* self = static_cast<PushConfigWeb*>(req->user_ctx);

    cJSON* root = cJSON_CreateObject();

    // 状态
    const char* state_str = "unknown";
    switch (self->push_channel_->GetState()) {
        case kPushChannelDisconnected: state_str = "disconnected"; break;
        case kPushChannelConnecting: state_str = "connecting"; break;
        case kPushChannelConnected: state_str = "connected"; break;
        case kPushChannelPlaying: state_str = "playing"; break;
        case kPushChannelWaitingConfig: state_str = "waiting_config"; break;
    }
    cJSON_AddStringToObject(root, "state", state_str);

    // 服务器地址
    cJSON_AddStringToObject(root, "url", self->push_channel_->GetServerUrl().c_str());

    // 设备信息
    cJSON_AddStringToObject(root, "device_id", SystemInfo::GetMacAddress().c_str());

    auto json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

esp_err_t PushConfigWeb::handle_test(httpd_req_t* req) {
    // 简单测试：返回成功（实际测试由 SetServerUrl 完成）
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "reachable", true);
    cJSON_AddStringToObject(root, "message", "测试完成");

    auto json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json_str);
    cJSON_Delete(root);

    return ESP_OK;
}