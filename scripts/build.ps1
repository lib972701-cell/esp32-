param([string]$IdfPath='D:/esp32-idf/Espressif/frameworks/esp-idf-v5.1.2',
      [string]$ToolsPath='D:/esp32-idf/Espressif',
      [switch]$Reconfigure)
$ErrorActionPreference='Stop'
$env:IDF_PATH=$IdfPath
$env:IDF_TOOLS_PATH=$ToolsPath
$env:PROCESSOR_ARCHITECTURE='AMD64'
# Trust only the known installed framework in this build process, not globally.
$env:GIT_CONFIG_COUNT='1'
$env:GIT_CONFIG_KEY_0='safe.directory'
$env:GIT_CONFIG_VALUE_0=$IdfPath
$taskPython=Join-Path $ToolsPath 'python_env/idf5.1_py3.11_env/Scripts/python.exe'
if (!(Test-Path -LiteralPath $taskPython)) { throw 'ESP-IDF 5.1 Python environment not found' }
$taskVars=& $taskPython "$IdfPath/tools/idf_tools.py" export --format key-value
if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF tool export failed' }
foreach ($taskLine in $taskVars) {
    $taskPair=$taskLine -split '=',2
    if ($taskPair.Count -eq 2) {
        $taskValue=$taskPair[1]
        if ($taskPair[0] -eq 'PATH') { $taskValue=$taskValue.Replace('%PATH%',$env:PATH) }
        [Environment]::SetEnvironmentVariable($taskPair[0],$taskValue,'Process')
    }
}
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    New-Item -ItemType Directory -Force 'verification' | Out-Null
    & $taskPython "$IdfPath/tools/idf.py" --version
    if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF preflight failed' }
    # Windows PowerShell 5 treats normal native stderr as an error record.
    # Keep the output but decide success from idf.py's exit status.
    $taskPreviousPreference=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        $taskIdfActions=@('build')
        if ($Reconfigure) { $taskIdfActions=@('reconfigure','build') }
        & $taskPython "$IdfPath/tools/idf.py" @taskIdfActions 2>&1 | Tee-Object 'verification/build.log'
        $taskBuildExit=$LASTEXITCODE
    } finally { $ErrorActionPreference=$taskPreviousPreference }
    if ($taskBuildExit -ne 0) { throw 'ESP-IDF build failed' }
} finally { Pop-Location }
