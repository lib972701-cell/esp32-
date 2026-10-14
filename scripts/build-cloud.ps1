param([string]$IdfRoot='D:/esp32-idf/Espressif/frameworks/esp-idf-5.4',
      [string]$ToolsRoot='D:/esp32-idf/Espressif', [switch]$Reconfigure)
$ErrorActionPreference='Stop'
$env:IDF_PATH=$IdfRoot
$env:IDF_TOOLS_PATH=$ToolsRoot
$env:ESP_IDF_VERSION='5.4.0'
$env:SAMPLE_CLOUD_AI_BUILD='1'
$env:IDF_PYTHON_ENV_PATH=Join-Path $ToolsRoot 'python_env/idf5.4_py3.12_env'
$taskPython=Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe'
$taskDirs=@((Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts'),
    (Join-Path $ToolsRoot 'tools/cmake/3.30.2/bin'),
    (Join-Path $ToolsRoot 'tools/ninja/1.12.1'),
    (Join-Path $ToolsRoot 'tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin'))
foreach($taskDir in $taskDirs) { if(!(Test-Path -LiteralPath $taskDir)) { throw "Missing tool: $taskDir" } }
$env:PATH=($taskDirs -join ';')+';'+$env:PATH
$env:GIT_CONFIG_COUNT='1'
$env:GIT_CONFIG_KEY_0='safe.directory'
$env:GIT_CONFIG_VALUE_0=$IdfRoot
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    New-Item -ItemType Directory -Force 'verification/cloud_ai_20261013' | Out-Null
    & $taskPython "$IdfRoot/tools/idf.py" --version
    if($LASTEXITCODE -ne 0) { throw 'IDF5.4 preflight failed' }
    $taskActions=@('build')
    if($Reconfigure) { $taskActions=@('reconfigure','build') }
    $taskPreference=$ErrorActionPreference
    $ErrorActionPreference='Continue'
    & $taskPython "$IdfRoot/tools/idf.py" -B build-cloud '-DCLOUD_AI_BUILD=ON' '-DIDF_TARGET=esp32s3' '-DSDKCONFIG=sdkconfig.cloud' '-DSDKCONFIG_DEFAULTS=sdkconfig.cloud.defaults' @taskActions 2>&1 | Tee-Object 'verification/cloud_ai_20261013/build.log'
    $taskExit=$LASTEXITCODE
    $ErrorActionPreference=$taskPreference
    if($taskExit -ne 0) { throw "Cloud integration build failed: $taskExit" }
} finally { Pop-Location }
