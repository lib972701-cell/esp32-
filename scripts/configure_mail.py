"""通过ESP32 USB_UART设置RAM凭据/校时；不在电脑上建立SMTP网络连接。

默认只配置。--probe不发邮件，--send明确发送一封。授权码不落盘，不写命令行。
"""
import argparse
import getpass
import re
import time
import serial


def strip_console_ansi(data):
    return re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", data)


def mail_result_value(data):
    match = re.search(rb"MAIL_RESULT=([A-Z_]+)[\r\n]", strip_console_ansi(data))
    return match.group(1).decode("ascii") if match else None


def wait_for(port, token, timeout=10):
    deadline = time.monotonic() + timeout
    received = bytearray()
    wanted = token.encode("ascii")
    while time.monotonic() < deadline:
        received.extend(port.read(max(1, min(port.in_waiting, 1024))))
        if wanted in received:
            return bytes(received)
        if len(received) > 32768:
            del received[:-16384]
    # Device bytes are never copied to exceptions: may contain console input.
    raise TimeoutError("设备未返回预期状态；检查端口、烧录、调试台占用或当前任务。")


def line(port, value):
    if any(c in value for c in "\r\n\x00"):
        raise ValueError("输入不允许包含换行或NUL")
    port.write(value.encode("ascii") + b"\r")
    port.flush()


def connect(port_name, factory=serial.Serial):
    # Set control lines before opening to avoid CH340 auto-reset/BOOT changes.
    port = factory(port=None, baudrate=115200, timeout=0.1, write_timeout=3)
    port.dtr = False
    port.rts = False
    port.port = port_name
    port.open()
    return port


def prepare_console(port):
    # REPL falls back to its plain terminal mode when no cursor-query answer is
    # available. An empty line is harmless in either terminal mode.
    port.write(b"\r")
    time.sleep(0.5)
    port.reset_input_buffer()
    line(port, "mail_status")
    wait_for(port, "MAIL_STATUS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="ESP32板载USB_UART端口，不是4G模块COM9")
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--probe", action="store_true")
    actions.add_argument("--send", action="store_true")
    parser.add_argument("--clear", action="store_true", help="仅清除RAM凭据")
    args = parser.parse_args()
    if args.clear and (args.probe or args.send):
        parser.error("--clear不能与--probe/--send并用")
    sender = recipient = authorization = None
    if not args.probe and not args.clear:
        sender = input("发件QQ/Foxmail邮箱：").strip()
        recipient = input("收件邮箱：").strip()
        if not re.fullmatch(r"[A-Za-z0-9._+-]+@(qq|foxmail)\.com", sender):
            raise ValueError("发件邮箱格式不合法")
        if not re.fullmatch(r"[A-Za-z0-9._+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}", recipient):
            raise ValueError("收件邮箱格式不合法")
        authorization = getpass.getpass("QQ SMTP授权码（隐藏输入）：")
        if not re.fullmatch(r"[A-Za-z0-9]{1,64}", authorization):
            raise ValueError("授权码应为不含空白的字母或数字")
    if not args.clear:
        answer = input("已按README接线、确认DTU TX为3.3V且没有屏/摄像头，请输入 YES：")
        if answer != "YES":
            print("尚未启用业务串口。")
            return
    port = connect(args.port)
    try:
        prepare_console(port)
        if args.clear:
            line(port, "mail_clear")
            wait_for(port, "MAIL_CLEARED")
            print("已清除ESP32 RAM凭据。")
            return
        line(port, "mail_time " + str(int(time.time())))
        wait_for(port, "MAIL_TIME_OK")
        line(port, "mail_wiring_ok")
        wait_for(port, "MAIL_WIRING=ESP_OK")
        if not args.probe:
            line(port, "mail_setup")
            wait_for(port, "INPUT_SENDER")
            line(port, sender)
            wait_for(port, "INPUT_RECIPIENT")
            line(port, recipient)
            wait_for(port, "INPUT_AUTHORIZATION_HIDDEN")
            line(port, authorization)
            authorization = None
            wait_for(port, "MAIL_CONFIG_READY_RAM_ONLY")
            print("配置已送入ESP32 RAM；重启后丢失。")
        if args.probe or args.send:
            line(port, "mail_probe" if args.probe else "mail_send_test")
            wait_for(port, "MAIL_REQUEST=ESP_OK")
            data = wait_for(port, "MAIL_RESULT=", timeout=220)
            # The token can straddle serial chunks, including its value.
            deadline = time.monotonic() + 3
            while mail_result_value(data) is None and time.monotonic() < deadline:
                data += port.read(max(1, min(port.in_waiting, 1024)))
            result = mail_result_value(data) or "UNREADABLE"
            print("ESP32结果：", result)
            if result == "DATA_ACCEPTED":
                print("QQ服务器已接受。请检查收件箱和垃圾箱，确认是否实际收到。")
            elif result == "UNKNOWN_NO_RETRY":
                print("提交结果未知，请先核对收件箱；程序没有自动重发。")
            elif result == "PROBE_OK":
                print("TLS和SMTP探测通过，未认证、未发送邮件。")
            else:
                raise RuntimeError("ESP32本次操作未成功；检查安全日志定位阶段。")
    finally:
        authorization = None
        port.close()


if __name__ == "__main__":
    try:
        main()
    except (ValueError, TimeoutError, RuntimeError, serial.SerialException) as exc:
        # Never print exception text originating in a serial library.
        print("操作失败：", str(exc) if not isinstance(exc, serial.SerialException) else "串口打开或通信失败")
        raise SystemExit(1)
