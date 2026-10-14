# ESP32-S3 智能辅助固件

DNESP32S3 V1.0：KEY0控制Wi-Fi云端语音，MPU6050持续检测疑似摔倒，M100MG通过4G发送定位求助邮件。

KEY1控制外部MaixCAM2红绿灯，KEY2控制行人方位；三键短按进入、长按2秒退出对应模式，云端对话与视觉互斥，摔倒检测常驻。视觉先输出状态与UART0日志；UART2选TX6/RX16、默认禁用，IO7保留IMU INT，实际接线待确认。

- [文档目录](docs/README.md)：使用指南、代码结构、接线、验证记录。
- [KEY0云端对话](docs/guides/KEY0云端对话.md)：短按开启，长按约2秒退出。
- [视觉与前台模式](docs/guides/视觉与前台模式.md)：互斥、串口配置和状态查询。
- [工程分层](docs/architecture/工程分层.md)：源码目录与阅读顺序。

从工程根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test.ps1
powershell -ExecutionPolicy Bypass -File scripts/build-cloud.ps1
```

当前集成版使用ESP-IDF 5.4，输出 `build-cloud/main.bin`。`scripts/build.ps1` 保留ESP-IDF 5.1.2邮件/摔倒版。构建不等于烧录，应用偏移为 `0x10000`；保持原16MB分区与NVS。

项目运行源码位于 `main`、`components`，日常工具位于 `scripts`，测试位于 `tests`，原始验证数据位于 `verification`，说明与图示统一位于 `docs`。历史代码与一次性工具保留并标明用途。
