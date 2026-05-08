"""
小智语音推送服务器

功能：
1. WebSocket 服务端，等待小智设备连接
2. 保持长连接，随时可推送语音
3. HTTP API 接口触发推送
4. MP3 转 Opus 格式推送

使用方法：
1. 启动服务器：python push_server.py
2. 设备连接后，访问 http://localhost:8080/push 触发推送
3. 或使用 POST /push 指定音频文件
"""

import asyncio
import json
import os
import struct
import logging
from pathlib import Path
from typing import Optional
from dataclasses import dataclass

import websockets
from aiohttp import web
import opuslib  # pip install opuslib
from pydub import AudioSegment  # pip install pydub

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger("PushServer")

# ============== 配置 ==============

# WebSocket 配置
WS_PORT = 8000
WS_PATH = "/push/v1"

# HTTP API 配置
HTTP_PORT = 8080

# Opus 编码配置（和小智设备匹配）
OPUS_SAMPLE_RATE = 16000  # 16kHz
OPUS_CHANNELS = 1         # 单声道
OPUS_FRAME_DURATION = 60  # 60ms 帧
OPUS_FRAME_SIZE = OPUS_SAMPLE_RATE * OPUS_FRAME_DURATION // 1000  # 960 samples

# 音频目录
MUSIC_DIR = Path(__file__).parent.parent / "music"

# ============== 数据结构 ==============

@dataclass
class DeviceConnection:
    """设备连接信息"""
    device_id: str
    websocket: websockets.WebSocketServerProtocol
    connected: bool = True

# ============== 全局状态 ==============

# 已连接的设备列表
connected_devices: dict[str, DeviceConnection] = {}

# ============== 音频处理 ==============

class AudioConverter:
    """音频转换器：MP3/WAV → Opus"""

    def __init__(self):
        self.encoder = opuslib.Encoder(
            OPUS_SAMPLE_RATE,
            OPUS_CHANNELS,
            opuslib.APPLICATION_AUDIO
        )
        self.encoder.bitrate = 24000
        self.encoder.complexity = 10

    def load_audio(self, audio_path: Path) -> AudioSegment:
        """加载音频文件"""
        logger.info(f"Loading audio: {audio_path}")

        # pydub 自动识别格式
        audio = AudioSegment.from_file(audio_path)

        # 转换为 16kHz 单声道
        audio = audio.set_frame_rate(OPUS_SAMPLE_RATE)
        audio = audio.set_channels(OPUS_CHANNELS)

        logger.info(f"Audio converted: {OPUS_SAMPLE_RATE}Hz, {OPUS_CHANNELS}ch, duration={len(audio)}ms")
        return audio

    def audio_to_opus_frames(self, audio: AudioSegment) -> list[bytes]:
        """将音频转换为 Opus 帧"""
        # 获取 PCM 数据
        pcm_data = audio.raw_data

        frames = []
        offset = 0
        frame_bytes = OPUS_FRAME_SIZE * 2  # 16-bit = 2 bytes per sample

        while offset + frame_bytes <= len(pcm_data):
            # 取一帧 PCM 数据
            pcm_frame = pcm_data[offset:offset + frame_bytes]

            # 编码为 Opus
            opus_frame = self.encoder.encode(pcm_frame, OPUS_FRAME_SIZE)
            frames.append(opus_frame)

            offset += frame_bytes

        # 处理剩余数据（不足一帧）
        if offset < len(pcm_data):
            remaining = pcm_data[offset:]
            # 补零到完整帧
            padded = remaining + b'\x00' * (frame_bytes - len(remaining))
            opus_frame = self.encoder.encode(padded, OPUS_FRAME_SIZE)
            frames.append(opus_frame)

        logger.info(f"Generated {len(frames)} Opus frames")
        return frames

    def file_to_opus_frames(self, audio_path: Path) -> list[bytes]:
        """从文件直接生成 Opus 帧"""
        audio = self.load_audio(audio_path)
        return self.audio_to_opus_frames(audio)

# ============== WebSocket 协议 ==============

class PushProtocol:
    """推送协议处理"""

    @staticmethod
    def make_hello_response() -> str:
        """服务器握手响应"""
        return json.dumps({
            "type": "hello",
            "status": "ok",
            "audio_params": {
                "format": "opus",
                "sample_rate": OPUS_SAMPLE_RATE,
                "channels": OPUS_CHANNELS,
                "frame_duration": OPUS_FRAME_DURATION
            }
        })

    @staticmethod
    def make_tts_start(session_id: str = "push_001") -> str:
        """TTS 开始消息"""
        return json.dumps({
            "type": "tts",
            "state": "start",
            "session_id": session_id,
            "text": "推送语音播放"
        })

    @staticmethod
    def make_tts_stop(session_id: str = "push_001") -> str:
        """TTS 结束消息"""
        return json.dumps({
            "type": "tts",
            "state": "stop",
            "session_id": session_id
        })

    @staticmethod
    def make_binary_frame(opus_data: bytes) -> bytes:
        """
        构建二进制音频帧

        帧格式（和小智协议 v3 一致）:
        [1字节类型][1字节保留][2字节长度][payload]

        类型 0x00 = Opus 音频
        """
        frame_type = 0x00  # Opus 音频
        reserved = 0x00
        payload_size = len(opus_data)

        header = struct.pack('>BBH', frame_type, reserved, payload_size)
        return header + opus_data

