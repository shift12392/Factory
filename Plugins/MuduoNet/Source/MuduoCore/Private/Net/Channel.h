#pragma once

#include "ChannelTie.h"
#include "CoreMinimal.h"
#include "SocketHandle.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

namespace mnet
{
	class FPoller;
	class FEventLoop;

	/**
	 * 一个可被 poller 监听的事件通道（muduo Channel 的移植）。
	 *
	 * 约束：
	 * - 不拥有 fd，fd 的生命周期由调用方管理。
	 * - 除 Tie/回调设置外，所有操作都必须在所属 poller 的线程里调用。
	 * - Tie 用弱引用守护宿主对象，防止 handleEvent 过程中宿主被析构。
	 */
	class MUDUOCORE_API FChannel
	{
	public:
		enum EEvent : uint32
		{
			NoneEvent = 0,
			ReadEvent = 1u << 0,
			WriteEvent = 1u << 1,
			ErrorEvent = 1u << 2,
			HangUpEvent = 1u << 3,
		};

		FChannel(FPoller* InPoller, FSocketValue InFd);
		/** 推荐用法：绑定到所属 EventLoop，注册/注销会带上线程断言。 */
		FChannel(FEventLoop* InLoop, FSocketValue InFd);
		~FChannel();

		FChannel(const FChannel&) = delete;
		FChannel& operator=(const FChannel&) = delete;

		void SetReadCallback(TFunction<void()> InCallback);
		void SetWriteCallback(TFunction<void()> InCallback);
		void SetCloseCallback(TFunction<void()> InCallback);
		void SetErrorCallback(TFunction<void()> InCallback);

		void Tie(const TWeakPtr<FChannelTieTarget>& InTarget);

		FSocketValue GetFd() const { return Fd; }
		uint32 GetEvents() const { return Events; }
		uint32 GetRevents() const { return Revents; }
		void SetRevents(uint32 InRevents) { Revents = InRevents; }

		/** poller 内部索引：<0 表示未注册。语义由具体 poller 决定，调用方不要依赖具体数值。 */
		int32 GetIndex() const { return Index; }
		void SetIndex(int32 InIndex) { Index = InIndex; }

		bool IsNoneEvent() const { return Events == NoneEvent; }
		bool IsWriting() const { return (Events & WriteEvent) != 0; }
		bool IsReading() const { return (Events & ReadEvent) != 0; }

		void EnableReading();
		void DisableReading();
		void EnableWriting();
		void DisableWriting();
		void DisableAll();

		void HandleEvent();
		void RemoveFromPoller();

	private:
		void Update();

		FEventLoop* Loop = nullptr;
		FPoller* Poller = nullptr;
		FSocketValue Fd = InvalidSocketValue;
		uint32 Events = NoneEvent;
		uint32 Revents = NoneEvent;
		int32 Index = -1;
		bool bEventHandling = false;

		TWeakPtr<FChannelTieTarget> TieTarget;
		TFunction<void()> ReadCallback;
		TFunction<void()> WriteCallback;
		TFunction<void()> CloseCallback;
		TFunction<void()> ErrorCallback;
	};
}
