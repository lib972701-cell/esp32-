$ErrorActionPreference='Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    New-Item -ItemType Directory -Force 'verification' | Out-Null
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
    python -m ziglang cc -O1 -fno-sanitize=undefined -Wall -Wextra -Werror -I tests/imu_stubs -I components/imu_hal -I components/imu_hal/include -I components/fall_core/include tests/imu_driver_test.c components/imu_hal/imu_mpu6050.c -o tests/imu_driver_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'IMU driver compilation failed' }
    & ./tests/imu_driver_test.exe | Tee-Object 'verification/imu-driver-tests.log'
    if ($LASTEXITCODE -ne 0) { throw 'IMU driver tests failed' }
    foreach ($fallTest in @('test_fall_detector','test_help_policy')) {
        python -m ziglang cc -O1 -fno-sanitize=undefined -Wall -Wextra -Werror -I components/fall_core/include "tests/$fallTest.c" components/fall_core/attitude.c components/fall_core/fall_detector.c components/fall_core/imu_calib.c components/fall_core/help_policy.c -o "tests/$fallTest.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Fall core compilation failed' }
        & "./tests/$fallTest.exe" | Tee-Object "verification/$fallTest.log"
        if ($LASTEXITCODE -ne 0) { throw 'Fall core tests failed' }
    }
    python -m unittest discover -s tests -p 'test_*.py' -v
    if ($LASTEXITCODE -ne 0) { throw 'Host helper tests failed' }
} finally { Pop-Location }
