@echo off
echo ========================================
echo   小智语音推送服务器启动
echo ========================================
echo.

cd /d %~dp0

echo 检查依赖...
pip show opuslib >nul 2>&1
if errorlevel 1 (
    echo 安装依赖...
    pip install -r requirements.txt
)

echo.
echo 启动服务器...
echo.
cd src
python push_server.py

pause