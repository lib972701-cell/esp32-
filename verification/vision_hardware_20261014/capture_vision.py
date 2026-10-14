"""UART0日志采集和测试命令通道，不触碰UART1/2或邮件发送命令。"""
from pathlib import Path
import json
import sys
import time
import serial

sys.stdout.reconfigure(encoding="utf-8",errors="replace")
base=Path(__file__).resolve().parent
commands=base/"commands.jsonl"
commands.touch(exist_ok=True)
offset=0
deadline=time.monotonic()+1800
with (base/"uart.log").open("ab") as log:
    port=serial.Serial(port=None,baudrate=115200,timeout=.1,write_timeout=2)
    port.port="COM9";port.dtr=False;port.rts=False
    port.open()
    print("CAPTURE_CONNECTED COM9",flush=True)
    tail=b""
    try:
        while time.monotonic()<deadline:
            data=port.read(8192)
            if data:
                log.write(data);log.flush()
                print(data.decode("utf-8","replace"),end="",flush=True)
                tail+=data
                if b"\x1b[6n" in tail:
                    port.write(b"\x1b[1;1R")
                tail=tail[-3:]
            with commands.open("r",encoding="utf-8") as f:
                f.seek(offset)
                for line in f:
                    entry=json.loads(line)
                    command=entry["command"]
                    if command not in {"", "activity_status", "mail_status",
                                       "activity_select idle", "activity_select light",
                                       "activity_select person", "activity_select cloud"}:
                        raise ValueError("Unsupported test command")
                    port.write((command+"\r").encode("ascii"))
                    print("TEST_COMMAND",entry.get("tag",""),command,flush=True)
                    time.sleep(.15)
                offset=f.tell()
    finally:
        port.close()
