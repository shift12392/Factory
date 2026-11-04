# MuduoNet

跨平台（Win64 / Linux）的 muduo 风格 Reactor 网络库，以 UE 插件形式提供。

- Windows：`WSAPoll` + 水平触发（Windows 没有边沿触发）
- Linux：`epoll` + 水平触发（与 muduo 默认一致）
- Socket 层自建（winsock2 / POSIX），**不依赖 `Sockets` 模块，也不需要修改引擎源码**
- 不引用任何 `UObject`，核心模块可以在无引擎循环的情况下做单元测试

## 目录结构

```
Plugins/MuduoNet/
├── MuduoNet.uplugin
└── Source/MuduoCore/
    ├── MuduoCore.Build.cs
    ├── Public/                     # 对外 API（M3 的 MuduoUE 适配层只用这些头）
    │   ├── MuduoNetPlatform.h      # 平台宏兜底 + 不支持平台直接报错
    │   ├── SocketHandle.h          # FSocketHandle（Windows SOCKET / Linux fd）
    │   ├── InetAddress.h           # 地址 + 端口，平台无关
    │   ├── SocketOps.h             # 跨平台 socket 操作 + 错误码归一化
    │   ├── Buffer.h                # muduo Buffer 移植（8 字节 prepend）
    │   ├── ChannelTie.h            # 可被 Channel 弱引用守护的宿主接口
    │   ├── Timer.h                 # FTimer / FTimerId
    │   ├── EventLoop.h             # RunInLoop / QueueInLoop / 定时器 / poll
    │   ├── EventLoopThread.h       # one loop per thread（FRunnable）
    │   ├── EventLoopThreadPool.h   # 轮询分配 + hash 分配
    │   ├── TcpConnection.h         # 连接状态机、Send/Shutdown/ForceClose、回调
    │   ├── TcpServer.h             # Acceptor + 线程池 + 连接表
    │   ├── TcpClient.h             # Connector + 可选自动重连
    │   ├── LengthHeaderCodec.h     # 4 字节大端长度头拆包/粘包
    │   └── MuduoNetLog.h           # LogMuduoNet
    └── Private/
        ├── Buffer.cpp  MuduoNetLog.cpp  MuduoCoreModule.cpp
        ├── Net/                    # 内部实现
        │   ├── Channel.h/.cpp      # 事件位、tie 生命周期守护、HandleEvent
        │   ├── Poller.h/.cpp       # 抽象 + 线程归属断言
        │   ├── EPollPoller.cpp     # Linux
        │   ├── WSAPollPoller.cpp   # Windows
        │   ├── TimerQueue.h/.cpp   # 有序定时器表（poll 超时驱动）
        │   ├── Acceptor.h/.cpp     # 非阻塞 accept 循环 + EMFILE idle fd
        │   ├── Connector.h/.cpp    # 非阻塞 connect + SO_ERROR + 指数退避重连
        │   ├── EventLoop.cpp  EventLoopThread.cpp  EventLoopThreadPool.cpp
        │   ├── TcpConnection.cpp  TcpServer.cpp  TcpClient.cpp  LengthHeaderCodec.cpp
        ├── Platform/
        │   ├── PlatformSocketIncludes.h
        │   ├── InetAddress.cpp
        │   ├── SocketOpsLinux.cpp / SocketOpsWin.cpp
        │   ├── Wakeup.h
        │   └── WakeupLinux.cpp（eventfd）/ WakeupWin.cpp（环回 UDP 对）
        └── Tests/                  # UE Automation 测试
    └── MuduoUE/                    # UE 适配层（M3）
        ├── MuduoUE.Build.cs
        ├── Public/
        │   ├── MuduoNetBridge.h        # 端点 id + 字节流桥接，可选 GameThread/Inline 派发
        │   ├── MuduoNetSubsystem.h     # UGameInstanceSubsystem：服务端 + 多客户端 + 蓝图委托
        │   └── MuduoNetComponent.h     # UActorComponent：自动连接/收发/断开的客户端
        ├── Private/
        │   ├── MuduoUE.cpp  MuduoNetBridge.cpp  MuduoNetSubsystem.cpp  MuduoNetComponent.cpp
        │   └── Tests/                  # MuduoNet.UE.* 自动化测试
```

