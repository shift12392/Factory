#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Templates/Function.h"

#include "EventLoop.h"
#include "EventLoopThread.h"
#include "Net/Channel.h"
#include "SocketOps.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace mnet;

	bool WaitUntilEcho(const TFunction<bool()>& Predicate, double TimeoutSeconds)
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

	/** 两个 IO 线程共享的状态；socket/channel 的创建与销毁都发生在各自 loop 线程。 */
	struct FMuduoEchoState
	{
		FSocketHandle ListenSocket;
		TUniquePtr<FChannel> ListenChannel;
		FSocketHandle ServerSocket;
		TUniquePtr<FChannel> ServerChannel;
		FSocketHandle ClientSocket;
		TUniquePtr<FChannel> ClientChannel;

		uint16 Port = 0;
		TArray<uint8> Payload;

		std::atomic<bool> bServerReady{ false };
		std::atomic<bool> bClientConnected{ false };
		std::atomic<int32> EchoedBytes{ 0 };
		std::atomic<int32> CleanupDone{ 0 };
	};

	void CleanupClientSide(const TSharedPtr<FMuduoEchoState>& State)
	{
		if (State->ClientChannel.IsValid())
		{
			State->ClientChannel->DisableAll();
			State->ClientChannel->RemoveFromPoller();
			State->ClientChannel.Reset();
		}
		State->ClientSocket.Reset();
		State->CleanupDone.fetch_add(1);
	}

	void CleanupServerSide(const TSharedPtr<FMuduoEchoState>& State)
	{
		if (State->ServerChannel.IsValid())
		{
			State->ServerChannel->DisableAll();
			State->ServerChannel->RemoveFromPoller();
			State->ServerChannel.Reset();
		}
		State->ServerSocket.Reset();

		if (State->ListenChannel.IsValid())
		{
			State->ListenChannel->DisableAll();
			State->ListenChannel->RemoveFromPoller();
			State->ListenChannel.Reset();
		}
		State->ListenSocket.Reset();
		State->CleanupDone.fetch_add(1);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMuduoEchoTest, "MuduoNet.Core.Echo",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMuduoEchoTest::RunTest(const FString& Parameters)
{
	using namespace mnet;

	TestTrue(TEXT("初始化网络子系统"), FSocketOps::EnsureNetworkInitialized());

	TSharedPtr<FMuduoEchoState> State = MakeShared<FMuduoEchoState>();
	State->Payload.SetNumUninitialized(64);
	for (int32 Index = 0; Index < State->Payload.Num(); ++Index)
	{
		State->Payload[Index] = static_cast<uint8>((Index * 7) & 0xFF);
	}

	FEventLoopThread ServerThread(TEXT("MuduoNet Echo Server"));
	FEventLoopThread ClientThread(TEXT("MuduoNet Echo Client"));

	FEventLoop* ServerLoop = ServerThread.StartLoop();
	FEventLoop* ClientLoop = ClientThread.StartLoop();

	TestTrue(TEXT("服务端 loop 启动"), ServerLoop != nullptr);
	TestTrue(TEXT("客户端 loop 启动"), ClientLoop != nullptr);
	if (ServerLoop == nullptr || ClientLoop == nullptr)
	{
		return false;
	}

	// ---- 服务端：监听 + accept + 回显 ----
	ServerLoop->RunInLoop([State, ServerLoop]()
	{
		State->ListenSocket = FSocketOps::CreateTcp(false);
		if (!State->ListenSocket.IsValid())
		{
			return;
		}

		FSocketOps::SetReuseAddr(State->ListenSocket.Get(), true);
		if (!FSocketOps::Bind(State->ListenSocket.Get(), FInetAddress::Loopback(0)))
		{
			return;
		}
		if (!FSocketOps::Listen(State->ListenSocket.Get(), 8))
		{
			return;
		}

		State->Port = FSocketOps::GetLocalAddress(State->ListenSocket.Get()).GetPort();

		State->ListenChannel = MakeUnique<FChannel>(ServerLoop, State->ListenSocket.Get());
		State->ListenChannel->SetReadCallback([State, ServerLoop]()
		{
			FInetAddress PeerAddress;
			FSocketHandle Accepted = FSocketOps::Accept(State->ListenSocket.Get(), PeerAddress);
			if (!Accepted.IsValid())
			{
				return;
			}

			State->ServerSocket = MoveTemp(Accepted);
			State->ServerChannel = MakeUnique<FChannel>(ServerLoop, State->ServerSocket.Get());
			State->ServerChannel->SetReadCallback([State]()
			{
				uint8 Buffer[256];
				const int32 Read = FSocketOps::Recv(State->ServerSocket.Get(), Buffer, static_cast<int32>(sizeof(Buffer)));
				if (Read > 0)
				{
					FSocketOps::Send(State->ServerSocket.Get(), Buffer, Read);
				}
			});
			State->ServerChannel->EnableReading();
		});
		State->ListenChannel->EnableReading();
		State->bServerReady.store(true);
	});

	TestTrue(TEXT("服务端就绪"), WaitUntilEcho([State]() { return State->bServerReady.load(); }, 3.0));
	TestTrue(TEXT("系统已分配端口"), State->Port != 0);

	// ---- 客户端：连接 + 发送 + 收 echo ----
	ClientLoop->RunInLoop([State, ClientLoop]()
	{
		State->ClientSocket = FSocketOps::CreateTcp(false);
		if (!State->ClientSocket.IsValid())
		{
			return;
		}

		const bool bConnected = FSocketOps::Connect(
			State->ClientSocket.Get(),
			FInetAddress::FromIpPort(TEXT("127.0.0.1"), State->Port));

		State->ClientChannel = MakeUnique<FChannel>(ClientLoop, State->ClientSocket.Get());

		auto SendPayload = [State]()
		{
			FSocketOps::Send(State->ClientSocket.Get(), State->Payload.GetData(), State->Payload.Num());
		};

		auto StartReading = [State]()
		{
			State->ClientChannel->SetReadCallback([State]()
			{
				uint8 Buffer[256];
				const int32 Read = FSocketOps::Recv(State->ClientSocket.Get(), Buffer, static_cast<int32>(sizeof(Buffer)));
				if (Read > 0)
				{
					State->EchoedBytes.fetch_add(Read);
				}
			});
			State->ClientChannel->EnableReading();
		};

		if (bConnected)
		{
			State->bClientConnected.store(true);
			SendPayload();
			StartReading();
		}
		else
		{
			State->ClientChannel->SetWriteCallback([State, SendPayload, StartReading]()
			{
				State->ClientChannel->DisableWriting();
				State->bClientConnected.store(true);
				SendPayload();
				StartReading();
			});
			State->ClientChannel->EnableWriting();
		}
	});

	TestTrue(TEXT("客户端完成连接"), WaitUntilEcho([State]() { return State->bClientConnected.load(); }, 3.0));
	TestTrue(TEXT("收到完整回显"), WaitUntilEcho([State]() { return State->EchoedBytes.load() >= State->Payload.Num(); }, 5.0));

	// ---- 清理：各自在所属 loop 线程执行 ----
	ClientLoop->RunInLoop([State]() { CleanupClientSide(State); });
	ServerLoop->RunInLoop([State]() { CleanupServerSide(State); });
	TestTrue(TEXT("两个 loop 都完成清理"), WaitUntilEcho([State]() { return State->CleanupDone.load() >= 2; }, 3.0));

	ClientThread.Stop();
	ServerThread.Stop();

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
