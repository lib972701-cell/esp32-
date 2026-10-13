# ESP32-S3 定位串口链路

本轮增加现有邮件队列中的一次性定位任务，不开并发串口任务，不自动发送邮件，不修改DTU参数。电脑COM9实测LBS开启且不上报网络，但返回零坐标；有效基站定位尚未通过。固件编译与主机协议测试不能替代上板定位测试。

## 配置与接线

沿用README_邮件链路.md的板版本、电平、供电、GPIO17 TX/GPIO18 RX与无屏/摄像头核验。COM9目前是DTU直连电脑，不应误烧录为ESP32。

DTU当前LBS开启、免费、60秒更新、不主动网络上报，GPS和Wi-Fi关闭。GNSS查询需要事先在平台开启GPS，但仍保持不上报。仅有查询命令不会自动开启定位。串口应答以CRLF分帧，坐标按命令文档的十进制度处理，不猜测或自动转换度分；收到越界坐标直接判BAD_REPLY。

## 控制台

确认实际接线与3.3V电平后：

```text
mail_wiring_ok
location_get
location_status
location_get lbs show
location_get gnss
```

location_get默认LBS，排队成功仅表示已提交。稍后LOCATION_RESULT才是本次查询结果。无需邮箱凭据或TLS校时。LBS最大等待65秒，GNSS最大5秒；一次查询，不自动重试，不重启DTU。show明确允许把成功坐标打印到本地串口；默认仅打印状态，保存日志时不要使用show，避免泄露真实位置。

状态为VALID/NO_FIX/BAD_REPLY/DEVICE_ERROR/TIMEOUT/IO_ERROR。两坐标都为零即NO_FIX；GNSS扩展应答实际前缀config,gps,ok，定位标记为0时清除缓存坐标。错误或新查询开始会清除先前结果。邮件任务目前重启DTU，因此接受邮件任务时也清除上次定位结果。

VALID只表示应答中的坐标格式/范围及GNSS定位标记通过校验，不代表精度达标、数据绝对新鲜或实际地址正确。LBS没有定位时间字段，无法判断服务器位置年龄。location_status的query_age_ms仅表示本次查询完成距今多久。程序不据此声称导航可靠性。

## 业务接口

location_service.h提供location_request(source)和location_get_last(&result,&query_age_ms)。前者异步返回排队状态；后者查询最近完成结果，无结果/排队中返回ESP_ERR_INVALID_STATE。只有result.status为LOCATION_VALID时才能使用longitude/latitude。业务应规定可接受的查询年龄，定位无效时仍能手动发送不含位置的邮件，不无限等待定位。

普通location_get不自动发邮件，不启用后台定位。新增组合发送入口见README_定位邮件联动.md：只有明确执行mail_send_location才查询后发信。LBS是粗略备用位置，不能用于精确路口导航或楼层定位。输出为WGS84经纬度，街道地址需另做逆地理编码；地图转换取决于所用地图坐标系。

## 邮件兼容性

定位和邮件由同一个worker顺序执行，通过busy和队列互斥；邮件setup/time/clear/wiring也拒绝忙时操作。严禁TLS期间插入config命令，严禁把定位自动上报到SMTP网络通道。后台定位服务仍会使用4G资源；本轮已验证电脑串口LBS启用后的实际邮件并获用户确认实收，ESP32上的并发拒绝、超时、TLS与定位仍待硬件验证。

## 验证

scripts/test.ps1增加location_test.c主机测试：双零、无定位标记旧坐标、范围、方向、CRLF、额外字段、非数字/指数/NaN、应答源不匹配、错误码、超长数据。全部使用合成坐标。

scripts/build.ps1使用安装的ESP-IDF5.1.2编译。没有新增库或联网地图服务。硬件恢复位置：平台v7、DTU COM9/115200，先核查免费LBS服务器覆盖及实际模块方案，再接ESP32核验/烧录。
