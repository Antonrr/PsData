// Copyright 2021-2022 dawg.team. All Rights Reserved.

#include "PsNetworkData.h"

#include "PsDataAPI.h"
#include "Serialize/PsDataOptimizedBinarySerialization.h"

#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

#include <string>

using namespace PsDataTools;

namespace PsDataTools
{

void StringToBuffer(const FString& String, FPsNetworkByteBuffer& OutBuffer)
{
	OutBuffer.Buffer.Reset();
	const auto Converter = FTCHARToUTF8(*String, String.Len());
	OutBuffer.Buffer.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
}

void BufferToString(const FPsNetworkByteBuffer& Buffer, FString& OutString)
{
	OutString.Reset();
	const auto Converter = FUTF8ToTCHAR(reinterpret_cast<const char*>(Buffer.Buffer.GetData()), Buffer.Buffer.Num());
	OutString.AppendChars(Converter.Get(), Converter.Length());
}

FString DecodePath(const TArray<uint16>& EncodedPath, const TArray<FString>& Dictionary)
{
	FString Result;

	for (int32 i = 0; i < EncodedPath.Num(); ++i)
	{
		const auto Index = EncodedPath[i];
		check(Dictionary.IsValidIndex(Index));

		if (i != 0)
		{
			Result.AppendChar('.');
		}
		Result.Append(Dictionary[Index]);
	}

	return Result;
}

TArray<uint16> EncodePath(const FString& DecodedPath, TArray<FString>& Dictionary)
{
	TArray<uint16> Result;

	TArray<FString> Strings;
	DecodedPath.ParseIntoArray(Strings, TEXT("."));

	for (int32 i = 0; i < Strings.Num(); ++i)
	{
		const auto String = Strings[i];
		auto Index = Dictionary.Find(String);
		if (Index == INDEX_NONE)
		{
			Index = Dictionary.Add(String);
		}

		check(TNumericLimits<uint16>::Max() >= Index);
		Result.Add(Index);
	}

	return Result;
}

} // namespace PsDataTools

/***********************************
 * FPsNetworkByteBuffer
 ***********************************/

FPsNetworkByteBuffer::FPsNetworkByteBuffer()
{
}

FPsNetworkByteBuffer::FPsNetworkByteBuffer(const TArray<uint8>& InBuffer)
	: Buffer(InBuffer)
{
}

bool FPsNetworkByteBuffer::Serialize(FArchive& Ar)
{
	Ar << *this;
	return true;
}

bool FPsNetworkByteBuffer::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << *this;

	bOutSuccess = true;
	return true;
}

FArchive& operator<<(FArchive& Ar, FPsNetworkByteBuffer& Value)
{
	return Ar << Value.Buffer;
}

void operator<<(FStructuredArchive::FSlot Slot, FPsNetworkByteBuffer& Value)
{
	Slot << Value.Buffer;
}

UE_IMPLEMENT_STRUCT("/Script/PsData", PsNetworkByteBuffer);

/***********************************
 * FPsNetworkEvent
 ***********************************/

FPsNetworkEvent::FPsNetworkEvent()
	: Type(EPsNetworkEventType::None)
{
}

FPsNetworkEvent::FPsNetworkEvent(EPsNetworkEventType InType, const TArray<uint16>& InPath, const TArray<uint8>& InBuffer)
	: Type(InType)
	, Path(InPath)
	, Data(InBuffer)
{
}

FPsNetworkEvent::FPsNetworkEvent(EPsNetworkEventType InType, const TArray<uint16>& InPath)
	: Type(InType)
	, Path(InPath)
{
	check(InType == EPsNetworkEventType::Removed);
}

bool FPsNetworkEvent::Serialize(FArchive& Ar)
{
	Ar << *this;
	return true;
}

bool FPsNetworkEvent::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << *this;

	bOutSuccess = true;
	return true;
}

FArchive& operator<<(FArchive& Ar, FPsNetworkEvent& Value)
{
	Ar << Value.Type;
	Ar << Value.Path;

	const bool bHasData = Value.Type != EPsNetworkEventType::Removed;
	if (bHasData)
	{
		Ar << Value.Data;
	}

	return Ar;
}

UE_IMPLEMENT_STRUCT("/Script/PsData", PsNetworkEvent);

/***********************************
 * APsDataNetworkActor
 ***********************************/

