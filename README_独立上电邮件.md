# ESP32 + DTU 独立上电发邮件

2026-10-12验证：最终固件自动启动后约46秒取得DATA_ACCEPTED，只读监测电脑串口写入字节为0；用户确认拔掉两根电脑USB、独立供电并完全断电重上电后也收到新邮件。冷启动实收为用户确认，未保存真实位置或凭据日志。证据verification/autonomous_20261012/。

用户已确认每次上电发送一封定位测试邮件。运行时不需要电脑Python、串口指令或电脑提供时间。

启动顺序：App Service加载本地编译配置→唯一worker启用UART1 GPIO17/18→等待20秒DTU启动→最多90秒查询DTU网络时间→转换成UTC设置ESP32时钟→查询一次LBS→组织正文→重启DTU建立干净TCP连接→验证QQ SMTP TLS→登录并提交一封邮件。

无有效位置时仍发送明确说明；时间无效、配置无效、TLS校验失败时不发送。邮件提交后结果未知也不自动重试。下次设备重启会开始新的单封测试，可能再次收到一封；测试版不是事件去重或持久化投递系统。

## 私有配置

本机private/mail_config.h包含收发邮箱、SMTP授权码、启动开关和DTU时间时区。目录已列入.gitignore，示例为components/App/Boot/mail_config.example.h，不含真实凭据。私有配置仅由Boot模块读取，控制台与测试日志不打印值。

这是按用户要求的演示版：授权码编译进ESP32固件，断电后不丢失；并未启用Flash加密或加密NVS。mail_clear只清运行RAM，下次上电仍会从固件加载配置。修改或撤销授权码后需更新私有配置并重新编译/烧录。

首次创建private配置后需执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Reconfigure
```

没有私有配置时使用禁用自动发送的默认模板，原控制台功能仍保留。MAIL_BOOT_ENABLE=0可禁用上电自动邮件。不要将private目录、含凭据的固件/构建产物作为公开项目资料。

## DTU校时

厂商命令config,get,nettime返回年/月/日/时/分/秒/星期。本机YED_DTU4_2.0.3实测返回北京时间，故MAIL_BOOT_TIME_OFFSET_MINUTES=480，转换UTC后使用。dtu_time_sync命令用于诊断，但自动启动任务会自行校时，无需执行它。

代码检查完整CRLF、日期/闰年/时间范围，限制年份2025至2100。未把编译时间或电脑时间当作断电后的实际时间，未关闭TLS证书有效期校验。当前时间来源为DTU网络时间，未证明经过加密认证；更换模块/固件/时区后应重新核验，不把它称为安全认证的时间服务。

## 硬件

ESP32与DTU共地；GPIO17 TX接DTU RX，GPIO18 RX接DTU TX；3.3V TTL，无屏/摄像头。两边按各自手册要求供电。脱离电脑时ESP32需要独立电源，不能拔掉唯一USB电源后期待继续运行；不要把独立电源正极随意互连。

COM8用于烧录、COM9 UART0可只读观察日志。电脑只读观察并不参与发信，但完全拔掉电脑USB后的冷启动实收仍需另行验证。精确定位及长期可靠性不由一次邮件实收推定。
