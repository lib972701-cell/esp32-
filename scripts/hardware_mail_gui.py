"""Local ESP32 hardware-test helper. No PC SMTP, credentials are never persisted."""
import json
import re
import threading
import time
from pathlib import Path
import tkinter as tk
from tkinter import messagebox
from configure_mail import connect, line, wait_for, prepare_console, strip_console_ansi

RESULT = Path(__file__).parents[1] / 'verification/hardware_20261012/send-result.json'
root = tk.Tk()
root.title('ESP32 4G location mail hardware test')
root.geometry('580x310')
tk.Label(root, text='ESP32 COM9 -> UART1 -> DTU -> QQ SMTP\nSend one location test email; no automatic retry.').pack(pady=10)
fields = []
for label, hidden in [('Sender QQ/Foxmail email', False), ('Recipient email', False), ('SMTP authorization (local only)', True)]:
    row = tk.Frame(root); row.pack(fill='x', padx=15, pady=4)
    tk.Label(row, text=label, width=29, anchor='w').pack(side='left')
    entry = tk.Entry(row, show='*' if hidden else '', width=36)
    entry.pack(side='left'); fields.append(entry)
status = tk.StringVar(value='Enter local credentials. Real location may appear in the email.')
tk.Label(root, textvariable=status, wraplength=550).pack(pady=10)

def complete_reply(port, data, pattern, timeout=3):
    end = time.monotonic()+timeout
    while time.monotonic()<end:
        match = re.search(pattern, strip_console_ansi(data))
        if match: return match, data
        data += port.read(max(1, min(port.in_waiting,1024)))
    raise TimeoutError('Incomplete reply')

def work(sender, recipient, authorization):
    result = {'transport':'esp32_uart1_4g', 'automatic_retry':False}
    try:
        with connect('COM9') as port:
            prepare_console(port)
            line(port,'mail_time '+str(int(time.time())));wait_for(port,'MAIL_TIME_OK')
            line(port,'mail_wiring_ok');wait_for(port,'MAIL_WIRING=ESP_OK')
            line(port,'mail_setup');wait_for(port,'INPUT_SENDER')
            line(port,sender);wait_for(port,'INPUT_RECIPIENT')
            line(port,recipient);wait_for(port,'INPUT_AUTHORIZATION_HIDDEN')
            line(port,authorization);authorization=None
            wait_for(port,'MAIL_CONFIG_READY_RAM_ONLY')
            line(port,'mail_send_location lbs')
            data=wait_for(port,'MAIL_REQUEST=')
            match,data=complete_reply(port,data,rb'MAIL_REQUEST=([A-Z_]+)[\r\n]')
            if match.group(1)!=b'ESP_OK': raise RuntimeError('Request rejected')
            data += wait_for(port,'MAIL_RESULT=',310)
            match,data=complete_reply(port,data,rb'MAIL_RESULT=([A-Z_]+)[\r\n]')
            result['status']=match.group(1).decode('ascii')
            location=re.search(rb'LOCATION_FOR_MAIL source=(LBS|GNSS) status=([A-Z_]+)',data)
            if location: result['location_source']=location.group(1).decode();result['location_status']=location.group(2).decode()
            result['tls_verified']=b'TLS_VERIFIED' in data
            # Erase RAM credentials once the transaction has ended.
            line(port,'mail_clear');wait_for(port,'MAIL_CLEARED')
            result['credentials_cleared']=True
    except Exception as exc:
        result['status']=result.get('status','UNCONFIRMED_CHECK_INBOX')
        result['failure_type']=type(exc).__name__
    finally:
        authorization=None
    result['time_utc_epoch']=int(time.time())
    RESULT.parent.mkdir(parents=True,exist_ok=True)
    RESULT.write_text(json.dumps(result,indent=2),encoding='utf-8')
    root.after(0,lambda:status.set('Result: '+result['status']+'. Check recipient inbox. No resend.'))

def send():
    sender,recipient,authorization=[x.get().strip() for x in fields]
    if not re.fullmatch(r'[A-Za-z0-9._+-]+@(qq|foxmail)\.com',sender) or not re.fullmatch(r'[A-Za-z0-9._+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}',recipient) or not re.fullmatch(r'[A-Za-z0-9]{1,64}',authorization):
        messagebox.showerror('Input','Check email addresses and SMTP authorization.');return
    button.config(state='disabled');fields[2].delete(0,'end')
    status.set('ESP32 is querying location and sending. May take several minutes. Keep this window open.')
    threading.Thread(target=work,args=(sender,recipient,authorization),daemon=True).start()

button=tk.Button(root,text='Send ONE ESP32 location test email',command=send)
button.pack(pady=5)
root.mainloop()