APsDataNetworkActor::APsDataNetworkActor()
	: State(EProxyState::Created)
	, NetworkData(nullptr)
{
	bReplicates = true;
	NetPriority = 10.f;
	RootComponent = CreateDefaultSubobject<USceneComponent>("RootComponent");
}

bool APsDataNetworkActor::IsAuthority() const
{
	if (const auto Controller = Cast<APlayerController>(GetOwner()))
	{
		return Controller->HasAuthority();
	}

	return false;
}

bool APsDataNetworkActor::IsConfirmed() const
{
	return State == EProxyState::Confirmed;
}

bool APsDataNetworkActor::IsSynchronized() const
{
	return State == EProxyState::Synchronized;
}

void APsDataNetworkActor::Open(UPsNetworkData* InNetworkData)
{
	NetworkData = InNetworkData;

	if (State == EProxyState::Closed)
	{
		return;
	}

	if (IsAuthority())
	{
		UE_LOG(LogDataNetwork, Display, TEXT("Server proxy opened"));
		State = EProxyState::Opened;
	}
	else
	{
		UE_LOG(LogDataNetwork, Display, TEXT("Client proxy opened"));
		State = EProxyState::Confirmed;
		Server_Confirm();
	}
}

void APsDataNetworkActor::Close()
{
	NetworkData = nullptr;

	if (State == EProxyState::Closed)
	{
		return;
	}

	if (IsAuthority())
	{
		UE_LOG(LogDataNetwork, Display, TEXT("Server proxy closed"));
	}
	else
	{
		UE_LOG(LogDataNetwork, Display, TEXT("Client proxy closed"));
	}

	State = EProxyState::Closed;
	Destroy();
}

void APsDataNetworkActor::Synchronize(const TArray<FString>& Dictionary, const FPsNetworkByteBuffer& Buffer)
{
	if (State == EProxyState::Closed)
	{
		return;
	}

	check(IsAuthority() && State == EProxyState::Confirmed);
	State = EProxyState::Synchronized;

	Client_Synchronize(MakeDictionaryForReplication(Dictionary), Buffer);
}

void APsDataNetworkActor::Send(const TArray<FString>& Dictionary, const TArray<FPsNetworkEvent>& Events)
{
	if (State == EProxyState::Closed)
	{
		return;
	}

	check(IsAuthority() && State == EProxyState::Synchronized);
	Client_Send(MakeDictionaryForReplication(Dictionary), Events);
}

TArray<FPsNetworkByteBuffer> APsDataNetworkActor::MakeDictionaryForReplication(const TArray<FString>& Dictionary)
{
	TArray<FPsNetworkByteBuffer> Buffers;
	const auto DictionarySize = Dictionary.Num();
	if (DictionarySize != LastDictionarySize)
	{
		FPsNetworkByteBuffer Buffer;
		for (int32 i = LastDictionarySize; i < DictionarySize; ++i)
		{
			StringToBuffer(Dictionary[i], Buffer);
			Buffers.Add(Buffer);
		}
		LastDictionarySize = DictionarySize;
	}
	return Buffers;
}

TArray<FString> APsDataNetworkActor::MakeDictionaryFromReplication(const TArray<FPsNetworkByteBuffer>& Dictionary) const
{
	TArray<FString> Strings;

	FString String;
	for (const auto& Buffer : Dictionary)
	{
		BufferToString(Buffer, String);
		Strings.Add(String);
	}
	return Strings;
}

void APsDataNetworkActor::Server_Confirm_Implementation()
{
	check(IsAuthority());
	UE_LOG(LogDataNetwork, Display, TEXT("Server proxy confirmed"));
	State = EProxyState::Confirmed;
	NetworkData->FlushRequest();
	NetworkData->SynchronizePromise.Resolve();
}

void APsDataNetworkActor::Client_Synchronize_Implementation(const TArray<FPsNetworkByteBuffer>& Dictionary, const FPsNetworkByteBuffer& Buffer)
{
	State = EProxyState::Synchronized;

	NetworkData->Synchronize(MakeDictionaryFromReplication(Dictionary), Buffer);
}

void APsDataNetworkActor::Client_Send_Implementation(const TArray<FPsNetworkByteBuffer>& Dictionary, const TArray<FPsNetworkEvent>& Events)
{
	check(State == EProxyState::Synchronized);
	NetworkData->Apply(MakeDictionaryFromReplication(Dictionary), Events);
}