# ============== WebSocket 服务端 ==============

async def handle_device_connection(websocket: websockets.WebSocketServerProtocol):
    """处理设备连接"""
    device_id = websocket.request_headers.get("Device-Id", "unknown")

    logger.info(f"Device connected: {device_id}")

    # 记录连接
    device = DeviceConnection(device_id=device_id, websocket=websocket)
    connected_devices[device_id] = device

    try:
        # 发送握手响应
        await websocket.send(PushProtocol.make_hello_response())

        # 处理设备消息
        async for message in websocket:
            if isinstance(message, str):
                # JSON 文本消息
                try:
                    data = json.loads(message)
                    msg_type = data.get("type")
                    logger.info(f"Received from {device_id}: {msg_type}")

                    if msg_type == "hello":
                        # 设备握手确认
                        await websocket.send(PushProtocol.make_hello_response())
                    elif msg_type == "listen":
                        # 设备开始录音（反馈）
                        state = data.get("state")
                        if state == "start":
                            logger.info(f"Device {device_id} started recording feedback")
                        elif state == "stop":
                            logger.info(f"Device {device_id} stopped recording")
                    elif msg_type == "abort":
                        logger.info(f"Device {device_id} aborted")

                except json.JSONDecodeError:
                    logger.warning(f"Invalid JSON from {device_id}: {message}")

            elif isinstance(message, bytes):
                # 二进制音频数据（设备反馈录音）
                logger.info(f"Received audio feedback from {device_id}: {len(message)} bytes")
                # 这里可以保存录音或转发到其他服务
                # TODO: 处理设备反馈录音

    except websockets.exceptions.ConnectionClosed as e:
        logger.info(f"Device {device_id} disconnected: {e}")

    finally:
        # 清理连接
        if device_id in connected_devices:
            connected_devices[device_id].connected = False
            del connected_devices[device_id]
        logger.info(f"Device {device_id} removed from connected list")

# ============== 推送功能 ==============

async def push_audio_to_device(device_id: str, opus_frames: list[bytes], session_id: str = "push_001"):
    """推送音频到指定设备"""
    if device_id not in connected_devices:
        logger.warning(f"Device {device_id} not connected")
        return False

    device = connected_devices[device_id]
    if not device.connected:
        logger.warning(f"Device {device_id} connection inactive")
        return False

    websocket = device.websocket

    logger.info(f"Pushing {len(opus_frames)} frames to {device_id}")

    try:
        # 1. 发送 TTS start
        await websocket.send(PushProtocol.make_tts_start(session_id))
        await asyncio.sleep(0.1)  # 稍等设备准备

        # 2. 发送音频帧
        for i, frame in enumerate(opus_frames):
            binary_frame = PushProtocol.make_binary_frame(frame)
            await websocket.send(binary_frame)

            # 控制发送速率（模拟实时流）
            await asyncio.sleep(OPUS_FRAME_DURATION / 1000)

            if i % 50 == 0:
                logger.info(f"Sent {i}/{len(opus_frames)} frames")

        # 3. 发送 TTS stop
        await websocket.send(PushProtocol.make_tts_stop(session_id))

        logger.info(f"Push completed to {device_id}")
        return True

    except Exception as e:
        logger.error(f"Push failed to {device_id}: {e}")
        return False

async def push_audio_to_all(opus_frames: list[bytes], session_id: str = "push_001"):
    """推送音频到所有已连接设备"""
    if not connected_devices:
        logger.warning("No devices connected")
        return {"success": 0, "failed": 0, "message": "No devices connected"}

    results = {"success": 0, "failed": 0, "devices": []}

    for device_id in list(connected_devices.keys()):
        success = await push_audio_to_device(device_id, opus_frames, session_id)
        if success:
            results["success"] += 1
            results["devices"].append({"id": device_id, "status": "success"})
        else:
            results["failed"] += 1
            results["devices"].append({"id": device_id, "status": "failed"})

    return results

# ============== HTTP API ==============

