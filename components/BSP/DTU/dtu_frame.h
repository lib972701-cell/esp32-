/*
 * 仅用于 ASCII 命令帧：接受 CR/LF 与可打印字符，不能过滤 TLS 二进制数据。
 */
#pragma once
#include <stdbool.h>
bool dtu_frame_byte_valid(unsigned char byte);
