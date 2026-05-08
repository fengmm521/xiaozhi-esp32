# 小智语音推送服务器

用于向小智 ESP32-S3 设备推送语音的服务器。

## 功能

- WebSocket 服务端，等待设备连接
- 保持长连接，随时可推送语音
- HTTP API 接口触发推送
- 支持 MP3/WAV 等格式转 Opus 推送

## 安装依赖

```bash
cd push_server
pip install -r requirements.txt
```

**注意**：pydub 需要 ffmpeg，请先安装：

- Windows: 下载 [ffmpeg](https://ffmpeg.org/download.html) 并添加到 PATH
- Linux: `apt install ffmpeg`
- Mac: `brew install ffmpeg`

## 启动服务器

```bash
cd push_server/src
python push_server.py
```

启动后：
- WebSocket 服务：`ws://0.0.0.0:8000/push/v1`
- HTTP API 服务：`http://0.0.0.0:8080`

## API 接口

### 查看已连接设备

```
GET http://localhost:8080/status
```

响应：
```json
{
  "connected_devices": [
    {"id": "device_mac_address", "connected": true}
  ],
  "total": 1
}
```

### 查看可用音频文件

```
GET http://localhost:8080/audio
```

### 推送音频

**推送默认音频 (music/1.mp3) 到所有设备：**
```
GET http://localhost:8080/push
```

**推送指定音频：**
```
POST http://localhost:8080/push
Content-Type: application/json

{
  "audio": "1.mp3"
}
```

**推送到指定设备：**
```
POST http://localhost:8080/push
Content-Type: application/json

{
  "device_id": "xx:xx:xx:xx:xx:xx",
  "audio": "1.mp3"
}
```

## 音频文件

将音频文件放在 `music/` 目录下，支持格式：
- MP3
- WAV
- OGG
- FLAC
- M4A

音频会自动转换为：
- 16kHz 采样率
- 单声道
- Opus 编码
- 60ms 帧时长

## 协议说明

### WebSocket 协议

设备连接时需要携带 Header：
- `Device-Id`: 设备 MAC 地址

握手流程：
```
设备连接 → 服务器发送 {"type":"hello","status":"ok"}
```

推送流程：
```
服务器发送 {"type":"tts","state":"start","session_id":"xxx"}
服务器发送 [二进制 Opus 帧]...
服务器发送 {"type":"tts","state":"stop","session_id":"xxx"}
```

二进制帧格式：
```
[1字节类型][1字节保留][2字节长度][Opus payload]
类型 0x00 = Opus 音频
```

## 测试

1. 启动服务器
2. 设备连接 WebSocket
3. 浏览器访问 `http://localhost:8080/push`
4. 设备应该开始播放 audio