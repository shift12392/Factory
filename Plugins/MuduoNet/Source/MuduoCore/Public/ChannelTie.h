#pragma once

#include "CoreMinimal.h"

namespace mnet
{
	/**
	 * 可以被 Channel 通过 Tie() 守护的宿主对象。
	 *
	 * Channel 只持有弱引用，HandleEvent 期间 Pin 成强引用，
	 * 避免「回调里把宿主从连接表移除导致宿主析构，而事件栈还在用它」。
	 */
	class MUDUOCORE_API FChannelTieTarget
	{
	public:
		virtual ~FChannelTieTarget() = default;
	};
}