## 当前进度

| 组件 | 状态 |
|---|---|
| 平台抽象层（SocketHandle / SocketOps / InetAddress / Wakeup） | ✅ Win64 已编译并通过测试 |
| Buffer（reader/writer index、prepend、readv/双 recv、MakeSpace） | ✅ |
| Channel（事件位、tie、HandleEvent 分发顺序：HUP → ERR → IN → OUT） | ✅ |
| Poller（epoll LT / WSAPoll LT，含注册/注销/扩容） | ✅ Win64 已测试 |
| Timer / TimerQueue（单次、重复、回调内取消，poll 超时驱动） | ✅ |
| EventLoop（RunInLoop / QueueInLoop / 跨线程唤醒 / Quit / 迭代统计） | ✅ |
| EventLoopThread / EventLoopThreadPool（one loop per thread，轮询与 hash 分配） | ✅ |
| 跨线程 echo 集成（两个 IO 线程：监听 + accept + 回显 + 清理） | ✅ |
| TcpConnection（状态机 / Send 跨线程投递 / 半关闭 / 高水位 / tie 守护） | ✅ |
| Acceptor（非阻塞 accept 循环 + EMFILE idle fd） | ✅ |
| Connector（非阻塞 connect + SO_ERROR + 自连接检测 + 指数退避重连） | ✅ |
| TcpServer / TcpClient（连接表、轮询分配 IO loop、可选自动重连） | ✅ |
| LengthHeaderCodec（4 字节大端长度头，拆包/粘包） | ✅ |
| MuduoUE 适配层：Bridge（端点 id / 派发模式 / 统计） | ✅ |
| UGameInstanceSubsystem（服务端 + 多客户端 + 蓝图委托） | ✅ |
| UActorComponent（自动连接 / 收发 / 断开 + 组件级事件） | ✅ |
| GameThread 派发验证（latent 跨帧测试） | ✅ |

## UE 适配层用法（M3）

端点 id 规则：服务端连接从 `1` 开始，客户端从 `FMuduoNetBridge::kClientIdOffset`（10 亿）开始，
蓝图里用同一个 `int64` 就能区分两侧。

**C++ / 蓝图（服务端）**

```cpp
UMuduoNetSubsystem* Net = GetGameInstance()->GetSubsystem<UMuduoNetSubsystem>();
Net->OnConnectionEvent.AddDynamic(this, &UMyObject::HandleConnection);
Net->OnMessageEvent.AddDynamic(this, &UMyObject::HandleMessage);
Net->StartServer(7779, /*ServerThreadNum=*/2);
// 回消息：Net->SendToConnection(ConnectionId, Data);
```

```cpp
void UMyObject::HandleMessage(int64 EndpointId, const TArray<uint8>& Message)
{
    if (EndpointId < mnet::FMuduoNetBridge::kClientIdOffset)   // 来自客户端
    {
        Net->SendToConnection(EndpointId, Message);            // 原样回显
    }
}
```

**蓝图（客户端）**：把 `MuduoNetComponent` 挂到 Actor 上，填 `ServerHost` / `ServerPort`，
勾选 `bAutoConnect`，绑定 `OnMessage` 即可；发消息用组件的 `Send`。

GUI 使用要点：

- 默认开启 4 字节大端长度头（`SetUseLengthHeaderCodec(false)` 可关闭，此时投递的是原始字节块）。
- 回调都在游戏线程，可以直接操作 UObject / 蓝图。
- `StopServer` / `DisconnectClient` / 组件 `EndPlay` 都会优雅收尾（半关闭 + loop 内销毁）。

## TCP 层语义（M2）

- **连接归属**：`FTcpServer` 的 `Acceptor` 跑在 base loop；新连接由 `EventLoopThreadPool` 轮询分配，
  在目标 IO loop 上创建 `FTcpChannel` 并 `ConnectEstablished()`。连接表只在 base loop 访问。
- **回调线程**：`Connection / Message / WriteComplete / HighWaterMark` 都在连接所属 loop 线程执行；
  `Message` 回调里的 `FBuffer*` 只在回调期间有效。
