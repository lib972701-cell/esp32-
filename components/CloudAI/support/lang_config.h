#pragma once
// 仅保留云端协议错误文本；无图像、字体或资源分区。
namespace Lang {
inline constexpr const char* CODE = "zh-CN";
namespace Strings {
inline constexpr const char* SERVER_NOT_FOUND = "服务器配置尚未就绪";
inline constexpr const char* SERVER_NOT_CONNECTED = "无法连接语音服务器";
inline constexpr const char* SERVER_ERROR = "语音服务网络异常";
inline constexpr const char* SERVER_TIMEOUT = "语音服务器响应超时";
}
}
