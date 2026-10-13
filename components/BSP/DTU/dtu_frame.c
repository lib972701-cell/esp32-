/*
 * 纯 C 命令帧字节校验，供 UART 读行和宿主测试共用；不处理 TLS 密文字节。
 */
#include "dtu_frame.h"
/*
 * 只允许 CR、LF 和可打印 ASCII；拒绝 NUL，防止 C 字符串截断绕过帧检查。
 */
bool dtu_frame_byte_valid(unsigned char byte) {
    return byte=='\r' || byte=='\n' || (byte>=32 && byte<=126);
}