async def handle_push_request(request: web.Request) -> web.Response:
    """
    HTTP API: 触发推送

    GET /push - 推送默认音频 (music/1.mp3)
    POST /push - 推送指定音频
        Body: {"audio": "filename.mp3"} 或 {"device_id": "xxx", "audio": "filename.mp3"}
    """
    converter = AudioConverter()

    # 获取参数
    if request.method == "POST":
        try:
            body = await request.json()
            audio_file = body.get("audio", "1.mp3")
            target_device = body.get("device_id")  # 可选，指定设备
        except:
            audio_file = "1.mp3"
            target_device = None
    else:
        audio_file = "1.mp3"
        target_device = None

    # 检查音频文件
    audio_path = MUSIC_DIR / audio_file
    if not audio_path.exists():
        return web.json_response({
            "error": f"Audio file not found: {audio_file}",
            "available": [f.name for f in MUSIC_DIR.glob("*") if f.is_file()]
        }, status=404)

    # 转换音频
    logger.info(f"Converting audio: {audio_path}")
    opus_frames = converter.file_to_opus_frames(audio_path)

    # 推送
    session_id = f"push_{int(asyncio.get_event_loop().time() * 1000)}"

    if target_device:
        # 推送到指定设备
        success = await push_audio_to_device(target_device, opus_frames, session_id)
        result = {
            "target": target_device,
            "success": success,
            "frames": len(opus_frames),
            "duration_ms": len(opus_frames) * OPUS_FRAME_DURATION
        }
    else:
        # 推送到所有设备
        result = await push_audio_to_all(opus_frames, session_id)
        result["frames"] = len(opus_frames)
        result["duration_ms"] = len(opus_frames) * OPUS_FRAME_DURATION

    return web.json_response(result)

async def handle_status_request(request: web.Request) -> web.Response:
    """
    HTTP API: 获取状态

    GET /status - 获取已连接设备列表
    """
    devices = []
    for device_id, device in connected_devices.items():
        devices.append({
            "id": device_id,
            "connected": device.connected
        })

    return web.json_response({
        "connected_devices": devices,
        "total": len(devices)
    })

async def handle_list_audio(request: web.Request) -> web.Response:
    """
    HTTP API: 获取可用音频列表

    GET /audio - 列出 music 目录下的音频文件
    """
    audio_files = []
    for f in MUSIC_DIR.glob("*"):
        if f.is_file() and f.suffix.lower() in ['.mp3', '.wav', '.ogg', '.flac', '.m4a']:
            audio_files.append({
                "name": f.name,
                "size": f.stat().st_size
            })

    return web.json_response({
        "audio_files": audio_files,
        "directory": str(MUSIC_DIR)
    })

# ============== 主程序 ==============

async def start_websocket_server():
    """启动 WebSocket 服务端"""
    logger.info(f"WebSocket server starting on port {WS_PORT}, path {WS_PATH}")

    async with websockets.serve(
        handle_device_connection,
        "0.0.0.0",
        WS_PORT,
        subprotocols=["xiaozhi-push"]
    ):
        logger.info(f"WebSocket server running at ws://0.0.0.0:{WS_PORT}{WS_PATH}")
        await asyncio.Future()  # 永久运行

async def start_http_server():
    """启动 HTTP API 服务端"""
    app = web.Application()

    # 注册路由
    app.router.add_get('/push', handle_push_request)
    app.router.add_post('/push', handle_push_request)
    app.router.add_get('/status', handle_status_request)
    app.router.add_get('/audio', handle_list_audio)

    runner = web.AppRunner(app)
    await runner.setup()

    site = web.TCPSite(runner, "0.0.0.0", HTTP_PORT)
    await site.start()

    logger.info(f"HTTP API server running at http://0.0.0.0:{HTTP_PORT}")

    # 打印 API 说明
    logger.info("=" * 50)
    logger.info("API Endpoints:")
    logger.info("  GET  /status  - 查看已连接设备")
    logger.info("  GET  /audio   - 查看可用音频文件")
    logger.info("  GET  /push    - 推送默认音频到所有设备")
    logger.info("  POST /push    - 推送指定音频")
    logger.info("            Body: {\"audio\": \"1.mp3\"}")
    logger.info("            Body: {\"device_id\": \"xxx\", \"audio\": \"1.mp3\"}")
    logger.info("=" * 50)

    await asyncio.Future()  # 永久运行

async def main():
    """主入口"""
    logger.info("=" * 50)
    logger.info("小智语音推送服务器启动")
    logger.info("=" * 50)

    # 检查 music 目录
    if not MUSIC_DIR.exists():
        logger.warning(f"Music directory not found: {MUSIC_DIR}")
        MUSIC_DIR.mkdir(parents=True)
        logger.info(f"Created music directory: {MUSIC_DIR}")

    # 检查依赖
    try:
        import opuslib
        import pydub
        logger.info("Dependencies OK: opuslib, pydub")
    except ImportError as e:
        logger.error(f"Missing dependency: {e}")
        logger.error("Please install: pip install opuslib pydub")
        return

    # 并行启动 WebSocket 和 HTTP 服务
    await asyncio.gather(
        start_websocket_server(),
        start_http_server()
    )

if __name__ == "__main__":
    asyncio.run(main())