/***********************************
 * UPsNetworkData
 ***********************************/

UPsNetworkData::UPsNetworkData()
	: NetUpdateFrequency(0.f)
	, bAccumulateEvents(true)
	, AccumulatedTime(0.f)
	, NumAuthorityProxies(0)
	, bForceFlush(false)
{
}

void UPsNetworkData::OpenConnection(APlayerController* Controller) const
{
	for (const auto Proxy : NetworkProxies)
	{
		if (Proxy->GetOwner() == Controller)
		{
			UE_LOG(LogDataNetwork, Fatal, TEXT("Attempt to add an already existing proxy"));
		}
	}

	for (const auto PendingController : PendingControllers)
	{
		if (PendingController == Controller)
		{
			UE_LOG(LogDataNetwork, Fatal, TEXT("Attempt to add an already existing proxy"));
		}
	}

	const auto bAuthority = Controller->HasAuthority();
	if (bAuthority)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = Controller;
		SpawnParameters.Name = *FString::Printf(TEXT("DataNetworkActor_%s"), *Controller->GetName());
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.bNoFail = true;
		const auto Proxy = Controller->GetWorld()->SpawnActor<APsDataNetworkActor>(SpawnParameters);
		NetworkProxies.Add(Proxy);
		Proxy->Open(const_cast<UPsNetworkData*>(this));

		++NumAuthorityProxies;
		FlushRequest();
	}
	else
	{
		MutableReset();
		PendingControllers.Add(Controller);
	}
}

void UPsNetworkData::CloseConnection(APlayerController* Controller) const
{
	const auto bAuthority = Controller->HasAuthority();
	if (bAuthority)
	{
		--NumAuthorityProxies;
	}
	else
	{
		MutableReset();
	}

	for (auto It = NetworkProxies.CreateIterator(); It; ++It)
	{
		const auto Proxy = *It;
		if (Proxy->GetOwner() == Controller)
		{
			Proxy->Close();
			It.RemoveCurrent();
			return;
		}
	}

	for (auto It = PendingControllers.CreateIterator(); It; ++It)
	{
		const auto PendingController = *It;
		if (PendingController == Controller)
		{
			It.RemoveCurrent();
			return;
		}
	}

	UE_LOG(LogDataNetwork, Fatal, TEXT("Attempt to remove a non-existing proxy"));
}

bool UPsNetworkData::HasAuthority() const
{
	return NumAuthorityProxies > 0;
}

void UPsNetworkData::FlushRequest() const
{
	bForceFlush = true;
}

void UPsNetworkData::Flush()
{
	bForceFlush = false;
	if (HasAuthority())
	{
		TArray<APsDataNetworkActor*> ConfirmedProxies;
		TArray<APsDataNetworkActor*> SynchronizedProxies;

		for (const auto NetworkProxy : NetworkProxies)
		{
			if (NetworkProxy->IsAuthority())
			{
				if (NetworkProxy->IsConfirmed())
				{
					ConfirmedProxies.Add(NetworkProxy);
				}

				if (NetworkEvents.Num() > 0)
				{
					if (NetworkProxy->IsSynchronized())
					{
						SynchronizedProxies.Add(NetworkProxy);
					}
				}
			}
		}

		if (ConfirmedProxies.Num() > 0)
		{
			const auto OutputBuffer = MakeShared<FPsDataBufferOutputStream>();
			FPsDataOptimizedBinarySerializer Serializer(OutputBuffer, &Dictionary);
			Serializer.bWriteDefaults = false;
			DataSerialize(&Serializer);

			const FPsNetworkByteBuffer Buffer(OutputBuffer->GetBuffer());
			for (const auto NetworkProxy : ConfirmedProxies)
			{
				NetworkProxy->Synchronize(Dictionary, Buffer);
			}
		}

		for (const auto NetworkProxy : SynchronizedProxies)
		{
			NetworkProxy->Send(Dictionary, NetworkEvents);
		}
	}

	NetworkEvents.Reset();
}

FPsDataSimplePromise& UPsNetworkData::OnSynchronizePromise() const
{
	return SynchronizePromise;
}