- **Cross-thread API**：`Send / Shutdown / ForceClose / StartRead / StopRead` 任意线程可调，
  内部投递到 loop；`Send` 会先尝试直写，写不完才进输出缓冲并关注可写（LT）。
- **关闭顺序**：`HandleClose` → 状态置 Disconnected → `DisableAll` → ConnectionCallback →
  CloseCallback（**最后一行**，`TcpServer/TcpClient` 在这里摘除连接），随后 `ConnectDestroyed` 在 loop 内销毁 Channel。
- **半关闭**：`Shutdown()` → `shutdown(SHUT_WR)`；对端读到 0 后关闭，双方都会收到断开回调。
- **重连**：`FTcpClient::SetRetry(true)` 时，断开后由 `Connector::Restart()` 重新走非阻塞连接；
  连接失败按 500ms 起、每次翻倍、上限 30s 的退避重试。
- **编解码**：`FLengthHeaderCodec` 提供 `Encode/OnMessage/Send`，支持半包、粘包、以及超大长度头的拒绝。

## 压力与诊断（M4）

**运行时指标**（`UMuduoNetSubsystem` / `FMuduoNetBridge` 都提供）：

| 接口 | 含义 |
|---|---|
| `GetServerConnectionCount()` | 当前服务端连接数 |
| `GetClientCount()` | 已创建的客户端端点数 |
| `GetBytesReceived()` / `GetBytesSent()` | 累计收发字节（不含 4 字节长度头） |
| `GetLastLoopBusyMs()` / `GetMaxLoopBusyMs()` | IO 线程单轮「事件处理」耗时（不含 poll 等待），用来判断回调是否拖慢了 loop |

**压力用例**（默认套件不会跑，名字刻意不含 `MuduoNet`）：

```bat
UnrealEditor-Cmd.exe Factory.uproject -ExecCmds="Automation RunTests Muduo.Stress" ^
  -unattended -nopause -nullrhi -nosplash -NoSound -testexit="Automation Test Queue Empty"
```

200 条客户端连接、服务端 2 个 IO 线程、48 字节消息全量回显；断言包含回显内容一致、
`GetMaxLoopBusyMs() < 500ms`、断开后服务端连接数归零。

**一键脚本**：

```powershell
# Windows（引擎路径按实际安装位置替换）
Plugins/MuduoNet/Scripts/Run-MuduoNetTests.ps1 `
  -EngineRoot "C:\Program Files\Epic Games\UE_5.8" `
  -ProjectFile "E:\Factory\Factory.uproject" -Configuration Development
```

```bash
# Linux（需要先安装 Linux 工具链）
Plugins/MuduoNet/Scripts/Run-MuduoNetTests.sh /path/to/UnrealEngine /path/to/Factory.uproject
```

**CI**：[`.github/workflows/muduonet.yml`](../../.github/workflows/muduonet.yml) 使用 self-hosted runner
（标签 `windows` + `unreal-engine`）并读取仓库变量 `UE_ROOT`；因为 UE 无法在 GitHub 托管 runner 上运行，
未配置 `UE_ROOT` 时工作流直接跳过。

**日志策略**：正常路径不打日志；发送/读取失败、非法长度头、创建线程失败才用 `Warning`/`Error`，
热路径最多是 `Verbose`（默认 verbosity 下不输出）。注意 UE 自动化会把测试期间出现的
`Warning`/`Error` 当成失败，需要覆盖时用 `AddExpectedErrorPlain`。



Win64（本仓库使用的引擎只安装了 Debug 编辑器，因此配置为 Debug）：

```bat
"E:\UnrealEngine\Engine\Build\BatchFiles\Build.bat" FactoryEditor Win64 Debug ^
  -Project="E:\Factory\Factory.uproject" -WaitMutex -NoHotReload
```

也可以使用 VS 打开 `Factory.sln` 直接编译 `MuduoCore` 模块。

> 插件已在 `Factory.uproject` 的 `Plugins` 列表中启用；不引用任何游戏代码，不影响现有运行行为。

## 测试

```bat
"E:\UnrealEngine\Engine\Binaries\Win64\UnrealEditor-Win64-Debug-Cmd.exe" "E:\Factory\Factory.uproject" ^
  -ExecCmds="Automation RunTests MuduoNet" -unattended -nopause -nullrhi -nosplash -NoSound ^
  -testexit="Automation Test Queue Empty" -abslog="%TEMP%\MuduoNetTests.log"
```

