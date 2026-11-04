#!/usr/bin/env bash
# 在 Linux 上编译并运行 MuduoNet 自动化测试。
#
# 用法：
#   ./Run-MuduoNetTests.sh /path/to/UnrealEngine /path/to/Factory.uproject [Filter]
#
# 说明：Linux 需要先安装引擎的 Linux 工具链（Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux）。

set -euo pipefail

ENGINE_ROOT="${1:?usage: Run-MuduoNetTests.sh <engine-root> <uproject> [filter]}"
PROJECT_FILE="${2:?usage: Run-MuduoNetTests.sh <engine-root> <uproject> [filter]}"
FILTER="${3:-MuduoNet}"

BUILD_SH="$ENGINE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh"
EDITOR="$ENGINE_ROOT/Engine/Binaries/Linux/UnrealEditor"

if [[ ! -x "$BUILD_SH" ]]; then
  echo "找不到 Build.sh: $BUILD_SH" >&2
  exit 1
fi

echo "== 编译 FactoryEditor Linux Development =="
"$BUILD_SH" FactoryEditor Linux Development -Project="$PROJECT_FILE"

echo "== 运行自动化测试：$FILTER =="
"$EDITOR" "$PROJECT_FILE" \
  -ExecCmds="Automation RunTests $FILTER" \
  -unattended -nopause -nullrhi -nosplash -NoSound \
  -testexit="Automation Test Queue Empty"
