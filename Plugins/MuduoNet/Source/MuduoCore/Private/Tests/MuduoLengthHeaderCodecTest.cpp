#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Buffer.h"
#include "LengthHeaderCodec.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMuduoLengthHeaderCodecTest, "MuduoNet.Core.LengthHeaderCodec",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMuduoLengthHeaderCodecTest::RunTest(const FString& Parameters)
{
	using namespace mnet;

	TArray<TArray<uint8>> ReceivedMessages;
	FLengthHeaderCodec Codec([&ReceivedMessages](const FTcpConnectionPtr&, const TArray<uint8>& Message, double)
	{
		ReceivedMessages.Add(Message);
	});

	// 非法长度头会打一条 Warning，自动化框架默认把它当失败，这里显式声明预期。
	AddExpectedErrorPlain(TEXT("长度头非法"), EAutomationExpectedErrorFlags::Contains, 1);

	// ---- 编码：4 字节大端长度头 + 消息体 ----
	const uint8 PayloadA[] = { 1, 2, 3 };
	FBuffer Encoded;
	FLengthHeaderCodec::Encode(Encoded, PayloadA, 3);

	TestEqual(TEXT("编码后总长度"), Encoded.ReadableBytes(), static_cast<int32>(7));
	TestEqual(TEXT("长度头为 3"), Encoded.PeekInt32(), static_cast<int32>(3));
	TestEqual(TEXT("长度头占 4 字节"), Encoded.Peek()[FLengthHeaderCodec::kHeaderLength - 1], static_cast<uint8>(3));
	TestEqual(TEXT("头部后紧跟消息体"), Encoded.Peek()[FLengthHeaderCodec::kHeaderLength], PayloadA[0]);

	// ---- 解码：两条消息 + 半包 ----
	const uint8 PayloadB[] = { 9, 8, 7, 6, 5 };
	FBuffer Input;
	FLengthHeaderCodec::Encode(Input, PayloadB, 5);
	FLengthHeaderCodec::Encode(Input, PayloadA, 3);

	// 先只给 6 字节：连第一条消息都不完整
	FBuffer Partial;
	Partial.Append(Input.Peek(), 6);
	Codec.OnMessage(nullptr, &Partial, 0.0);
	TestEqual(TEXT("半包不触发回调"), ReceivedMessages.Num(), static_cast<int32>(0));
	TestEqual(TEXT("半包数据保留在缓冲区"), Partial.ReadableBytes(), static_cast<int32>(6));

	// 补齐剩余数据
	Partial.Append(Input.Peek() + 6, Input.ReadableBytes() - 6);
	Codec.OnMessage(nullptr, &Partial, 0.0);

	TestEqual(TEXT("两条完整消息都触发回调"), ReceivedMessages.Num(), static_cast<int32>(2));
	TestEqual(TEXT("第一条消息长度"), ReceivedMessages[0].Num(), static_cast<int32>(5));
	TestEqual(TEXT("第二条消息长度"), ReceivedMessages[1].Num(), static_cast<int32>(3));
	TestTrue(TEXT("第一条消息内容"), FMemory::Memcmp(ReceivedMessages[0].GetData(), PayloadB, 5) == 0);
	TestTrue(TEXT("第二条消息内容"), FMemory::Memcmp(ReceivedMessages[1].GetData(), PayloadA, 3) == 0);
	TestEqual(TEXT("缓冲区已被消费完"), Partial.ReadableBytes(), static_cast<int32>(0));

	// ---- 非法长度头：不应触发回调，也不应崩溃（Conn 为空时跳过 ForceClose）----
	FBuffer BadHeader;
	const uint8 NegativeHeader[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
	BadHeader.Append(NegativeHeader, 4);
	Codec.OnMessage(nullptr, &BadHeader, 0.0);
	TestEqual(TEXT("非法长度不触发回调"), ReceivedMessages.Num(), static_cast<int32>(2));

	// ---- 空消息也算合法帧 ----
	FBuffer EmptyMessage;
	FLengthHeaderCodec::Encode(EmptyMessage, nullptr, 0);
	Codec.OnMessage(nullptr, &EmptyMessage, 0.0);
	TestEqual(TEXT("空消息触发回调"), ReceivedMessages.Num(), static_cast<int32>(3));
	TestEqual(TEXT("空消息长度为 0"), ReceivedMessages[2].Num(), static_cast<int32>(0));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
