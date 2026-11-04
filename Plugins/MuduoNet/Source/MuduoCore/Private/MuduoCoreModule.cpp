#include "Modules/ModuleManager.h"

#include "SocketOps.h"

namespace mnet
{
	/**
	 * MuduoCore 模块入口。
	 * Windows 上在这里做一次 WSAStartup（可重复调用），Linux 上忽略 SIGPIPE；
	 * 插件本身不依赖 Sockets 模块，也不修改引擎源码。
	 */
	class FMuduoCoreModule final : public IModuleInterface
	{
	public:
		virtual void StartupModule() override
		{
			bNetworkInitialized = FSocketOps::EnsureNetworkInitialized();
		}

		virtual void ShutdownModule() override
		{
			if (bNetworkInitialized)
			{
				FSocketOps::ShutdownNetwork();
				bNetworkInitialized = false;
			}
		}

	private:
		bool bNetworkInitialized = false;
	};
}

IMPLEMENT_MODULE(mnet::FMuduoCoreModule, MuduoCore)
