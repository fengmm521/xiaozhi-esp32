小智 ESP32 1.7.6 推送通道补丁文件

说明：
这是为官方 1.7.6 版本添加推送通道功能的补丁文件。

安装方法：
-----------

1. 复制文件到 Linux 编译机器

将整个 patches_for_1.7.6 目录复制到 Linux 机器，然后：

cd /media/hard0/esp32/esp32c3_micropython/xiaozhi-esp32-1.7.6

# 复制 main 目录下的文件
cp patches_for_1.7.6/main/push_channel.h main/
cp patches_for_1.7.6/main/push_channel.cc main/
cp patches_for_1.7.6/main/push_config_web.h main/
cp patches_for_1.7.6/main/push_config_web.cc main/
cp patches_for_1.7.6/main/application.h main/
cp patches_for_1.7.6/main/application.cc main/
cp patches_for_1.7.6/main/background_task.h main/
cp patches_for_1.7.6/main/background_task.cc main/
cp patches_for_1.7.6/main/CMakeLists.txt main/
cp patches_for_1.7.6/main/Kconfig.projbuild main/

# 复制 push_server 目录（可选，用于推送服务器）
cp -r patches_for_1.7.6/push_server ./

2. 清理并重新编译

rm -rf build sdkconfig
idf.py set-target esp32s3

# 使用你原来的编译命令
python scripts/release.py ai_my_board_esp32s3N16R8

或者：
idf.py -DBOARD_TYPE_BREAD_COMPACT_ESP32_FENGMM521 -DBOARD_NAME=ai_my_board_esp32s3N16R8 build


修改的文件列表：
-----------

新增文件：
- main/push_channel.h      - 推送通道头文件
- main/push_channel.cc     - 推送通道实现
- main/push_config_web.h   - 配置 Web 服务器头文件
- main/push_config_web.cc  - 配置 Web 服务器实现

修改文件：
- main/application.h       - 添加推送通道成员
- main/application.cc      - 集成推送通道逻辑
- main/background_task.h   - 更新 Schedule 返回值（适配当前版本）
- main/background_task.cc  - 更新实现
- main/CMakeLists.txt      - 添加新文件编译
- main/Kconfig.projbuild   - 添加配置选项


推送服务器：
-----------

push_server 目录包含推送服务器的 Python 代码，可以部署在任何机器上。

启动推送服务器：
cd push_server/src
pip install -r ../requirements.txt
python push_server.py

访问测试页面：
http://推送服务器IP:8080


功能说明：
-----------

1. 设备启动后会检查 NVS 中是否有推送服务器配置
2. 如果没有配置或连接失败3次，会启动配置 Web（端口 8081）
3. 用户可以通过 http://设备IP:8081 配置推送服务器地址
4. 配置保存后，设备会自动连接推送服务器
5. 推送服务器可以随时向设备推送语音
6. 唤醒词触发时，会打断推送并切换到原有对话模式
7. 对话结束后，自动恢复推送通道连接


配置选项（idf.py menuconfig）：
-----------

Xiaozhi Assistant
  → Enable Push Channel          [Y]
  → Push Server URL             ws://192.168.1.100:8000/push/v1
  → Push Server Token           (可选)