void UPsNetworkData::Tick(float DeltaTime)
{
	if (HasAuthority())
	{
		auto MinDeltaTime = 0.f;
		if (NetUpdateFrequency > SMALL_NUMBER)
		{
			MinDeltaTime = 1.f / NetUpdateFrequency;
		}

		AccumulatedTime += DeltaTime;
		if (bForceFlush || AccumulatedTime >= MinDeltaTime)
		{
			Flush();
			AccumulatedTime = 0.f;
		}
	}

	HandlingControllers();
}

TStatId UPsNetworkData::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPsNetworkData, STATGROUP_Tickables);
}

void UPsNetworkData::CommitChanges(const UPsData* Data, const FDataField* Field)
{
	if (!Field->Context->IsData() && !Field->Meta.bHidden)
	{
		const auto OutputBuffer = MakeShared<FPsDataBufferOutputStream>();
		FPsDataOptimizedBinarySerializer Serializer(OutputBuffer, &Dictionary);
		Serializer.bWriteDefaults = false;
		const auto Property = FPsDataFriend::GetProperty(Data, Field->Index);
		Property->Serialize(&Serializer);

		FString Path = Data->GetPathFromData(this);
		Path.AppendChar('.');
		Path.Append(Field->Name);

		NetworkEvents.Emplace(EPsNetworkEventType::Changed, EncodePath(Path, Dictionary), OutputBuffer->GetBuffer());

		if (!bAccumulateEvents)
		{
			Flush();
		}
	}
}

void UPsNetworkData::CommitAddedEvent(const UPsData* Data)
{
	const auto OutputBuffer = MakeShared<FPsDataBufferOutputStream>();
	FPsDataOptimizedBinarySerializer Serializer(OutputBuffer, &Dictionary);
	Serializer.bWriteDefaults = false;

	UPsDataUPsDataLibrary::TypeSerialize(Data->GetParent(), Data->GetParentField(), &Serializer, Data);

	NetworkEvents.Emplace(EPsNetworkEventType::Added, EncodePath(Data->GetPathFromData(this), Dictionary), OutputBuffer->GetBuffer());

	if (!bAccumulateEvents)
	{
		Flush();
	}
}

void UPsNetworkData::CommitRemovingEvent(const UPsData* Data)
{
	NetworkEvents.Emplace(EPsNetworkEventType::Removed, EncodePath(Data->GetPathFromData(this), Dictionary));

	if (!bAccumulateEvents)
	{
		Flush();
	}
}

void UPsNetworkData::HandlingControllers()
{
	if (PendingControllers.Num() > 0)
	{
		for (auto ControllerIterator = PendingControllers.CreateIterator(); ControllerIterator; ++ControllerIterator)
		{
			const auto PendingController = *ControllerIterator;
			if (PendingController == nullptr)
			{
				ControllerIterator.RemoveCurrent();
			}
			else
			{
				for (TActorIterator<APsDataNetworkActor> ActorIterator(PendingController->GetWorld()); ActorIterator; ++ActorIterator)
				{
					const auto PendingActor = *ActorIterator;
					if (PendingActor->GetOwner() == PendingController)
					{
						ControllerIterator.RemoveCurrent();
						PendingActor->Open(this);
						NetworkProxies.Add(PendingActor);
					}
				}
			}
		}
	}
}

void UPsNetworkData::Synchronize(const TArray<FString>& DictionaryDiff, const FPsNetworkByteBuffer& Buffer)
{
	check(!HasAuthority());

	Dictionary.Append(DictionaryDiff);

	DEFERRED_EVENT_PROCESSING();
	const auto InputBuffer = MakeShared<FPsDataBufferInputStream>(Buffer.Buffer);
	FPsDataOptimizedBinaryDeserializer Deserializer(InputBuffer, &Dictionary);
	DataDeserialize(&Deserializer, false);

	UE_LOG(LogDataNetwork, Display, TEXT("Client proxy synchronized"));
	SynchronizePromise.Resolve();
}

