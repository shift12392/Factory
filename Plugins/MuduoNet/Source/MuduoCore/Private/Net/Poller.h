#pragma once

#include "CoreMinimal.h"
#include "SocketHandle.h"
#include "Templates/UniquePtr.h"

namespace mnet
{
	class FChannel;

	/**
	 * IO 多路复用抽象。
	 *
	 * 实现：Linux -> epoll（LT），Windows -> WSAPoll（LT，Windows 没有边沿触发）。
	 * 约定：
	 * - 所有方法都必须在创建 poller 的线程（也就是 loop 线程）调用。
	 * - 不拥有 Channel，只保存裸指针。
	 */
	class MUDUOCORE_API FPoller
	{
	public:
		using FChannelList = TArray<FChannel*>;

		FPoller();
		virtual ~FPoller();

		FPoller(const FPoller&) = delete;
		FPoller& operator=(const FPoller&) = delete;

		/** 等待事件，超时返回空列表。 */
		virtual void Poll(int32 TimeoutMs, FChannelList& OutActiveChannels) = 0;
		virtual void UpdateChannel(FChannel* Channel) = 0;
		virtual void RemoveChannel(FChannel* Channel) = 0;

		bool HasChannel(const FChannel* Channel) const;

		void AssertInLoopThread() const;

		/** 按平台返回 EPollPoller / WSAPollPoller。 */
		static TUniquePtr<FPoller> CreateDefaultPoller();

	protected:
		void FillActiveChannel(FChannel* Channel, uint32 Revents, FChannelList& OutActiveChannels);

		TMap<FSocketValue, FChannel*> Channels;
		uint32 OwningThreadId = 0;
	};
}
