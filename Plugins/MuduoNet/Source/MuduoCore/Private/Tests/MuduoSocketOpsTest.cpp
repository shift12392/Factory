#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Templates/Function.h"

#include "InetAddress.h"
#include "SocketOps.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool WaitUntil(const TFunction<bool()>& Predicate, double TimeoutSeconds)
	{
		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		for (;;)
		{
			if (Predicate())
			{
				return true;
			}
			if (FPlatformTime::Seconds() >= Deadline)
			{
				return false;
			}
			FPlatformProcess::Sleep(0.001f);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMuduoSocketOpsTest, "MuduoNet.Core.SocketOps",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMuduoSocketOpsTest::RunTest(const FString& Parameters)
{
	using namespace mnet;

	TestTrue(TEXT("网络子系统初始化"), FSocketOps::EnsureNetworkInitialized());

	// ---- 地址解析与格式化 ----
	{
		const FInetAddress Loopback = FInetAddress::Loopback(12345);
		TestTrue(TEXT("Loopback 有效"), Loopback.IsValid());
		TestEqual(TEXT("Loopback 端口"), static_cast<int32>(Loopback.GetPort()), 12345);
		TestTrue(TEXT("Loopback IP 文本"), Loopback.ToIpString() == TEXT("127.0.0.1"));
		TestTrue(TEXT("Loopback ip:port 文本"), Loopback.ToIpPortString() == TEXT("127.0.0.1:12345"));

		const FInetAddress Parsed = FInetAddress::FromIpPort(TEXT("192.168.1.10"), 8080);
		TestTrue(TEXT("解析点分十进制成功"), Parsed.IsValid());
		TestEqual(TEXT("解析端口"), static_cast<int32>(Parsed.GetPort()), 8080);
		TestTrue(TEXT("再次解析结果一致"), Parsed == FInetAddress::FromIpPort(TEXT("192.168.1.10"), 8080));

		TestFalse(TEXT("非法 IP 解析失败"), FInetAddress::FromIpPort(TEXT("300.1.1.1"), 80).IsValid());
		TestFalse(TEXT("空字符串解析失败"), FInetAddress::FromIpPort(TEXT(""), 80).IsValid());

		const FInetAddress AnyAddress = FInetAddress::Any(0);
		TestTrue(TEXT("Any 地址有效"), AnyAddress.IsValid());
		TestTrue(TEXT("Any 是未指定地址"), AnyAddress.IsUnspecified());

		const FInetAddress IPv6Loopback = FInetAddress::Loopback(0, true);
		TestTrue(TEXT("IPv6 Loopback 有效"), IPv6Loopback.IsValid());
		TestTrue(TEXT("IPv6 Loopback 文本"), IPv6Loopback.ToIpString() == TEXT("::1"));
	}

	// ---- 监听 / 连接 / 收发 ----
	FSocketHandle ListenSocket = FSocketOps::CreateTcp(false);
	TestTrue(TEXT("创建监听 socket"), ListenSocket.IsValid());
	TestTrue(TEXT("设置 SO_REUSEADDR"), FSocketOps::SetReuseAddr(ListenSocket.Get(), true));
	TestTrue(TEXT("绑定回环端口"), FSocketOps::Bind(ListenSocket.Get(), FInetAddress::Loopback(0)));
	TestTrue(TEXT("开始监听"), FSocketOps::Listen(ListenSocket.Get(), 8));

	const FInetAddress BoundAddress = FSocketOps::GetLocalAddress(ListenSocket.Get());
	TestTrue(TEXT("取得本地地址"), BoundAddress.IsValid());
	TestTrue(TEXT("系统已分配端口"), BoundAddress.GetPort() != 0);

	{
		FSocketHandle Duplicate = FSocketOps::CreateTcp(false);
		TestTrue(TEXT("创建第二个 socket"), Duplicate.IsValid());
		TestFalse(TEXT("重复绑定同一端口失败"), FSocketOps::Bind(Duplicate.Get(), BoundAddress));
	}

	FSocketHandle ClientSocket = FSocketOps::CreateTcp(false);
	TestTrue(TEXT("创建客户端 socket"), ClientSocket.IsValid());

	// 非阻塞 connect：返回 false 且错误为 InProgress/WouldBlock 属正常。
	const bool bConnectedImmediately = FSocketOps::Connect(ClientSocket.Get(), BoundAddress);
	if (!bConnectedImmediately)
	{
		const ESocketError ConnectError = FSocketOps::GetLastError();
		TestTrue(FString::Printf(TEXT("非阻塞 connect 错误应为进行中，实际 %s"), *FSocketOps::ErrorToString(ConnectError)),
			ConnectError == ESocketError::InProgress || ConnectError == ESocketError::TryAgain || ConnectError == ESocketError::AlreadyInProgress);
	}

	FSocketHandle ServerSocket;
	FInetAddress PeerAddress;
	const bool bAccepted = WaitUntil([&]()
	{
		ServerSocket = FSocketOps::Accept(ListenSocket.Get(), PeerAddress);
		return ServerSocket.IsValid();
	}, 3.0);

	TestTrue(TEXT("accept 成功"), bAccepted);
	TestTrue(TEXT("对端地址有效"), PeerAddress.IsValid());

	if (ServerSocket.IsValid())
	{
		TestTrue(TEXT("对端是回环地址"), PeerAddress.ToIpString() == TEXT("127.0.0.1"));

		uint8 Payload[64];
		for (int32 Index = 0; Index < 64; ++Index)
		{
			Payload[Index] = static_cast<uint8>(Index);
		}

		TestEqual(TEXT("发送 64 字节"), FSocketOps::Send(ClientSocket.Get(), Payload, 64), static_cast<int32>(64));

		uint8 Received[128] = { 0 };
		int32 ReceivedBytes = 0;
		WaitUntil([&]()
		{
			ReceivedBytes = FSocketOps::Recv(ServerSocket.Get(), Received, static_cast<int32>(sizeof(Received)));
			return ReceivedBytes > 0;
		}, 3.0);

		TestEqual(TEXT("收到 64 字节"), ReceivedBytes, static_cast<int32>(64));
		TestTrue(TEXT("收到内容一致"), FMemory::Memcmp(Received, Payload, 64) == 0);

		TestTrue(TEXT("服务端本地地址有效"), FSocketOps::GetLocalAddress(ServerSocket.Get()).IsValid());
		TestTrue(TEXT("服务端对端地址有效"), FSocketOps::GetPeerAddress(ServerSocket.Get()).IsValid());
		TestFalse(TEXT("不是自连接"), FSocketOps::IsSelfConnect(ServerSocket.Get()));

		FSocketOps::ShutdownWrite(ClientSocket.Get());
		int32 CloseRead = 1;
		WaitUntil([&]()
		{
			CloseRead = FSocketOps::Recv(ServerSocket.Get(), Received, static_cast<int32>(sizeof(Received)));
			return CloseRead == 0;
		}, 3.0);
		TestEqual(TEXT("对端半关闭后读到 0"), CloseRead, static_cast<int32>(0));
	}

	TestTrue(TEXT("错误码 0 映射为 None"), FSocketOps::ErrorToString(FSocketOps::ErrorFromCode(0)) == TEXT("None"));
	TestEqual(TEXT("监听 socket 的 SO_ERROR 为 0"), FSocketOps::GetSocketError(ListenSocket.Get()), static_cast<int32>(0));

	FSocketOps::ShutdownNetwork();

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
