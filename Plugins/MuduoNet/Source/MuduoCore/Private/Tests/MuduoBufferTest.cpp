#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Buffer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMuduoBufferTest, "MuduoNet.Core.Buffer",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMuduoBufferTest::RunTest(const FString& Parameters)
{
	using namespace mnet;

	FBuffer Buffer;
	TestEqual(TEXT("初始可读字节数为 0"), Buffer.ReadableBytes(), static_cast<int32>(0));
	TestEqual(TEXT("初始可写字节数为初始容量"), Buffer.WritableBytes(), FBuffer::kInitialSize);
	TestEqual(TEXT("初始 prepend 空间"), Buffer.PrependableBytes(), FBuffer::kCheapPrepend);

	const uint8 Payload[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	Buffer.Append(Payload, 10);
	TestEqual(TEXT("写入后可读字节数"), Buffer.ReadableBytes(), static_cast<int32>(10));
	TestTrue(TEXT("peek 内容正确"), FMemory::Memcmp(Buffer.Peek(), Payload, 10) == 0);

	Buffer.Retrieve(4);
	TestEqual(TEXT("读取 4 字节后可读"), Buffer.ReadableBytes(), static_cast<int32>(6));
	TestEqual(TEXT("读取 4 字节后 prepend 空间"), Buffer.PrependableBytes(), FBuffer::kCheapPrepend + 4);

	Buffer.PrependInt32(0x01020304);
	TestEqual(TEXT("prepend 后可读"), Buffer.ReadableBytes(), static_cast<int32>(10));
	TestEqual(TEXT("按大端读回 int32"), Buffer.ReadInt32(), static_cast<int32>(0x01020304));
	TestEqual(TEXT("读回后剩余可读"), Buffer.ReadableBytes(), static_cast<int32>(6));

	Buffer.Retrieve(3);
	TArray<uint8> BigChunk;
	BigChunk.SetNumZeroed(4096);
	for (int32 Index = 0; Index < BigChunk.Num(); ++Index)
	{
		BigChunk[Index] = static_cast<uint8>(Index & 0xFF);
	}
	Buffer.Append(BigChunk.GetData(), BigChunk.Num());

	TestEqual(TEXT("大块写入后可读字节数"), Buffer.ReadableBytes(), 3 + BigChunk.Num());
	TestTrue(TEXT("大块写入后前 3 字节未被破坏"), FMemory::Memcmp(Buffer.Peek(), Payload + 7, 3) == 0);
	TestTrue(TEXT("大块写入后内容正确"), FMemory::Memcmp(Buffer.Peek() + 3, BigChunk.GetData(), BigChunk.Num()) == 0);

	const TArray<uint8> All = Buffer.RetrieveAllAsArray();
	TestEqual(TEXT("取走全部后可读为 0"), Buffer.ReadableBytes(), static_cast<int32>(0));
	TestEqual(TEXT("取走全部的长度"), All.Num(), 3 + BigChunk.Num());

	// prepend 只能使用预留的 8 字节；后 prepend 的数据排在前面。
	Buffer.PrependInt32(0x11223344);
	Buffer.PrependInt32(0x55667788);
	TestEqual(TEXT("prepend 两个 int32 后可读"), Buffer.ReadableBytes(), static_cast<int32>(8));
	TestEqual(TEXT("后 prepend 的在前"), Buffer.ReadInt32(), static_cast<int32>(0x55667788));
	TestEqual(TEXT("先 prepend 的在后"), Buffer.ReadInt32(), static_cast<int32>(0x11223344));
	TestEqual(TEXT("读完后回到初始位置"), Buffer.PrependableBytes(), FBuffer::kCheapPrepend);

	// 制造空洞后再写入超过剩余可写空间的数据，强制走 MakeSpace 的搬移分支。
	FBuffer Reuse;
	TArray<uint8> First;
	First.SetNumZeroed(100);
	for (int32 Index = 0; Index < First.Num(); ++Index)
	{
		First[Index] = static_cast<uint8>(Index);
	}
	Reuse.Append(First.GetData(), First.Num());
	Reuse.Retrieve(50);
	TestEqual(TEXT("消费后可读"), Reuse.ReadableBytes(), static_cast<int32>(50));

	TArray<uint8> Second;
	Second.SetNumZeroed(2000);
	for (int32 Index = 0; Index < Second.Num(); ++Index)
	{
		Second[Index] = static_cast<uint8>((Index * 3) & 0xFF);
	}
	Reuse.Append(Second.GetData(), Second.Num());

	TestEqual(TEXT("搬移后可读字节数"), Reuse.ReadableBytes(), 50 + Second.Num());
	TestTrue(TEXT("搬移后保留原可读数据"), FMemory::Memcmp(Reuse.Peek(), First.GetData() + 50, 50) == 0);
	TestTrue(TEXT("搬移后新数据正确"), FMemory::Memcmp(Reuse.Peek() + 50, Second.GetData(), Second.Num()) == 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
