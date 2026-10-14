import serial,time,pathlib
p=pathlib.Path(__file__).parent
end=time.time()+600
with (p/'serial.log').open('ab') as f:
 while time.time()<end:
  s=serial.Serial()
  s.port='COM8'; s.baudrate=115200; s.timeout=0.2; s.dtr=False; s.rts=False
  try:
   s.open(); print('CAPTURE_CONNECTED',flush=True)
   while time.time()<end:
    b=s.read(8192)
    if b:
     f.write(b); f.flush(); print(b.decode('utf-8','replace'),end='',flush=True)
  except serial.SerialException:
   print('PORT_RECONNECT',flush=True); time.sleep(1)
  finally:
   s.close()
