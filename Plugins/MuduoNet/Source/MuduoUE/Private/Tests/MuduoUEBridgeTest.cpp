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

	bool WaitUntilBridge(const TFunction<bool()>& Predicate, double TimeoutSeconds)
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

	struct FBridgeTestState
	{
		TSharedPtr<FMuduoNetBridge> Bridge;
		TArray<uint8> Payload;

		std::atomic<int32> ServerMessages{ 0 };
		std::atomic<int32> ClientMessages{ 0 };
		std::atomic<int32> EchoedBytes{ 0 };
		std::atomic<bool> bPayloadMismatch{ false };
		std::atomic<bool> bClientConnected{ false };
		std::atomic<bool> bClientDisconnected{ false };
		std::atomic<bool> bServerSawConnection{ false };
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMuduoUEBridgeTest, "MuduoNet.UE.Bridge",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMuduoUEBridgeTest::RunTest(const FString& Parameters)
{
	using namespace mnet;

	TestTrue(TEXT("初始化网络子系统"), FSocketOps::EnsureNetworkInitialized());

	TSharedPtr<FBridgeTestState> State = MakeShared<FBridgeTestState>();
	State->Bridge = MakeShared<FMuduoNetBridge>(EDispatchMode::Inline);

	State->Payload.SetNumUninitialized(128);
	for (int32 Index = 0; Index < State->Payload.Num(); ++Index)
	{
		State->Payload[Index] = static_cast<uint8>((Index * 11) & 0xFF);
	}

	State->Bridge->SetConnectionEvent([State](int64 EndpointId, bool bConnected)
	{
		if (EndpointId >= FMuduoNetBridge::kClientIdOffset)
		{
			if (bConnected)
			{
				State->bClientConnected.store(true);
			}
			else
			{
				State->bClientDisconnected.store(true);
			}
			return;
		}

		if (bConnected)
		{
			State->bServerSawConnection.store(true);
		}
	});

	State->Bridge->SetMessageEvent([State](int64 EndpointId, const TArray<uint8>& Message)
	{
		if (EndpointId < FMuduoNetBridge::kClientIdOffset)
		{
			// 服务端：原样回显
			State->ServerMessages.fetch_add(1);
			State->Bridge->SendToServerConnection(EndpointId, Message);
			return;
		}

		// 客户端：校验回显内容
		State->ClientMessages.fetch_add(1);
		State->EchoedBytes.fetch_add(Message.Num());
		const bool bMatches = Message.Num() == State->Payload.Num() &&
			(Message.Num() == 0 || FMemory::Memcmp(Message.GetData(), State->Payload.GetData(), Message.Num()) == 0);
		if (!bMatches)
		{
			State->bPayloadMismatch.store(true);
		}
	});

	// ---- 服务端 ----
	TestTrue(TEXT("启动服务端（系统分配端口）"), State->Bridge->StartServer(0));
	TestTrue(TEXT("服务端端口有效"), State->Bridge->GetServerPort() != 0);
	TestTrue(TEXT("服务端处于运行状态"), State->Bridge->IsServerRunning());

	// ---- 客户端 ----
	const int64 ClientId = State->Bridge->ConnectToServer(TEXT("127.0.0.1"), State->Bridge->GetServerPort());
	TestTrue(TEXT("客户端 id 合法"), ClientId >= FMuduoNetBridge::kClientIdOffset);
	TestEqual(TEXT("客户端数量"), State->Bridge->GetClientCount(), static_cast<int32>(1));

	TestTrue(TEXT("客户端连接成功"), WaitUntilBridge([State]() { return State->bClientConnected.load(); }, 5.0));
	TestTrue(TEXT("服务端感知连接"), WaitUntilBridge([State]() { return State->bServerSawConnection.load(); }, 5.0));
	TestTrue(TEXT("客户端状态查询"), State->Bridge->IsClientConnected(ClientId));
	TestEqual(TEXT("服务端连接数"), State->Bridge->GetServerConnectionCount(), static_cast<int32>(1));

	// ---- 收发 ----
	TestTrue(TEXT("发送消息"), State->Bridge->SendFromClient(ClientId, State->Payload));
	TestTrue(TEXT("收到回显"), WaitUntilBridge([State]() { return State->ClientMessages.load() >= 1; }, 5.0));

	TestFalse(TEXT("回显内容一致"), State->bPayloadMismatch.load());
	TestEqual(TEXT("回显字节数"), State->EchoedBytes.load(), State->Payload.Num());
	TestEqual(TEXT("服务端处理的消息数"), State->ServerMessages.load(), static_cast<int32>(1));
	TestTrue(TEXT("统计：接收字节数"), State->Bridge->GetBytesReceived() >= State->Payload.Num());
	TestTrue(TEXT("统计：发送字节数"), State->Bridge->GetBytesSent() >= State->Payload.Num());

	// ---- 断开 ----
	State->Bridge->DisconnectClient(ClientId);
	TestTrue(TEXT("客户端感知断开"), WaitUntilBridge([State]() { return State->bClientDisconnected.load(); }, 5.0));
	TestTrue(TEXT("服务端连接数归零"), WaitUntilBridge([State]() { return State->Bridge->GetServerConnectionCount() == 0; }, 5.0));

	// ---- 停止 ----
	State->Bridge->StopServer();
	TestFalse(TEXT("服务端已停止"), State->Bridge->IsServerRunning());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
