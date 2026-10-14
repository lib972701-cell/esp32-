from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import html
OUT=Path(__file__).resolve().parents[2]/'docs'/'images'
OUT.mkdir(parents=True,exist_ok=True)
FONT='C:/Windows/Fonts/msyh.ttc'
BOLD='C:/Windows/Fonts/msyhbd.ttc'
class Figure:
 def __init__(self,w,h,title,subtitle):
  self.w=w;self.h=h;self.im=Image.new('RGB',(w,h),'#f4f7fb');self.d=ImageDraw.Draw(self.im);self.svg=[]
  self.text(50,30,title,34,'#152b48',True);self.text(50,82,subtitle,19,'#53647b')
 def text(self,x,y,s,size=20,color='#20344e',bold=False):
  font=ImageFont.truetype(BOLD if bold else FONT,size)
  self.d.text((x,y),s,font=font,fill=color)
  self.svg.append(f'<text x="{x}" y="{y+size}" font-size="{size}" fill="{color}" font-weight="{700 if bold else 400}">{html.escape(s)}</text>')
 def box(self,x,y,w,h,title,lines,fill='#ffffff',stroke='#c8d5e5'):
  self.d.rounded_rectangle((x,y,x+w,y+h),radius=14,fill=fill,outline=stroke,width=2)
  self.svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="14" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
  self.text(x+18,y+14,title,22,'#152b48',True)
  for i,line in enumerate(lines):self.text(x+18,y+52+29*i,line,18)
 def arrow(self,points,color='#42688f',label=None,lx=0,ly=0):
  self.d.line(points,fill=color,width=3)
  x,y=points[-1];px,py=points[-2]
  import math
  a=math.atan2(y-py,x-px)
  p1=(x-12*math.cos(a-.45),y-12*math.sin(a-.45));p2=(x-12*math.cos(a+.45),y-12*math.sin(a+.45))
  self.d.polygon([(x,y),p1,p2],fill=color)
  coords=' '.join(f'{p[0]},{p[1]}' for p in points)
  self.svg.append(f'<polyline points="{coords}" fill="none" stroke="{color}" stroke-width="3" marker-end="url(#arrow)"/>')
  if label:self.text(lx,ly,label,17,color)
 def save(self,name):
  self.im.save(OUT/(name+'.png'))
  svg=f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" viewBox="0 0 {self.w} {self.h}"><defs><marker id="arrow" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0 0 L8 4 L0 8 Z" fill="#42688f"/></marker></defs><rect width="100%" height="100%" fill="#f4f7fb"/><g font-family="Microsoft YaHei, sans-serif">'+''.join(self.svg)+'</g></svg>'
  (OUT/(name+'.svg')).write_text(svg,encoding='utf-8')
