# 定位后通过4G发送邮件

本轮在现有唯一UART worker中加入组合任务：查询一次位置→组织中文正文→重启DTU建立新邮件连接→TLS/SMTP发送一次。定位失败不阻塞邮件，正文明确标为无有效位置，不使用双零或旧位置。发送结果UNKNOWN_NO_RETRY时先查收件箱，程序不自动重发。

## 手动测试

沿用README_邮件链路.md接线、电平核验、mail_time准确UTC校时、mail_wiring_ok及mail_setup隐藏凭据配置。scripts/configure_mail.py默认只完成配置，可在退出释放ESP32端口后打开串口终端执行：

```text
mail_send_location
mail_status
mail_send_location gnss
```

第一条默认LBS。不要连续执行两条发送命令，等待上一次MAIL_RESULT再操作。GNSS需要平台预先开启GPS；程序不会改参数。命令会向配置的收件邮箱发送本次查询得到的位置，标题明确为测试、不是求助。定位查询与TLS互斥，其他定位/发送请求忙时被拒绝。

正文包含LBS/GNSS来源、有效时的WGS84十进制度经纬度，以及查询完成Unix时间（UTC）。LBS明确标为基站估算位置；查询完成时间不代表定位数据产生时间，也不能保证位置精度。无效时包含NO_FIX/超时/错误等说明，不输出坐标。未做GCJ02/BD09转换、街道地址解析或后台连续追踪。

目前DTU免费LBS仍返回无效位置，预期组合邮件内容为无有效位置。硬件ESP32上的组合邮件尚未实测，不把电脑串口邮件实收推定为本固件通过。

## 业务接入

mail_service.h：

```c
mail_request_with_location("位置通知", "业务说明", LOCATION_LBS);
// 排队成功只代表接受任务，稍后读取结果。
mail_result_t result;
if (mail_get_last_result(&result) == ESP_OK) {
    // MAIL_ACCEPTED只代表SMTP DATA接受，实际收件仍需验证。
}
```

subject<=45 UTF-8字节，base_body<=400字节，组合正文<=768字节。接口不自动发送跌倒事件，不持久化凭据，断电需要重新配置。新邮件任务接受后，结果变为NONE_OR_PENDING，完成后才可读取；原mail_request_text和mail_request_test保留。

组合任务不会把位置作为独立location_status缓存保留，因为原邮件链路随后重启DTU；仅把本次查询快照写入该封正文。坐标不写默认日志，job与临时缓冲用完清零。

## 验证和限制

scripts/test.ps1包含合成位置的正文测试：有效位置、各失败状态、越界/NaN/双零、长度限制、来源及时间标注，不使用用户真实坐标。原定位协议、邮件协议与Python配置助手测试继续运行。scripts/build.ps1编译ESP-IDF5.1.2。

串口占用总时间包含LBS最多65秒（GNSS5秒）、DTU重启等待20秒、TLS握手最多60秒、SMTP会话最多120秒，及少量有界发送开销。适用于手动演示，未做紧急事件延迟优化；不能宣称实时求助性能。后台LBS更新仍耗流量，当前60秒周期未在本轮修改。
