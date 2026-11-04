# 编译并运行 MuduoNet 的自动化测试。
#
# 用法：
#   ./Run-MuduoNetTests.ps1 -EngineRoot "C:\Program Files\Epic Games\UE_5.8" `
#       -ProjectFile "E:\Factory\Factory.uproject" -Configuration Development
#
# 压力用例单独跑：
#   ./Run-MuduoNetTests.ps1 ... -Filter Muduo.Stress

param(
	[Parameter(Mandatory = $true)][string]$EngineRoot,
	[Parameter(Mandatory = $true)][string]$ProjectFile,
	[string]$Configuration = "Development",
	[string]$Filter = "MuduoNet",
	[string]$EditorCmd = ""
)

$ErrorActionPreference = "Stop"

$BuildBat = Join-Path $EngineRoot "Engine/Build/BatchFiles/Build.bat"
if (-not (Test-Path $BuildBat)) {
	throw "找不到 Build.bat: $BuildBat"
}

Write-Host "== 编译 FactoryEditor $Configuration =="
& $BuildBat FactoryEditor Win64 $Configuration "-Project=$ProjectFile" -WaitMutex -NoHotReload
if ($LASTEXITCODE -ne 0) {
	throw "编译失败，退出码 $LASTEXITCODE"
}

if ([string]::IsNullOrEmpty($EditorCmd)) {
	$Candidates = @(
		(Join-Path $EngineRoot "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"),
		(Join-Path $EngineRoot "Engine/Binaries/Win64/UnrealEditor-Win64-Debug-Cmd.exe")
	)
	$EditorCmd = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

if ([string]::IsNullOrEmpty($EditorCmd) -or -not (Test-Path $EditorCmd)) {
	throw "找不到 UnrealEditor-Cmd.exe，请通过 -EditorCmd 指定"
}

$LogFile = Join-Path $env:TEMP "MuduoNetTests.log"
if (Test-Path $LogFile) {
	Remove-Item $LogFile -Force
}

Write-Host "== 运行自动化测试：$Filter =="
& $EditorCmd "$ProjectFile" "-ExecCmds=Automation RunTests $Filter" -unattended -nopause -nullrhi -nosplash -NoSound -testexit="Automation Test Queue Empty" "-abslog=$LogFile"

$ExitCode = $LASTEXITCODE
Write-Host "日志：$LogFile"

if (Test-Path $LogFile) {
	Select-String -Path $LogFile -Pattern "Test Completed|tests performed" | ForEach-Object { $_.Line }
}

exit $ExitCode
