#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"

#include "Async/Async.h"
#include "Net/Channel.h"
#include "Net/Poller.h"
#include "Platform/Wakeup.h"
#include "SocketOps.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMuduoPollerTest, "MuduoNet.Core.Poller",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMuduoPollerTest::RunTest(const FString& Parameters)
{
	using namespace mnet;

	TestTrue(TEXT("网络子系统初始化"), FSocketOps::EnsureNetworkInitialized());

	TUniquePtr<FPoller> Poller = FPoller::CreateDefaultPoller();
	TestTrue(TEXT("创建 Poller"), Poller.IsValid());
	if (!Poller.IsValid())
	{
		return false;
	}

	// ---- 1. 空轮询应当按超时返回 ----
	{
		FPoller::FChannelList ActiveChannels;
		const double StartTime = FPlatformTime::Seconds();
		Poller->Poll(50, ActiveChannels);
		const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

		TestEqual(TEXT("超时没有事件"), ActiveChannels.Num(), static_cast<int32>(0));
		TestTrue(FString::Printf(TEXT("等待时长应接近 50ms（实际 %.1fms）"), ElapsedMs), ElapsedMs >= 30.0);
	}

	// ---- 2. 唤醒源能打断 poll ----
	{
		FWakeup Wakeup;
		TestTrue(TEXT("创建唤醒源"), Wakeup.IsValid());

		if (Wakeup.IsValid())
		{
			FChannel WakeupChannel(Poller.Get(), Wakeup.GetReadHandle());
			bool bWokenUp = false;
			WakeupChannel.SetReadCallback([&Wakeup, &bWokenUp]()
			{
				bWokenUp = true;
				Wakeup.Drain();
			});
			WakeupChannel.EnableReading();

			TFuture<void> Notifier = Async(EAsyncExecution::Thread, [&Wakeup]()
			{
				FPlatformProcess::Sleep(0.05f);
				Wakeup.Notify();
			});

			FPoller::FChannelList ActiveChannels;
			const double StartTime = FPlatformTime::Seconds();
			Poller->Poll(2000, ActiveChannels);
			const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

			for (FChannel* Channel : ActiveChannels)
			{
				Channel->HandleEvent();
			}
			Notifier.Wait();

			TestTrue(TEXT("收到唤醒回调"), bWokenUp);
			TestTrue(FString::Printf(TEXT("唤醒应及时返回（实际 %.1fms）"), ElapsedMs), ElapsedMs < 1000.0);

			WakeupChannel.DisableAll();
			WakeupChannel.RemoveFromPoller();
		}
	}

	// ---- 3. TCP：非阻塞连接 -> 可写 -> accept -> 可读 ----
	{
		FSocketHandle ListenSocket = FSocketOps::CreateTcp(false);
		TestTrue(TEXT("创建监听 socket"), ListenSocket.IsValid());
		TestTrue(TEXT("绑定回环端口"), FSocketOps::Bind(ListenSocket.Get(), FInetAddress::Loopback(0)));
		TestTrue(TEXT("开始监听"), FSocketOps::Listen(ListenSocket.Get(), 8));

		const FInetAddress BoundAddress = FSocketOps::GetLocalAddress(ListenSocket.Get());
		TestTrue(TEXT("取得监听地址"), BoundAddress.IsValid());

		FChannel ListenChannel(Poller.Get(), ListenSocket.Get());
		ListenChannel.EnableReading();

		FSocketHandle ClientSocket = FSocketOps::CreateTcp(false);
		TestTrue(TEXT("创建客户端 socket"), ClientSocket.IsValid());

		const bool bConnectedImmediately = FSocketOps::Connect(ClientSocket.Get(), BoundAddress);
		if (!bConnectedImmediately)
		{
			const ESocketError ConnectError = FSocketOps::GetLastError();
			TestTrue(FString::Printf(TEXT("非阻塞 connect 应处于进行中（实际 %s）"), *FSocketOps::ErrorToString(ConnectError)),
				ConnectError == ESocketError::InProgress || ConnectError == ESocketError::TryAgain || ConnectError == ESocketError::AlreadyInProgress);

			FChannel ClientChannel(Poller.Get(), ClientSocket.Get());
			bool bWritable = false;
			ClientChannel.SetWriteCallback([&bWritable]()
			{
				bWritable = true;
			});
			ClientChannel.EnableWriting();

			FPoller::FChannelList ActiveChannels;
			Poller->Poll(3000, ActiveChannels);
			for (FChannel* Channel : ActiveChannels)
			{
				Channel->HandleEvent();
			}

			TestTrue(TEXT("连接完成后客户端可写"), bWritable);

			ClientChannel.DisableAll();
			ClientChannel.RemoveFromPoller();
		}

		TestEqual(TEXT("连接建立后 SO_ERROR 为 0"), FSocketOps::GetSocketError(ClientSocket.Get()), static_cast<int32>(0));

		FInetAddress PeerAddress;
		FSocketHandle ServerSocket;
		{
			FPoller::FChannelList ActiveChannels;
			Poller->Poll(3000, ActiveChannels);
			for (FChannel* Channel : ActiveChannels)
			{
				Channel->HandleEvent();
			}
			ServerSocket = FSocketOps::Accept(ListenSocket.Get(), PeerAddress);
		}

		TestTrue(TEXT("accept 成功"), ServerSocket.IsValid());

		if (ServerSocket.IsValid())
		{
			FChannel ServerChannel(Poller.Get(), ServerSocket.Get());
			ServerChannel.EnableReading();

			const char Message[] = "muduo-net";
			const int32 MessageLength = static_cast<int32>(sizeof(Message) - 1);
			TestEqual(TEXT("发送 payload"), FSocketOps::Send(ClientSocket.Get(), Message, MessageLength), MessageLength);

			FPoller::FChannelList ActiveChannels;
			Poller->Poll(3000, ActiveChannels);
			TestTrue(TEXT("服务端收到可读事件"), ActiveChannels.Num() > 0);
			for (FChannel* Channel : ActiveChannels)
			{
				Channel->HandleEvent();
			}

			char Received[32] = { 0 };
			const int32 ReceivedBytes = FSocketOps::Recv(ServerSocket.Get(), Received, static_cast<int32>(sizeof(Received)));
			TestEqual(TEXT("收到字节数"), ReceivedBytes, MessageLength);
			TestTrue(TEXT("收到内容一致"), FMemory::Memcmp(Received, Message, MessageLength) == 0);

			// 半关闭：客户端 shutdown write 后，服务端应当收到可读事件并读到 0。
			FSocketOps::ShutdownWrite(ClientSocket.Get());
			FPoller::FChannelList CloseEvents;
			Poller->Poll(3000, CloseEvents);
			TestTrue(TEXT("半关闭后服务端仍有事件"), CloseEvents.Num() > 0);

			const int32 CloseRead = FSocketOps::Recv(ServerSocket.Get(), Received, static_cast<int32>(sizeof(Received)));
			TestEqual(TEXT("半关闭后读到 0"), CloseRead, static_cast<int32>(0));

			ServerChannel.DisableAll();
			ServerChannel.RemoveFromPoller();
		}

		ListenChannel.DisableAll();
		ListenChannel.RemoveFromPoller();
	}

	FSocketOps::ShutdownNetwork();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
