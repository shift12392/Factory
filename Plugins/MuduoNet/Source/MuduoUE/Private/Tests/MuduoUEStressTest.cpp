#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Templates/Function.h"

#include "MuduoNetBridge.h"
#include "SocketOps.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace mnet;

	bool WaitUntilStress(const TFunction<bool()>& Predicate, double TimeoutSeconds)
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

	struct FStressState
	{
		TSharedPtr<FMuduoNetBridge> Bridge;
		TArray<uint8> Payload;

		std::atomic<int32> ConnectedClients{ 0 };
		std::atomic<int32> EchoedMessages{ 0 };
		std::atomic<int32> MismatchedMessages{ 0 };
	};
}

/**
 * 压力用例：200 条客户端连接同时收发。
 * 名字刻意不含 "MuduoNet"，避免默认套件每次都跑；单独执行：
 *   -ExecCmds="Automation RunTests Muduo.Stress"
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMuduoNetStressManyConnectionsTest, "Muduo.Stress.ManyConnections",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMuduoNetStressManyConnectionsTest::RunTest(const FString& Parameters)
{
	using namespace mnet;

	TestTrue(TEXT("初始化网络子系统"), FSocketOps::EnsureNetworkInitialized());

	const int32 NumConnections = 200;

	TSharedPtr<FStressState> State = MakeShared<FStressState>();
	State->Bridge = MakeShared<FMuduoNetBridge>(EDispatchMode::Inline);

	State->Payload.SetNumUninitialized(48);
	for (int32 Index = 0; Index < State->Payload.Num(); ++Index)
	{
		State->Payload[Index] = static_cast<uint8>((Index * 17) & 0xFF);
	}

	State->Bridge->SetConnectionEvent([State](int64 EndpointId, bool bConnected)
	{
		if (EndpointId >= FMuduoNetBridge::kClientIdOffset && bConnected)
		{
			State->ConnectedClients.fetch_add(1);
		}
	});

	State->Bridge->SetMessageEvent([State](int64 EndpointId, const TArray<uint8>& Message)
	{
		if (EndpointId < FMuduoNetBridge::kClientIdOffset)
		{
			State->Bridge->SendToServerConnection(EndpointId, Message);
			return;
		}

		if (Message.Num() != State->Payload.Num() ||
			(Message.Num() > 0 && FMemory::Memcmp(Message.GetData(), State->Payload.GetData(), Message.Num()) != 0))
		{
			State->MismatchedMessages.fetch_add(1);
		}
		State->EchoedMessages.fetch_add(1);
	});

	// 服务端用 2 个 IO 线程，验证多 loop 下的连接分配。
	TestTrue(TEXT("启动服务端"), State->Bridge->StartServer(0, 2));

	TArray<int64> ClientIds;
	ClientIds.Reserve(NumConnections);
	for (int32 Index = 0; Index < NumConnections; ++Index)
	{
		const int64 ClientId = State->Bridge->ConnectToServer(TEXT("127.0.0.1"), State->Bridge->GetServerPort());
		if (ClientId != 0)
		{
			ClientIds.Add(ClientId);
		}
	}

	TestEqual(TEXT("客户端全部创建成功"), ClientIds.Num(), NumConnections);
	TestTrue(TEXT("全部连接建立"),
		WaitUntilStress([State, NumConnections]() { return State->ConnectedClients.load() >= NumConnections; }, 20.0));
	TestTrue(TEXT("服务端连接数一致"),
		WaitUntilStress([State, NumConnections]() { return State->Bridge->GetServerConnectionCount() == NumConnections; }, 10.0));

	for (const int64 ClientId : ClientIds)
	{
		State->Bridge->SendFromClient(ClientId, State->Payload);
	}

	TestTrue(TEXT("全部收到回显"),
		WaitUntilStress([State, NumConnections]() { return State->EchoedMessages.load() >= NumConnections; }, 20.0));
	TestEqual(TEXT("回显数量"), State->EchoedMessages.load(), NumConnections);
	TestEqual(TEXT("回显内容全部一致"), State->MismatchedMessages.load(), static_cast<int32>(0));
	TestTrue(FString::Printf(TEXT("loop 最大忙碌耗时 %.2fms 应低于 500ms"), State->Bridge->GetMaxLoopBusyMs()),
		State->Bridge->GetMaxLoopBusyMs() < 500.0);
	TestTrue(TEXT("统计：接收字节数"), State->Bridge->GetBytesReceived() >= NumConnections * State->Payload.Num());

	for (const int64 ClientId : ClientIds)
	{
		State->Bridge->DisconnectClient(ClientId);
	}

	TestTrue(TEXT("全部断开且服务端连接数归零"),
		WaitUntilStress([State]() { return State->Bridge->GetServerConnectionCount() == 0; }, 20.0));

	State->Bridge->StopServer();
	TestFalse(TEXT("服务端已停止"), State->Bridge->IsServerRunning());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
