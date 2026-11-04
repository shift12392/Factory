#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Templates/Function.h"

#include "EventLoop.h"
#include "EventLoopThread.h"
#include "LengthHeaderCodec.h"
#include "TcpClient.h"
#include "TcpServer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace mnet;

	bool WaitUntilTcp(const TFunction<bool()>& Predicate, double TimeoutSeconds)
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

	/** 两个 loop 线程共享的测试状态，所有跨线程字段都是原子量。 */
	struct FEchoTestState
	{
		TArray<TArray<uint8>> ExpectedMessages;

		std::atomic<bool> bServerReady{ false };
		std::atomic<bool> bClientConnected{ false };
		std::atomic<bool> bClientDisconnected{ false };
		std::atomic<bool> bServerSawConnect{ false };
		std::atomic<bool> bServerSawDisconnect{ false };
		std::atomic<bool> bPayloadMismatch{ false };
		std::atomic<int32> ClientMessagesReceived{ 0 };
		std::atomic<int32> ServerMessagesReceived{ 0 };
		std::atomic<int32> ClientBytesReceived{ 0 };
		std::atomic<int32> CleanupDone{ 0 };

		uint16 Port = 0;

		TUniquePtr<FTcpServer> Server;
		TUniquePtr<FTcpClient> Client;
		TUniquePtr<FLengthHeaderCodec> ServerCodec;
		TUniquePtr<FLengthHeaderCodec> ClientCodec;

		int32 GetTotalBytes() const
		{
			int32 Total = 0;
			for (const TArray<uint8>& Message : ExpectedMessages)
			{
				Total += Message.Num();
			}
			return Total;
		}
	};

	bool RunEchoScenario(FAutomationTestBase& Test, int32 ServerThreadNum, const TCHAR* ScenarioName)
	{
		TSharedPtr<FEchoTestState> State = MakeShared<FEchoTestState>();

		// 16 字节、300 字节、256KB：覆盖小包粘包、跨读边界与大包分片回写。
		const int32 MessageSizes[] = { 16, 300, 256 * 1024 };
		for (int32 MessageIndex = 0; MessageIndex < UE_ARRAY_COUNT(MessageSizes); ++MessageIndex)
		{
			TArray<uint8> Message;
			Message.SetNumUninitialized(MessageSizes[MessageIndex]);
			for (int32 Index = 0; Index < Message.Num(); ++Index)
			{
				Message[Index] = static_cast<uint8>(((Index + MessageIndex) * 13) & 0xFF);
			}
			State->ExpectedMessages.Add(MoveTemp(Message));
		}

		FEventLoopThread ServerLoopThread(FString::Printf(TEXT("MuduoNet M2 Server %s"), ScenarioName));
		FEventLoopThread ClientLoopThread(FString::Printf(TEXT("MuduoNet M2 Client %s"), ScenarioName));

		FEventLoop* ServerLoop = ServerLoopThread.StartLoop();
		FEventLoop* ClientLoop = ClientLoopThread.StartLoop();

		Test.TestTrue(*FString::Printf(TEXT("%s: 服务端 loop 启动"), ScenarioName), ServerLoop != nullptr);
		Test.TestTrue(*FString::Printf(TEXT("%s: 客户端 loop 启动"), ScenarioName), ClientLoop != nullptr);
		if (ServerLoop == nullptr || ClientLoop == nullptr)
		{
			return false;
		}

		// ---- 服务端：监听 + 长度头拆包 + 原样回显 ----
		ServerLoop->RunInLoop([State, ServerLoop, ServerThreadNum]()
		{
			State->Server = MakeUnique<FTcpServer>(ServerLoop, FInetAddress::Loopback(0), TEXT("MuduoNetM2Echo"));
			State->Server->SetThreadNum(ServerThreadNum);

			State->ServerCodec = MakeUnique<FLengthHeaderCodec>(
				[State](const FTcpConnectionPtr& Conn, const TArray<uint8>& Message, double)
				{
					State->ServerMessagesReceived.fetch_add(1);
					State->ServerCodec->Send(Conn, Message);
				});

			State->Server->SetMessageCallback([State](const FTcpConnectionPtr& Conn, FBuffer* Buffer, double ReceiveTime)
			{
				State->ServerCodec->OnMessage(Conn, Buffer, ReceiveTime);
			});

			State->Server->SetConnectionCallback([State](const FTcpConnectionPtr& Conn)
			{
				if (Conn->IsConnected())
				{
					State->bServerSawConnect.store(true);
				}
				else
				{
					State->bServerSawDisconnect.store(true);
				}
			});

			State->Server->Start();
			State->Port = State->Server->GetListenAddress().GetPort();
			State->bServerReady.store(true);
		});

		Test.TestTrue(*FString::Printf(TEXT("%s: 服务端就绪"), ScenarioName),
			WaitUntilTcp([State]() { return State->bServerReady.load(); }, 5.0));
		Test.TestTrue(*FString::Printf(TEXT("%s: 已分配监听端口"), ScenarioName), State->Port != 0);

		// ---- 客户端：连接成功后连发三条消息 ----
		ClientLoop->RunInLoop([State, ClientLoop]()
		{
			State->Client = MakeUnique<FTcpClient>(
				ClientLoop,
				FInetAddress::FromIpPort(TEXT("127.0.0.1"), State->Port),
				TEXT("MuduoNetM2Client"));

			State->ClientCodec = MakeUnique<FLengthHeaderCodec>(
				[State](const FTcpConnectionPtr&, const TArray<uint8>& Message, double)
				{
					const int32 Index = State->ClientMessagesReceived.fetch_add(1);
					State->ClientBytesReceived.fetch_add(Message.Num());

					const bool bMatches =
						State->ExpectedMessages.IsValidIndex(Index) &&
						State->ExpectedMessages[Index].Num() == Message.Num() &&
						(Message.Num() == 0 ||
							FMemory::Memcmp(State->ExpectedMessages[Index].GetData(), Message.GetData(), Message.Num()) == 0);

					if (!bMatches)
					{
						State->bPayloadMismatch.store(true);
					}
				});

			State->Client->SetMessageCallback([State](const FTcpConnectionPtr& Conn, FBuffer* Buffer, double ReceiveTime)
			{
				State->ClientCodec->OnMessage(Conn, Buffer, ReceiveTime);
			});

			State->Client->SetConnectionCallback([State](const FTcpConnectionPtr& Conn)
			{
				if (Conn->IsConnected())
				{
					State->bClientConnected.store(true);
					for (const TArray<uint8>& Message : State->ExpectedMessages)
					{
						State->ClientCodec->Send(Conn, Message);
					}
				}
				else
				{
					State->bClientDisconnected.store(true);
				}
			});

			State->Client->Connect();
		});

		const int32 ExpectedCount = State->ExpectedMessages.Num();
		const int32 ExpectedBytes = State->GetTotalBytes();

		Test.TestTrue(*FString::Printf(TEXT("%s: 客户端连接成功"), ScenarioName),
			WaitUntilTcp([State]() { return State->bClientConnected.load(); }, 5.0));
		Test.TestTrue(*FString::Printf(TEXT("%s: 服务端感知到连接"), ScenarioName),
			WaitUntilTcp([State]() { return State->bServerSawConnect.load(); }, 5.0));

		Test.TestTrue(*FString::Printf(TEXT("%s: 收到全部 %d 条回显"), ScenarioName, ExpectedCount),
			WaitUntilTcp([State, ExpectedCount]() { return State->ClientMessagesReceived.load() >= ExpectedCount; }, 10.0));
		Test.TestTrue(*FString::Printf(TEXT("%s: 服务端处理了全部消息"), ScenarioName),
			WaitUntilTcp([State, ExpectedCount]() { return State->ServerMessagesReceived.load() >= ExpectedCount; }, 10.0));

		Test.TestFalse(*FString::Printf(TEXT("%s: 回显内容与顺序一致"), ScenarioName), State->bPayloadMismatch.load());
		Test.TestEqual(*FString::Printf(TEXT("%s: 回显总字节数"), ScenarioName), State->ClientBytesReceived.load(), ExpectedBytes);

		// ---- 半关闭：客户端 shutdown write，两端都应感知断开 ----
		ClientLoop->RunInLoop([State]()
		{
			if (State->Client.IsValid())
			{
				State->Client->Disconnect();
			}
		});

		Test.TestTrue(*FString::Printf(TEXT("%s: 客户端感知断开"), ScenarioName),
			WaitUntilTcp([State]() { return State->bClientDisconnected.load(); }, 5.0));
		Test.TestTrue(*FString::Printf(TEXT("%s: 服务端感知断开"), ScenarioName),
			WaitUntilTcp([State]() { return State->bServerSawDisconnect.load(); }, 5.0));

		// ---- 清理：对象必须在各自 loop 线程销毁 ----
		ClientLoop->RunInLoop([State]()
		{
			State->Client.Reset();
			State->ClientCodec.Reset();
			State->CleanupDone.fetch_add(1);
		});
		ServerLoop->RunInLoop([State]()
		{
			State->Server.Reset();
			State->ServerCodec.Reset();
			State->CleanupDone.fetch_add(1);
		});

		Test.TestTrue(*FString::Printf(TEXT("%s: 两个 loop 完成清理"), ScenarioName),
			WaitUntilTcp([State]() { return State->CleanupDone.load() >= 2; }, 5.0));

		ClientLoopThread.Stop();
		ServerLoopThread.Stop();

		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMuduoTcpServerClientTest, "MuduoNet.Core.TcpServerClient",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMuduoTcpServerClientTest::RunTest(const FString& Parameters)
{
	bool bAllSucceeded = true;

	// 单 loop（所有连接都在 base loop）
	bAllSucceeded &= RunEchoScenario(*this, 0, TEXT("单IO线程"));

	// 多 loop（2 个 IO 线程，验证 acceptor -> io loop 的连接移交）
	bAllSucceeded &= RunEchoScenario(*this, 2, TEXT("双IO线程"));

	return bAllSucceeded;
}

#endif // WITH_DEV_AUTOMATION_TESTS
