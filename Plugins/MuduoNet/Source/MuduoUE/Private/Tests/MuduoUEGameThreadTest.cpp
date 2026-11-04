#include "CoreGlobals.h"
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Templates/Function.h"

#include "MuduoNetBridge.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using namespace mnet;

	struct FGameThreadTestState
	{
		TSharedPtr<FMuduoNetBridge> Bridge;
		TArray<uint8> Payload;
		int64 ClientId = 0;

		std::atomic<bool> bClientConnected{ false };
		std::atomic<int32> ClientMessages{ 0 };
		std::atomic<bool> bCallbackOnGameThread{ false };
		std::atomic<bool> bPayloadMismatch{ false };
		std::atomic<bool> bTimedOut{ false };
	};

	/** 跨帧等待条件成立；游戏线程每帧都会执行 latent 命令，因此 AsyncTask 的派发有机会落地。 */
	class FMuduoUEWaitFor final : public IAutomationLatentCommand
	{
	public:
		FMuduoUEWaitFor(TSharedPtr<FGameThreadTestState> InState, TFunction<bool()> InPredicate, double InTimeoutSeconds)
			: State(MoveTemp(InState))
			, Predicate(MoveTemp(InPredicate))
			, Deadline(FPlatformTime::Seconds() + InTimeoutSeconds)
		{
		}

		virtual bool Update() override
		{
			if (Predicate())
			{
				return true;
			}
			if (FPlatformTime::Seconds() >= Deadline)
			{
				if (State.IsValid())
				{
					State->bTimedOut.store(true);
				}
				return true;
			}
			return false;
		}

	private:
		TSharedPtr<FGameThreadTestState> State;
		TFunction<bool()> Predicate;
		double Deadline = 0.0;
	};

	/** 只执行一次的动作。 */
	class FMuduoUEAction final : public IAutomationLatentCommand
	{
	public:
		explicit FMuduoUEAction(TFunction<void()> InAction)
			: Action(MoveTemp(InAction))
		{
		}

		virtual bool Update() override
		{
			if (Action)
			{
				Action();
				Action = nullptr;
			}
			return true;
		}

	private:
		TFunction<void()> Action;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMuduoUEGameThreadDispatchTest, "MuduoNet.UE.GameThreadDispatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMuduoUEGameThreadDispatchTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FGameThreadTestState> State = MakeShared<FGameThreadTestState>();
	State->Bridge = MakeShared<FMuduoNetBridge>(EDispatchMode::GameThread);

	State->Payload.SetNumUninitialized(64);
	for (int32 Index = 0; Index < State->Payload.Num(); ++Index)
	{
		State->Payload[Index] = static_cast<uint8>((Index * 5) & 0xFF);
	}

	State->Bridge->SetConnectionEvent([State](int64 EndpointId, bool bConnected)
	{
		if (EndpointId >= FMuduoNetBridge::kClientIdOffset && bConnected)
		{
			State->bClientConnected.store(true);
		}
	});

	State->Bridge->SetMessageEvent([State](int64 EndpointId, const TArray<uint8>& Message)
	{
		// 关键断言：GameThread 模式下回调必须在游戏线程执行。
		State->bCallbackOnGameThread.store(IsInGameThread());

		if (EndpointId < FMuduoNetBridge::kClientIdOffset)
		{
			State->Bridge->SendToServerConnection(EndpointId, Message);
			return;
		}

		State->ClientMessages.fetch_add(1);
		const bool bMatches = Message.Num() == State->Payload.Num() &&
			(Message.Num() == 0 || FMemory::Memcmp(Message.GetData(), State->Payload.GetData(), Message.Num()) == 0);
		if (!bMatches)
		{
			State->bPayloadMismatch.store(true);
		}
	});

	TestTrue(TEXT("启动服务端"), State->Bridge->StartServer(0));

	State->ClientId = State->Bridge->ConnectToServer(TEXT("127.0.0.1"), State->Bridge->GetServerPort());
	TestTrue(TEXT("客户端 id 合法"), State->ClientId != 0);

	// 回调被投递到游戏线程队列，必须跨帧等待。
	ADD_LATENT_AUTOMATION_COMMAND(FMuduoUEWaitFor(State,
		[State]() { return State->bClientConnected.load(); }, 5.0));

	ADD_LATENT_AUTOMATION_COMMAND(FMuduoUEAction([State]()
	{
		State->Bridge->SendFromClient(State->ClientId, State->Payload);
	}));

	ADD_LATENT_AUTOMATION_COMMAND(FMuduoUEWaitFor(State,
		[State]() { return State->ClientMessages.load() >= 1; }, 5.0));

	ADD_LATENT_AUTOMATION_COMMAND(FMuduoUEAction([this, State]()
	{
		TestFalse(TEXT("没有等待超时"), State->bTimedOut.load());
		TestTrue(TEXT("回调运行在游戏线程"), State->bCallbackOnGameThread.load());
		TestEqual(TEXT("收到一条回显"), State->ClientMessages.load(), static_cast<int32>(1));
		TestFalse(TEXT("回显内容一致"), State->bPayloadMismatch.load());

		State->Bridge->DisconnectClient(State->ClientId);
		State->Bridge->StopServer();
	}));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
