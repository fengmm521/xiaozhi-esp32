# 小智 ESP32 推送通道开发日志

## 项目概述

为小智 ESP32-S3 设备添加推送通道功能，使服务器可以主动向设备推送语音播放。

---

## 2024-05-08 工作记录

### 完成的功能

#### 1. 推送服务器 (Python)

位置：`push_server/` 目录

- `src/push_server.py` - WebSocket 推送服务器
- `src/test_page.html` - 测试页面
- `requirements.txt` - Python 依赖
- `music/1.mp3` - 测试音频文件

功能：
- WebSocket 服务端监听设备连接（端口 8000）
- HTTP API 接口触发推送（端口 8080）
- MP3 → Opus 格式转换（16kHz, 单声道, 60ms 帧）
- 支持推送到所有设备或指定设备

API 接口：
- `GET /status` - 查看已连接设备
- `GET /audio` - 查看可用音频文件
- `GET /push` - 推送默认音频
- `POST /push` - 推送指定音频

---

#### 2. 固件端推送通道

新增文件：
- `main/push_channel.h` - 推送通道头文件
- `main/push_channel.cc` - 推送通道实现
- `main/push_config_web.h` - 配置 Web 服务器头文件
- `main/push_config_web.cc` - 配置 Web 服务器实现

修改文件：
- `main/application.h` - 添加 push_channel_ 成员
- `main/application.cc` - 集成推送通道逻辑
- `main/background_task.h` - 更新 Schedule 返回 bool
- `main/background_task.cc` - 更新实现
- `main/CMakeLists.txt` - 添加新文件编译
- `main/Kconfig.projbuild` - 添加配置选项

---

### 功能流程

```
┌─────────────────────────────────────────────────────────────────┐
│                      推送通道工作流程                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. 设备启动 → WiFi 连接成功                                     │
│                                                                 │
│  2. 检查 NVS 是否有推送服务器配置                                 │
│     ├─ 有配置 → 尝试连接推送服务器                                │
│     │     ├─ 成功 → 保持连接，等待推送                            │
│     │     ├─ 失败 → 重试（最多3次）                               │
│     │     └─ 3次失败 → 开启配置Web（端口 8081）                   │
│     │                                                           │
│     └─ 无配置 → 开启配置Web（端口 8081）                          │
│                                                                 │
│  3. 用户通过浏览器配置推送服务器地址                              │
│     http://设备IP:8081                                           │
│                                                                 │
│  4. 配置保存到 NVS，设备连接推送服务器                            │
│                                                                 │
│  5. 推送服务器随时可以向设备推送语音                              │
│                                                                 │
│  6. 唤醒词触发时：打断推送 → 切换到原有对话模式                    │
│                                                                 │
│  7. 对话结束后：恢复推送通道连接                                  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

### 补丁文件

位置：`patches_for_1.7.6/` 目录

用于给官方 1.7.6 版本添加推送功能。

使用方法：
```bash
cd xiaozhi-esp32-1.7.6
cp patches_for_1.7.6/main/*.h main/
cp patches_for_1.7.6/main/*.cc main/
cp patches_for_1.7.6/main/CMakeLists.txt main/
cp patches_for_1.7.6/main/Kconfig.projbuild main/
```

---

### 编译问题修复记录

| 问题 | 解决方案 |
|------|----------|
| `WebSocket::Disconnect()` 不存在 | 改为 `websocket_->Close()` |
| `OpusDecoder::Decode()` 参数类型错误 | 使用 `std::move(opus_copy)` |
| `HTTPD_REQ_RECV_ERR_TIMEOUT` 不存在 | 改为 `HTTPD_SOCK_ERR_TIMEOUT` |
| `BackgroundTask::Schedule()` 返回 void | 更新为返回 bool |
| 未使用变量 warning | 删除未使用变量 |

---

### 编译环境

- Linux 编译服务器：`fengmm521@192.168.88.251`
- 项目路径：`/home/fengmm521/hard0/esp32/esp32c3_micropython/xiaozhi-esp32-1.7.6`
- 通过 SSH/SCP 操作远程服务器

---

## 待测试事项（明天）

1. 烧录固件到设备
2. 设备启动后检查配置Web是否正常启动
3. 通过配置页面设置推送服务器地址
4. 启动推送服务器测试
5. 测试语音推送功能
6. 测试唤醒词打断推送功能
7. 测试对话结束后恢复推送

---

## 可能需要的后续改进

1. 添加设备反馈录音功能（设备录音发回服务器）
2. 添加推送服务器的心跳检测
3. 添加更多的错误处理和日志
4. 支持多个推送服务器配置
5. 添加 OTA 更新推送功能

---

## 文件快速索引

### 推送服务器
- `push_server/src/push_server.py` - 主程序
- `push_server/README.md` - 使用说明

### 固件端
- `main/push_channel.h` - 推送通道类定义
- `main/push_channel.cc` - 推送通道实现
- `main/push_config_web.h` - 配置Web类定义  
- `main/push_config_web.cc` - 配置Web实现

### 补丁
- `patches_for_1.7.6/README.txt` - 安装说明
- `patches_for_1.7.6/main/` - 所有补丁文件