当前用例：

| 用例 | 覆盖内容 |
|---|---|
| `MuduoNet.Core.Buffer` | 读写索引、prepend 顺序、大块写入触发 MakeSpace、数据完整性 |
| `MuduoNet.Core.SocketOps` | 地址解析/格式化、bind/listen、重复 bind 失败、非阻塞 connect、accept、收发、半关闭读到 0 |
| `MuduoNet.Core.Poller` | 超时返回、唤醒源打断 poll、非阻塞 connect 可写事件、accept、可读事件、半关闭 |
| `MuduoNet.Core.EventLoop` | 本线程 RunInLoop 立即执行、跨线程 QueueInLoop 唤醒并在 loop 线程执行、单次/重复定时器、回调内取消、Quit |
| `MuduoNet.Core.EventLoopThread` | 独立线程上的 loop、定时器线程归属、重复 Stop 安全 |
| `MuduoNet.Core.EventLoopThreadPool` | 3 个 loop 分属 3 个线程、轮询分配、hash 分配稳定、0 线程退回 base loop |
| `MuduoNet.Core.Echo` | 两个 IO 线程上的完整链路：监听 → accept → 收发回显 → 各自 loop 内清理 |
| `MuduoNet.Core.LengthHeaderCodec` | 编码字节序、半包不触发回调、粘包连续回调、非法长度拒绝、空消息 |
| `MuduoNet.Core.TcpServerClient` | TcpServer/TcpClient 端到端：单 IO 线程与双 IO 线程两种配置，3 条消息（16B/300B/256KB）回显、顺序与内容校验、半关闭后两端都感知断开 |
| `MuduoNet.UE.Bridge` | 适配层端到端（Inline 派发）：启动服务端、连接、收发回显、字节统计、断开与服务端连接数归零 |
| `MuduoNet.UE.GameThreadDispatch` | GameThread 派发：跨帧（latent）验证回调确实在游戏线程执行、内容正确 |
| `Muduo.Stress.ManyConnections` | 压力用例（不在默认套件内）：200 条连接 + 2 个 IO 线程，全量回显、内容校验、loop 忙碌耗时上限、连接数归零 |

Linux：本机既没有 WSL 也没有 Linux 交叉工具链（`Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux` 不存在），
因此 Linux 分支目前只完成代码与平台隔离，需要在装有 Linux 工具链的机器上验证：

```bash
# 在 Linux 机器上（或安装了 Linux 工具链的 Windows 上）
Engine/Build/BatchFiles/Linux/Build.sh FactoryEditor Linux Development -Project=/path/Factory.uproject
Engine/Binaries/Linux/UnrealEditor Factory.uproject \
  -ExecCmds="Automation RunTests MuduoNet" -unattended -nopause -nullrhi -nosplash \
  -testexit="Automation Test Queue Empty"
```

## 平台差异（实现时已固化）

| 维度 | Linux | Windows |
|---|---|---|
| 事件多路复用 | `epoll` LT | `WSAPoll` LT |
| 唤醒 | `eventfd` | 环回 UDP 对 |
| 半关闭 | `shutdown(SHUT_WR)`，`EPOLLRDHUP` | `shutdown(SD_SEND)`，读 0 判定 |
| 错误码 | `errno` | `WSAGetLastError()` |
| 写入防 SIGPIPE | `MSG_NOSIGNAL` + 忽略 SIGPIPE | 不适用 |
| 句柄类型 | `int` fd | `SOCKET`（64 位），统一为 `FSocketValue` |

设计约束（后续 TcpConnection 必须遵守）：

1. **所有 fd 操作只在所属 loop 线程**；跨线程只投递任务。
2. **socket 关闭必须延后到 loop 线程**：Windows 上对 `WSAPoll` 数组里已关闭的句柄会整体返回 `WSAENOTSOCK`。
3. **水平触发语义**：读要循环到 `WouldBlock`，写根据残留数据决定是否关注可写。
4. `FBuffer` 只在回调期间有效，跨线程请拷贝。
