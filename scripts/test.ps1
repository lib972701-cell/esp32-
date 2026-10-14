$ErrorActionPreference='Stop'
$taskTestRoot=Split-Path $PSScriptRoot -Parent
# Zig缓存与本项目使用同一盘符，避免已有跨盘缓存产生CrossDevice错误。
$env:ZIG_LOCAL_CACHE_DIR=Join-Path $taskTestRoot 'verification/zig-cache'
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $taskTestRoot 'verification/zig-global-cache'
Push-Location $taskTestRoot
try {
    New-Item -ItemType Directory -Force 'verification' | Out-Null
    New-Item -ItemType Directory -Force 'verification/vision_20261014' | Out-Null
    python -m ziglang cc -O1 -UNDEBUG -Wall -Wextra -Werror -I components/Middlewares/Vision tests/vision_session_test.c components/Middlewares/Vision/vision_session.c -o tests/vision_session_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Vision session test compilation failed' }
    & ./tests/vision_session_test.exe | Tee-Object 'verification/vision_20261014/session-tests.log'
    if ($LASTEXITCODE -ne 0) { throw 'Vision session tests failed' }
    python -m ziglang cc -O1 -UNDEBUG -Wall -Wextra -Werror -I components/CloudAI/control -I components/CloudAI/board tests/key0_session_test.c -o tests/key0_session_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'KEY0/session test compilation failed' }
    & ./tests/key0_session_test.exe | Tee-Object 'verification/key0-session-tests.log'
    if ($LASTEXITCODE -ne 0) { throw 'KEY0/session tests failed' }
    python -m ziglang cc -O1 -UNDEBUG -Wall -Wextra -Werror -I tests/imu_v2_stubs -I components/BSP/MPU6050/include tests/imu_port_v2_test.c components/BSP/I2C/idf5/imu_port_v2.c -o tests/imu_port_v2_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'IMU v2 transport test compilation failed' }
    & ./tests/imu_port_v2_test.exe | Tee-Object 'verification/imu-port-v2-tests.log'
    if ($LASTEXITCODE -ne 0) { throw 'IMU v2 transport tests failed' }
    python -m ziglang cc -O1 -fno-sanitize=undefined -Wall -Wextra -Werror -I components/Middlewares/MAIL -I components/Middlewares/LOCATION -I components/App/Message -I components/BSP/DTU tests/protocol_test.c components/Middlewares/MAIL/mail_protocol.c -o tests/protocol_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host C compilation failed' }
    & ./tests/protocol_test.exe | Tee-Object 'verification/protocol-tests.log'
    if ($LASTEXITCODE -ne 0) { throw 'Protocol tests failed' }
    python -m ziglang cc -O1 -fno-sanitize=undefined -Wall -Wextra -Werror -I components/Middlewares/MAIL -I components/Middlewares/LOCATION -I components/App/Message -I components/BSP/DTU tests/location_test.c components/Middlewares/LOCATION/location_protocol.c components/BSP/DTU/dtu_frame.c -o tests/location_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Location C compilation failed' }
    & ./tests/location_test.exe | Tee-Object 'verification/location-tests.log'
    if ($LASTEXITCODE -ne 0) { throw 'Location tests failed' }
    python -m ziglang cc -O1 -fno-sanitize=undefined -Wall -Wextra -Werror -I components/Middlewares/MAIL -I components/Middlewares/LOCATION -I components/App/Message -I components/BSP/DTU tests/location_mail_test.c components/App/Message/location_mail.c components/Middlewares/LOCATION/location_protocol.c -o tests/location_mail_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Location mail C compilation failed' }
    & ./tests/location_mail_test.exe | Tee-Object 'verification/location-mail-tests.log'
    if ($LASTEXITCODE -ne 0) { throw 'Location mail tests failed' }
    python -m ziglang cc -O1 -fno-sanitize=undefined -Wall -Wextra -Werror -I components/Middlewares/TIME tests/time_test.c components/Middlewares/TIME/dtu_time_parse.c -o tests/time_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Time C compilation failed' }
    & ./tests/time_test.exe | Tee-Object 'verification/time-tests.log'
    if ($LASTEXITCODE -ne 0) { throw 'Time tests failed' }
    python -m ziglang cc -O1 -fno-sanitize=undefined -Wall -Wextra -Werror -I components/App/Message -I components/Middlewares/MAIL tests/fall_mail_test.c components/App/Message/fall_mail.c -o tests/fall_mail_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Fall mail compilation failed' }
    & ./tests/fall_mail_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Fall mail tests failed' }
    python -m ziglang cc -O1 -fno-sanitize=undefined -Wall -Wextra -Werror -I tests/imu_stubs -I components/BSP/MPU6050 -I components/BSP/MPU6050/include -I components/Middlewares/FALL/include -I components/BSP/MPU6050/include tests/imu_driver_test.c components/BSP/MPU6050/imu_mpu6050.c -o tests/imu_driver_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'IMU driver compilation failed' }
    & ./tests/imu_driver_test.exe | Tee-Object 'verification/imu-driver-tests.log'
    if ($LASTEXITCODE -ne 0) { throw 'IMU driver tests failed' }
    foreach ($fallTest in @('test_fall_detector','test_help_policy')) {
        python -m ziglang cc -O1 -fno-sanitize=undefined -Wall -Wextra -Werror -I components/Middlewares/FALL/include -I components/BSP/MPU6050/include "tests/$fallTest.c" components/Middlewares/FALL/attitude.c components/Middlewares/FALL/fall_detector.c components/Middlewares/FALL/imu_calib.c components/Middlewares/FALL/help_policy.c -o "tests/$fallTest.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Fall core compilation failed' }
        & "./tests/$fallTest.exe" | Tee-Object "verification/$fallTest.log"
        if ($LASTEXITCODE -ne 0) { throw 'Fall core tests failed' }
    }
    python -m unittest discover -s tests -p 'test_*.py' -v
    if ($LASTEXITCODE -ne 0) { throw 'Host helper tests failed' }
} finally { Pop-Location }
