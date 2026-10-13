# ESP32-S3 经 UART / 4G DTU 直发 QQ 邮件

独立运行入口：[README_独立上电邮件.md](README_独立上电邮件.md)。本机私有配置启用每次上电一封，ESP32自行从DTU校时和定位，无需电脑脚本提供运行参数。

当前目录结构见[README_工程分层.md](README_工程分层.md)：main仅入口，components/BSP负责硬件、Middlewares负责协议、App负责业务与控制台；旧集中源码已备份。

本轮入口：[README_邮件链路.md](README_邮件链路.md)。现有工程已移植邮件程序；原模板完整备份在 `history/20261011-before-mail/`。

定位扩展入口：[README_定位链路.md](README_定位链路.md)，提供基站/GNSS一次性串口查询，与邮件共用任务队列；有效硬件定位仍待验证。

定位邮件联动：[README_定位邮件联动.md](README_定位邮件联动.md)，手动查询位置后发送一次邮件，定位失败仍可发信；组合链路已编译，硬件实测待完成。

下述为原工程历史说明。

| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-H2 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | -------- | -------- |

# _Sample project_

(See the README.md file in the upper level 'examples' directory for more information about examples.)

This is the simplest buildable example. The example is used by command `idf.py create-project`
that copies the project to user specified path and set it's name. For more information follow the [docs page](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#start-a-new-project)



## How to use example
We encourage the users to use the example as a template for the new projects.
A recommended way is to follow the instructions on a [docs page](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#start-a-new-project).

## Example folder contents

The project **sample_project** contains one source file in C language [main.c](main/main.c). The file is located in folder [main](main).

ESP-IDF projects are built using CMake. The project build configuration is contained in `CMakeLists.txt`
files that provide set of directives and instructions describing the project's source files and targets
(executable, library, or both). 

Below is short explanation of remaining files in the project folder.

```
├── CMakeLists.txt
├── main
│   ├── CMakeLists.txt
│   └── main.c
└── README.md                  This is the file you are currently reading
```
Additionally, the sample project contains Makefile and component.mk files, used for the legacy Make based build system. 
They are not used or needed when building with CMake and idf.py.