f=Figure(1700,1350,'系统框图：ESP32-S3 + 4G DTU 定位邮件','依据当前工程源码 / 2026-10-12 · 蓝色区域为 ESP32 软件，右侧为外部设备与服务')
f.box(40,130,1190,1140,'ESP32-S3 / ESP-IDF / FreeRTOS',[],fill='#e9f1fc')
f.box(75,195,330,135,'main：启动入口',['app_service_start()','app_console_start()'])
f.box(445,195,360,135,'App / Boot：启动配置',['编译配置 → 收发邮箱与授权码','当前：上电单封 / 时区 UTC+8'])
f.box(845,195,350,135,'App / Console：调试入口',['UART0 REPL / 状态与手动命令','只调用 Service，不操作 UART1'])
f.box(250,395,775,165,'App / Service：业务调度中心',['容量 1 的工作队列 + busy 状态 + 互斥锁','唯一 dtu_worker：16 KiB 栈 / 优先级 5','串行执行：校时 → 查询定位 → 组正文 → 邮件传输'])
f.arrow([(240,330),(240,365),(400,365),(400,395)])
f.arrow([(625,330),(625,395)])
f.arrow([(1020,330),(1020,365),(880,365),(880,395)])
f.box(75,635,325,170,'Middlewares / TIME',['nettime 查询与日期校验','时区偏移 → UTC Unix 时间','设置系统时钟供 TLS / Date 使用'])
f.box(445,635,360,170,'Middlewares / LOCATION',['LBS / GNSS 查询与应答解析','状态、经纬度、耗时、错误码','异常 / 双零坐标 → 无有效位置'])
f.box(850,635,345,170,'App / Message',['location_mail_body()','基础正文 + 本次位置 / 失败原因','查询完成 UTC 时间；不复用旧坐标'])
f.arrow([(400,560),(400,595),(240,595),(240,635)])
f.arrow([(625,560),(625,635)])
f.arrow([(805,720),(850,720)])
f.box(445,885,750,180,'Middlewares / MAIL：端到端邮件协议',['mail_link：DTU 重启 / 通道检查 / MIME / mbedTLS','mail_protocol：SMTP 应答校验与 AUTH LOGIN / DATA','TLS 在 ESP32 执行；证书链 + 主机名 + 有效期校验'])
f.arrow([(1020,805),(1020,885)])
f.box(75,1110,1120,125,'BSP / DTU：UART1 驱动与帧完整性',['115200 / 8N1 / 无硬件流控 · GPIO17 TX → DTU RX · GPIO18 RX ← DTU TX','RX 32 KiB / TX 2 KiB / 事件队列 · ASCII 命令读行 + TLS 二进制收发'])
f.arrow([(240,805),(240,1110)])
f.arrow([(625,805),(625,845),(420,845),(420,1110)])
f.arrow([(820,1065),(820,1110)])
f.box(1280,195,370,160,'电脑（可选）',['UART0：调试命令 / 日志','烧录与调试阶段使用','独立运行不需要电脑提供时间'])
f.arrow([(1280,275),(1245,275),(1245,365),(1100,365),(1100,330)])
f.box(1280,470,370,200,'4G DTU 模块',['厂商固件：时间 / LBS / GNSS接口','TCP 通道 → smtp.qq.com:465','串口透明传输 TLS 字节','联网与定位能力依赖实际模块'])
f.arrow([(1195,1170),(1250,1170),(1250,590),(1280,590)],label='UART1 / 3.3 V TTL / 共地',lx=1280,ly=695)
f.box(1280,735,370,140,'运营商网络 / Internet',['4G 接入与 TCP 字节传输','LBS 所需服务依赖 DTU 实现'])
f.arrow([(1460,670),(1460,735)])
f.box(1280,940,370,140,'QQ SMTP 邮件服务器',['TLS 对端 / 授权码认证','DATA 最终 250 → 已接受提交'])
f.arrow([(1460,875),(1460,940)])
f.box(1280,1145,370,125,'收件人邮箱',['接收包含定位状态的测试邮件','服务器接受不等于已进入收件箱'])
f.arrow([(1460,1080),(1460,1145)])
f.text(50,1293,'当前源码未包含传感器采集、跌倒算法、短信、显示或自建云函数；本图只表示已实现链路。',20)
f.save('system_architecture')
f=Figure(1600,1290,'工作流程：一次独立上电邮件任务','自动启动默认查询 LBS；GNSS 是手动选择的查询分支 · 时间为代码中的等待 / 超时边界')
steps=[
('① 上电与应用启动',['app_main → Service → Console；加载编译配置，创建唯一 worker']),
('② 配置校验与 UART1 初始化',['收发邮箱 / 授权码校验；初始化后等待 DTU 启动 20 秒']),
('③ 查询 DTU 网络时间并设置 ESP32 时钟',['config,get,nettime；单次最多 5 秒，总预算 90 秒；UTC+8 → UTC']),
('④ 查询本次位置',['自动：LBS 最多 65 秒；手动 GNSS：最多 5 秒；校验帧与坐标']),
('⑤ 生成定位邮件正文',['有效：WGS84 经纬度 + 来源；无效：状态说明；均附查询完成时间']),
('⑥ 重启 DTU，准备新的 TCP 会话',['等待 20 秒；核验 smtp.qq.com:465 通道；TCP 就绪等待最多 90 秒']),
('⑦ ESP32 与 QQ 服务器完成 TLS 握手',['mbedTLS + 证书包；校验证书 / smtp.qq.com / 时间；握手最多 60 秒']),
('⑧ 编码 MIME 并执行 SMTP 提交',['EHLO → AUTH LOGIN → MAIL FROM → RCPT TO → DATA → 正文']),
('⑨ 记录结果，清理临时数据，回到等待任务',['DATA_ACCEPTED / REJECTED / FAILED / UNKNOWN_NO_RETRY；不自动补发'])
]
for i,(title,lines) in enumerate(steps):
 y=140+i*119
 f.box(70,y,1010,94,title,lines)
 if i<8:f.arrow([(575,y+94),(575,y+119)])
f.box(1140,245,400,175,'停止发送',['配置 / UART / 校时失败','TLS 校验或握手失败','记录失败结果，释放 busy'],fill='#fff0ef',stroke='#e4b1ad')
f.arrow([(1080,300),(1140,300)])
f.arrow([(1080,420),(1110,420),(1110,345),(1140,345)])
f.arrow([(1080,902),(1110,902),(1110,380),(1140,380)])
f.box(1140,500,400,150,'定位失败仍继续',['NO_FIX / TIMEOUT / 错误等','写明“无有效位置”','不回退使用旧坐标'],fill='#fff8e6',stroke='#e0ca8a')
f.arrow([(1080,544),(1140,544)])
f.box(1140,985,400,180,'提交结果必须区分',['最终 250：服务器已接受','明确 4xx / 5xx：被拒绝','DATA 后应答丢失：结果未知','未知时不自动重发'],fill='#eaf7ef',stroke='#b0d5be')
f.arrow([(1080,1139),(1140,1139)])
f.text(70,1230,'每次上电发一封 ≠ 周期发送；复位会启动新任务。各步骤可提前返回，图中时间不能当作实际发信耗时。',19)
f.save('boot_mail_flow')
print('Generated architecture and workflow: PNG + SVG')

