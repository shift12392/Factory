#include "Modules/ModuleManager.h"

// MuduoUE 只有 UObject 适配层（Subsystem / Component / Bridge），
// 网络核心在 MuduoCore 模块里，这里不需要额外初始化。
IMPLEMENT_MODULE(FDefaultModuleImpl, MuduoUE)
