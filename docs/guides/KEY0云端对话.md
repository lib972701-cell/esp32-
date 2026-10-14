# KEY0云端语音对话

2026-10-14视觉接入后，KEY0经统一前台调度启动；启用视觉UART时先等MaixCAM2回到IDLE，KEY1/2选择视觉会先关闭对话。见[视觉与前台模式](视觉与前台模式.md)。下方1807120字节固件实机记录属于此前版本，不能代替新增互斥功能的硬件验证。

当前集成版通过Wi-Fi连接小智云端。摔倒检测、DTU定位与邮件业务独立运行。

## 使用

1. 保持已配置的2.4GHz Wi-Fi/手机热点开启，等待串口输出 `KEY0 ready`。
2. 短按KEY0并松开开启对话；回答结束后继续监听。
3. 按住KEY0约2秒退出录音、播放和会话。松开不会重启，再次短按可重新开启。

KEY0是XL9555的P1_7，不是GPIO0上的BOOT。开机不会自动开始对话；上电按住KEY0或I2C读取失败会抑制误触发。摔倒检测开机即启动，不与按键绑定。

Wi-Fi配置缺失或连接失败时，连接日志给出的 `Xiaozhi` 配网热点设置网络；初次设备激活信息通过串口日志提供。串口实际端口需枚举，本轮COM8用于烧录、COM9用于UART0日志。

## 硬件接口

| 功能 | 配置 |
|---|---|
| KEY0 | XL9555地址0x20，输入寄存器0x01 bit7，低有效 |
| 板载音频与按键 | I2C0，SDA GPIO41，SCL GPIO42 |
| ES8388 I2S | MCLK3、BCLK46、WS9、输出10、输入14 |
| 功放 | XL9555 P0_2置0使能 |
| 当前MPU6050 | I2C1，GPIO4/5、0x68、400kHz、50ms传输超时 |
| DTU | UART1，TX GPIO17、RX GPIO18，115200 8N1 |

接口依据DNESP32S3 V1.0手册及官方音频例程核对；新I2C适配位于 `components/BSP/I2C/idf5`。

## 构建与代码

从工程根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test.ps1
powershell -ExecutionPolicy Bypass -File scripts/build-cloud.ps1
```

ESP-IDF5.4使用 `sdkconfig.cloud`、`sdkconfig.cloud.defaults`，固件在 `build-cloud/main.bin`，应用偏移 `0x10000`。保持原 `partitions-16MiB.csv`，不增加资源分区、不自动擦NVS、不自动升级固件。8MB Octal PSRAM已启用，Opus任务保留26KB栈。

源码入口是 `CloudAI/control/cloud_ai.cc`，会话管理在 `control/application.cc`，板载硬件在 `board`，音频队列在 `audio`，云端协议与服务发现/激活在 `network`，设置和设备信息在 `support`。详见[工程分层](../architecture/工程分层.md)。

只引入5个固定音频/网络组件：Wi-Fi连接、esp-ml307中的Wi-Fi网络接口、Opus、Opus包装及esp_codec_dev。没有屏幕、摄像头、LVGL、字体或图像模块。早期过度集成副本已归档到工程外 `C:/Users/workspace/Desktop/sample_project_cloud_history_20261014/`。

## 验证状态

2026-10-14实机日志确认短按开启、多轮采播、长按退出和再次短按恢复。未按键已启动MPU6050检测，对话期间模拟摔倒事件进入邮件队列，event2第二次尝试 `DATA_ACCEPTED`；用户确认“收到了，没问题”。当时已上板固件1807120字节，原30个保护文件未变。

详细过程见[上机验证记录](../verification/hardware_cloud_20261014/上机验证记录.md)，原始日志在根目录 `verification/hardware_cloud_20261014`。目录整理后的固件需要与原上板版本区分，见[目录整理记录](../verification/organization_20261014/目录整理记录.md)。

长期稳定性、连接中退出与完整断网恢复待验证。日志出现偶发I2C NACK、UDP丢包，26KB Opus栈首轮解码余量约968字节；本轮手持演示不代表真人摔倒性能。
