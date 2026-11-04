#include "EventLoopThread.h"

#include "EventLoop.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "MuduoNetLog.h"

namespace mnet
{
	/** EventLoop 的宿主：在 IO 线程栈上建 loop，线程退出时析构。 */
	class FEventLoopThread::FRunnableImpl final : public FRunnable
	{
	public:
		FRunnableImpl(const FThreadInitCallback& InInitCallback, FEvent* InReadyEvent, std::atomic<FEventLoop*>& InLoopPtr)
			: InitCallback(InInitCallback)
			, ReadyEvent(InReadyEvent)
			, LoopPtr(InLoopPtr)
		{
		}

		virtual bool Init() override { return true; }

		virtual uint32 Run() override
		{
			FEventLoop Loop;
			LoopPtr.store(&Loop);

			if (InitCallback)
			{
				InitCallback(&Loop);
			}

			if (ReadyEvent != nullptr)
			{
				ReadyEvent->Trigger();
			}

			Loop.Loop();

			LoopPtr.store(nullptr);
			return 0;
		}

		virtual void Stop() override
		{
			// FRunnableThread::Kill(true) 会先调用 Stop()，再等待线程退出。
			if (FEventLoop* Loop = LoopPtr.load())
			{
				Loop->Quit();
			}
		}

		virtual void Exit() override {}

	private:
		FThreadInitCallback InitCallback;
		FEvent* ReadyEvent = nullptr;
		std::atomic<FEventLoop*>& LoopPtr;
	};

	FEventLoopThread::FEventLoopThread(const FString& InThreadName)
		: ThreadName(InThreadName)
	{
	}

	FEventLoopThread::~FEventLoopThread()
	{
		Stop();
	}

	FEventLoop* FEventLoopThread::StartLoop(const FThreadInitCallback& InInitCallback)
	{
		checkf(Thread == nullptr && Runnable == nullptr, TEXT("MuduoNet: FEventLoopThread 不能重复启动（%s）"), *ThreadName);

		ReadyEvent = FPlatformProcess::GetSynchEventFromPool(true);
		Runnable = new FRunnableImpl(InInitCallback, ReadyEvent, LoopPtr);
		Thread = FRunnableThread::Create(Runnable, *ThreadName, 0, TPri_Normal);

		if (Thread == nullptr)
		{
			UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: 创建 IO 线程失败（%s）"), *ThreadName);
			delete Runnable;
			Runnable = nullptr;
			FPlatformProcess::ReturnSynchEventToPool(ReadyEvent);
			ReadyEvent = nullptr;
			return nullptr;
		}

		ReadyEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(ReadyEvent);
		ReadyEvent = nullptr;

		FEventLoop* Loop = LoopPtr.load();
		checkf(Loop != nullptr, TEXT("MuduoNet: IO 线程已退出（%s）"), *ThreadName);
		return Loop;
	}

	void FEventLoopThread::Stop()
	{
		if (Thread != nullptr)
		{
			// Kill(true) 内部调用 Runnable->Stop()（让 loop 退出）并等待线程结束。
			Thread->Kill(true);
			delete Thread;
			Thread = nullptr;
		}

		delete Runnable;
		Runnable = nullptr;
		LoopPtr.store(nullptr);
	}
}