void UPsNetworkData::Apply(const TArray<FString>& DictionaryDiff, const TArray<FPsNetworkEvent>& Events)
{
	check(!HasAuthority());

	Dictionary.Append(DictionaryDiff);

	DEFERRED_EVENT_PROCESSING();

	for (const auto& Event : Events)
	{
		TDataPathExecutor<true, true> PathExecutor(this, DecodePath(Event.Path, Dictionary));

		FAbstractDataProperty* Property;
		if (PathExecutor.Execute(Property))
		{
			if (Event.Type == EPsNetworkEventType::Changed)
			{
				const bool bSuccess = ApplyChanged(Property, Event.Data);
				check(bSuccess);
			}
			else if (Event.Type == EPsNetworkEventType::Added)
			{
				const bool bSuccess = ApplyAddedEvent(Property, PathExecutor.GetPath(), Event.Data);
				check(bSuccess);
			}
			else if (Event.Type == EPsNetworkEventType::Removed)
			{
				const bool bSuccess = ApplyRemovingEvent(Property, PathExecutor.GetPath());
				check(bSuccess);
			}
		}
	}
}

bool UPsNetworkData::ApplyChanged(FAbstractDataProperty* Property, const FPsNetworkByteBuffer& Buffer) const
{
	FPsDataOptimizedBinaryDeserializer Deserializer(MakeShared<FPsDataBufferInputStream>(Buffer.Buffer), &Dictionary);
	Property->Deserialize(&Deserializer);
	return true;
}

bool UPsNetworkData::ApplyAddedEvent(FAbstractDataProperty* Property, const FString& Key, const FPsNetworkByteBuffer& Buffer) const
{
	const auto Field = Property->GetField();
	check(Field->Context->IsData());

	FPsDataOptimizedBinaryDeserializer Deserializer(MakeShared<FPsDataBufferInputStream>(Buffer.Buffer), &Dictionary);
	UPsData* NewData = static_cast<UPsData*>(UPsDataUPsDataLibrary::TypeDeserialize(Property->GetOwner(), Field, &Deserializer, nullptr));

	if (Field->Context->IsArray())
	{
		const auto IndexOpt = Numbers::ToUnsignedInteger<int32>(ToStringView(Key));
		if (GetContext<TArray<UPsData*>>().IsA(Field->Context) && IndexOpt)
		{
			TPsDataArrayProxy<UPsData*> Proxy(static_cast<TDataProperty<TArray<UPsData*>>*>(Property));
			Proxy.Insert(NewData, IndexOpt.GetValue());
			return true;
		}
	}
	else if (Field->Context->IsMap())
	{
		if (GetContext<TMap<FString, UPsData*>>().IsA(Field->Context) && IsValidKey(Key))
		{
			TPsDataMapProxy<UPsData*> Proxy(static_cast<TDataProperty<TMap<FString, UPsData*>>*>(Property));
			if (!Proxy.Contains(Key))
			{
				Proxy.Add(Key, NewData);
				return true;
			}
		}
	}
	else
	{
		const auto DataProperty = static_cast<TDataProperty<UPsData*>*>(Property);
		DataProperty->SetValue(NewData);
		return true;
	}

	return false;
}

bool UPsNetworkData::ApplyRemovingEvent(FAbstractDataProperty* Property, const FString& Key) const
{
	const auto Field = Property->GetField();
	check(Field->Context->IsData());

	if (Field->Context->IsArray())
	{
		const auto IndexOpt = Numbers::ToUnsignedInteger<int32>(ToStringView(Key));
		if (GetContext<TArray<UPsData*>>().IsA(Field->Context) && IndexOpt)
		{
			TPsDataArrayProxy<UPsData*> Proxy(static_cast<TDataProperty<TArray<UPsData*>>*>(Property));
			if (Proxy.IsValidIndex(IndexOpt.GetValue()))
			{
				Proxy.RemoveAt(IndexOpt.GetValue());
				return true;
			}
		}
	}
	else if (Field->Context->IsMap())
	{
		if (GetContext<TMap<FString, UPsData*>>().IsA(Field->Context) && IsValidKey(Key))
		{
			TPsDataMapProxy<UPsData*> Proxy(static_cast<TDataProperty<TMap<FString, UPsData*>>*>(Property));
			if (Proxy.Contains(Key))
			{
				Proxy.Remove(Key);
				return true;
			}
		}
	}
	else
	{
		const auto DataProperty = static_cast<TDataProperty<UPsData*>*>(Property);
		DataProperty->SetValue(nullptr);
		return true;
	}

	return false;
}

void UPsNetworkData::MutableReset() const
{
	SynchronizePromise.Reset();
	const_cast<UPsNetworkData*>(this)->Reset();
}
