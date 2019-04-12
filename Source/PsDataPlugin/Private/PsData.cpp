// Copyright 2015-2019 Mail.Ru Group. All Rights Reserved.

#include "PsData.h"

#include "PsDataCore.h"
#include "PsDataMemory.h"
#include "Serialize/PsDataBinarySerialization.h"

#include "Async/Async.h"
#include "Core/Public/Misc/SecureHash.h"

/***********************************
* PsData friend
***********************************/

namespace FDataReflectionTools
{
void FPsDataFriend::ChangeDataName(UPsData* Data, const FString& Name, const FString& CollectionName)
{
	if (Data->DataKey != Name || Data->CollectionKey != CollectionName)
	{
		Data->DataKey = Name;
		Data->CollectionKey = CollectionName;
		Data->Broadcast(UPsDataEvent::ConstructEvent(TEXT("NameChanged"), false));
	}
}

void FPsDataFriend::AddChild(UPsData* Parent, UPsData* Data)
{
	if (Data->Parent != nullptr)
	{
		UE_LOG(LogData, Fatal, TEXT("Child already added"));
		return;
	}

	Data->Parent = Parent;
	Parent->Children.Add(Data);
	Data->Broadcast(UPsDataEvent::ConstructEvent(TEXT("Added"), true));
}

void FPsDataFriend::RemoveChild(UPsData* Parent, UPsData* Data)
{
	if (Data->Parent != Parent)
	{
		UE_LOG(LogData, Fatal, TEXT("Child not added"));
		return;
	}

	Data->Broadcast(UPsDataEvent::ConstructEvent(TEXT("Removing"), true));
	Parent->Children.Remove(Data);
	Data->Parent = nullptr;
}

bool FPsDataFriend::IsChanged(UPsData* Data)
{
	return Data->bChanged;
}

void FPsDataFriend::SetIsChanged(UPsData* Data, bool NewValue)
{
	Data->bChanged = NewValue;
}

void FPsDataFriend::InitProperties(UPsData* Data)
{
	Data->InitProperties();
}

TArray<TUniquePtr<FAbstractDataMemory>>& FPsDataFriend::GetMemory(UPsData* Data)
{
	return Data->Memory;
}
} // namespace FDataReflectionTools

/***********************************
 * FPsDataReport
 ***********************************/

FPsDataReport::FPsDataReport(EPsDataReportType InType, const FString& InSourcePath, const FString& InReason)
	: Type(InType)
	, SourcePath(InSourcePath)
	, Reason(InReason)
{
}

FPsDataReport::FPsDataReport(EPsDataReportType InType, const FString& InSourcePath, const FString& InReason, const FString& InLinkedPath)
	: Type(InType)
	, SourcePath(InSourcePath)
	, Reason(InReason)
	, LinkedPath(InLinkedPath)
{
}

/***********************************
* PSDATA!
***********************************/

UPsData::UPsData(const class FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DataKey()
	, Parent(nullptr)
	, BroadcastInProgress(0)
	, bChanged(false)
{
	FDataReflection::AddToQueue(this);
}

void UPsData::PostInitProperties()
{
	Super::PostInitProperties();
	FDataReflection::RemoveFromQueue(this);
	FDataReflection::Fill(this);
}

void UPsData::InitProperties()
{
}

/***********************************
 * Event system
 ***********************************/

void UPsData::Broadcast(UPsDataEvent* Event) const
{
	const bool bDeferredEventProcessing = false;
	if (bDeferredEventProcessing)
	{
		TWeakObjectPtr<UPsData> WeakPtr(const_cast<UPsData*>(this));
		Event->AddToRoot();
		AsyncTask(ENamedThreads::GameThread, [WeakPtr, Event]() {
			Event->RemoveFromRoot();
			if (WeakPtr.IsValid())
			{
				WeakPtr->BroadcastInternal(Event);
			}
		});
	}
	else
	{
		BroadcastInternal(Event);
	}
}

void UPsData::Bind(const FString& Type, const FPsDataDynamicDelegate& Delegate) const
{
	BindInternal(Type, Delegate);
}

void UPsData::Bind(const FString& Type, const FPsDataDelegate& Delegate) const
{
	BindInternal(Type, Delegate);
}

void UPsData::Unbind(const FString& Type, const FPsDataDynamicDelegate& Delegate) const
{
	UnbindInternal(Type, Delegate);
}

void UPsData::Unbind(const FString& Type, const FPsDataDelegate& Delegate) const
{
	UnbindInternal(Type, Delegate);
}

void UPsData::Bind(int32 Hash, const FPsDataDynamicDelegate& Delegate) const
{
	TSharedPtr<const FDataField> Field = FDataReflection::GetFieldByHash(GetClass(), Hash);
	check(Field.IsValid());
	BindInternal(Field->GenerateChangePropertyEventName(), Delegate);
}

void UPsData::Bind(int32 Hash, const FPsDataDelegate& Delegate) const
{
	TSharedPtr<const FDataField> Field = FDataReflection::GetFieldByHash(GetClass(), Hash);
	check(Field.IsValid());
	BindInternal(Field->GenerateChangePropertyEventName(), Delegate);
}

void UPsData::Unbind(int32 Hash, const FPsDataDynamicDelegate& Delegate) const
{
	TSharedPtr<const FDataField> Field = FDataReflection::GetFieldByHash(GetClass(), Hash);
	check(Field.IsValid());
	UnbindInternal(Field->GenerateChangePropertyEventName(), Delegate);
}

void UPsData::Unbind(int32 Hash, const FPsDataDelegate& Delegate) const
{
	TSharedPtr<const FDataField> Field = FDataReflection::GetFieldByHash(GetClass(), Hash);
	check(Field.IsValid());
	UnbindInternal(Field->GenerateChangePropertyEventName(), Delegate);
}

void UPsData::BlueprintBind(const FString& Type, const FPsDataDynamicDelegate& Delegate)
{
	BindInternal(Type, Delegate);
}

void UPsData::BlueprintUnbind(const FString& Type, const FPsDataDynamicDelegate& Delegate)
{
	UnbindInternal(Type, Delegate);
}

void UPsData::UpdateDelegates() const
{
	if (BroadcastInProgress > 0)
	{
		return;
	}

	for (auto MapIt = Delegates.CreateIterator(); MapIt; ++MapIt)
	{
		for (auto ArrayIt = MapIt->Value.CreateIterator(); ArrayIt; ++ArrayIt)
		{
			const FDelegateWrapper& Wrapper = *ArrayIt;
			if (!Wrapper.DynamicDelegate.IsBound() && !Wrapper.Delegate.IsBound())
			{
				ArrayIt.RemoveCurrent();
			}
		}

		if (MapIt->Value.Num() == 0)
		{
			MapIt.RemoveCurrent();
		}
	}
}

void UPsData::BroadcastInternal(UPsDataEvent* Event, UClass* Previous) const
{
	++BroadcastInProgress;

	if (Event->Target == nullptr)
	{
		Event->Target = const_cast<UPsData*>(this);
	}

	if (!Event->bStopImmediate)
	{
		auto Find = Delegates.Find(Event->Type);
		if (Find)
		{
			TArray<FDelegateWrapper> Copy = *Find;
			for (FDelegateWrapper& Wrapper : Copy)
			{
				bool bExecute = true;
				if (Wrapper.Field.IsValid())
				{
					bExecute = false;
					if (Previous == nullptr)
					{
						if (Event->Type == Wrapper.Field->GenerateChangePropertyEventName())
						{
							bExecute = true;
						}
					}
					else if (Wrapper.Field->Context->IsData())
					{
						if (Wrapper.Field->Context->GetUE4Type() == Previous)
						{
							bExecute = true;
						}
					}
				}
				if (bExecute)
				{
					Wrapper.DynamicDelegate.ExecuteIfBound(Event);
					Wrapper.Delegate.ExecuteIfBound(Event);
					if (Event->bStopImmediate)
					{
						break;
					}
				}
			}
		}
	}

	if (!Event->bStop && Event->bBubbles && Parent)
	{
		Parent->BroadcastInternal(Event, GetClass());
	}

	--BroadcastInProgress;

	UpdateDelegates();
}

void UPsData::BindInternal(const FString& Type, const FPsDataDynamicDelegate& Delegate, TSharedPtr<const FDataField> Field) const
{
	if (!Delegate.IsBound())
	{
		return;
	}

	Delegates.FindOrAdd(Type).Add(FDelegateWrapper(Delegate, Field));
	UpdateDelegates();
}

void UPsData::BindInternal(const FString& Type, const FPsDataDelegate& Delegate, TSharedPtr<const FDataField> Field) const
{
	if (!Delegate.IsBound())
	{
		return;
	}

	Delegates.FindOrAdd(Type).Add(FDelegateWrapper(Delegate, Field));
	UpdateDelegates();
}

void UPsData::UnbindInternal(const FString& Type, const FPsDataDynamicDelegate& Delegate, TSharedPtr<const FDataField> Field) const
{
	if (Delegate.IsBound())
	{
		auto Find = Delegates.Find(Type);
		if (Find)
		{
			for (FDelegateWrapper& Wrapper : *Find)
			{
				if (Wrapper.DynamicDelegate == Delegate && Wrapper.Field == Field)
				{
					Wrapper.DynamicDelegate.Unbind();
				}
			}
		}
	}

	UpdateDelegates();
}

void UPsData::UnbindInternal(const FString& Type, const FPsDataDelegate& Delegate, TSharedPtr<const FDataField> Field) const
{
	if (Delegate.IsBound())
	{
		auto Find = Delegates.Find(Type);
		if (Find)
		{
			for (FDelegateWrapper& Wrapper : *Find)
			{
				if (Wrapper.Delegate.GetHandle() == Delegate.GetHandle() && Wrapper.Field == Field)
				{
					Wrapper.Delegate.Unbind();
				}
			}
		}
	}

	UpdateDelegates();
}

/***********************************
 * Serialize/Deserialize
 ***********************************/

void UPsData::DataSerialize(FPsDataSerializer* Serializer) const
{
	for (auto& Pair : FDataReflection::GetFields(this->GetClass()))
	{
		Serializer->WriteKey(Pair.Key);
		Memory[Pair.Value->Index]->Serialize(this, Pair.Value, Serializer);
		Serializer->PopKey(Pair.Key);
	}
}

void UPsData::DataDeserialize(FPsDataDeserializer* Deserializer)
{
	const auto& Fields = FDataReflection::GetFields(this->GetClass());
	FString Key;
	while (Deserializer->ReadKey(Key))
	{
		if (auto Find = Fields.Find(Key))
		{
			auto& Field = *Find;
			Memory[Field->Index]->Deserialize(this, Field, Deserializer);
		}
		Deserializer->PopKey(Key);
	}
}

/***********************************
 * Data property
 ***********************************/

const FString& UPsData::GetDataKey() const
{
	return DataKey;
}

const FString& UPsData::GetCollectionKey() const
{
	return CollectionKey;
}

UPsData* UPsData::GetParent() const
{
	return Parent;
}

UPsData* UPsData::GetRoot() const
{
	UPsData* Root = const_cast<UPsData*>(this);
	while (Root->Parent != nullptr)
	{
		Root = Root->Parent;
	}
	return Root;
}

FString UPsData::GetHash() const
{
	//TODO: Sort keys
	//TODO: Streaming
	FPsDataBinarySerializer Serializer;
	DataSerialize(&Serializer);

	uint8 Digest[16];
	FMD5 Md5Gen;
	Md5Gen.Update((uint8*)Serializer.GetBuffer().GetData(), Serializer.GetBuffer().Num());
	Md5Gen.Final(Digest);

	return FString::Printf(
		TEXT("%08x%08x%08x%08x"),
		(static_cast<uint32>(Digest[0]) << 24) | (static_cast<uint32>(Digest[1]) << 16) | (static_cast<uint32>(Digest[2]) << 8) | static_cast<uint32>(Digest[3]),
		(static_cast<uint32>(Digest[4]) << 24) | (static_cast<uint32>(Digest[5]) << 16) | (static_cast<uint32>(Digest[6]) << 8) | static_cast<uint32>(Digest[7]),
		(static_cast<uint32>(Digest[8]) << 24) | (static_cast<uint32>(Digest[9]) << 16) | (static_cast<uint32>(Digest[10]) << 8) | static_cast<uint32>(Digest[11]),
		(static_cast<uint32>(Digest[12]) << 24) | (static_cast<uint32>(Digest[13]) << 16) | (static_cast<uint32>(Digest[14]) << 8) | static_cast<uint32>(Digest[15]));
}

/***********************************
 * Utility
 ***********************************/

void UPsData::Reset()
{
	for (const auto& Pair : FDataReflection::GetFields(GetClass()))
	{
		const auto& Field = Pair.Value;
		Memory[Field->Index]->Reset(this, Field);
	}
	InitProperties();
}

TArray<FPsDataReport> UPsData::Validation() const
{
	TArray<FPsDataReport> Result;

	//TODO: PS-136
	UPsData* Data = const_cast<UPsData*>(this);
	UPsData* Current = Data;
	UPsData* RootData = Data->GetRoot();

	FString Path;
	do
	{
		// TODO: need GetPath() function
		const FString& CollectionName = Current->GetCollectionKey();
		if (!CollectionName.IsEmpty())
		{
			if (Path.IsEmpty())
			{
				Path = CollectionName + TEXT(".") + Current->GetDataKey();
			}
			else
			{
				Path = CollectionName + TEXT(".") + Current->GetDataKey() + TEXT(".") + Path;
			}
		}
		else
		{
			if (Path.IsEmpty())
			{
				Path = Current->GetDataKey();
			}
			else
			{
				Path = Current->GetDataKey() + TEXT(".") + Path;
			}
		}
	} while ((Current = Current->GetParent()) != nullptr);

	for (auto& Pair : FDataReflection::GetLinks(GetClass()))
	{
		const FString FieldPath = Path + TEXT(".") + Pair.Value->Name;

		if (Pair.Value->bAbstract)
		{
			Result.Add(FPsDataReport(EPsDataReportType::Logic, FieldPath, TEXT("Used abstract property")));
		}
		else
		{
			if (Pair.Value->bCollection)
			{
				TArray<FString>* PropertiesPtr = nullptr;
				if (FDataReflectionTools::GetByName<TArray<FString>>(Data, Pair.Value->Name, PropertiesPtr))
				{
					TMap<FString, UPsData*>* MapPtr = nullptr;
					if (FDataReflectionTools::GetByName(RootData, Pair.Value->Path, MapPtr))
					{
						TMap<FString, UPsData*> Map = *MapPtr;

						const TArray<FString>& Properties = *PropertiesPtr;
						for (const FString& Property : Properties)
						{
							if (Property.Len() > 0)
							{
								UPsData** Find = Map.Find(Property);
								if (Find == nullptr)
								{
									Result.Add(FPsDataReport(EPsDataReportType::Link, FieldPath, TEXT("Property not found"), Pair.Value->Path + TEXT(".") + Property));
								}
							}
							else if (!Pair.Value->Meta.bNullable)
							{
								Result.Add(FPsDataReport(EPsDataReportType::Link, FieldPath, TEXT("Property is empty"), Pair.Value->Path + TEXT(".?")));
							}
						}
					}
					else
					{
						Result.Add(FPsDataReport(EPsDataReportType::Logic, Pair.Value->Path, TEXT("Used undeclared property")));
					}
				}
				else
				{
					Result.Add(FPsDataReport(EPsDataReportType::Logic, FieldPath, TEXT("Used undeclared property")));
				}
			}
			else
			{
				FString* PropertyPtr = nullptr;
				if (FDataReflectionTools::GetByName<FString>(Data, Pair.Value->Name, PropertyPtr))
				{
					TMap<FString, UPsData*>* MapPtr = nullptr;
					if (FDataReflectionTools::GetByName(RootData, Pair.Value->Path, MapPtr))
					{
						TMap<FString, UPsData*> Map = *MapPtr;
						const FString& Property = *PropertyPtr;
						if (Property.Len() > 0)
						{
							UPsData** Find = Map.Find(Property);
							if (Find == nullptr)
							{
								Result.Add(FPsDataReport(EPsDataReportType::Link, FieldPath, TEXT("Property not found"), Pair.Value->Path + TEXT(".") + Property));
							}
						}
						else if (!Pair.Value->Meta.bNullable)
						{
							Result.Add(FPsDataReport(EPsDataReportType::Link, FieldPath, TEXT("Property is empty"), Pair.Value->Path + TEXT(".?")));
						}
					}
					else
					{
						Result.Add(FPsDataReport(EPsDataReportType::Logic, Pair.Value->Path, TEXT("Used undeclared property")));
					}
				}
				else
				{
					Result.Add(FPsDataReport(EPsDataReportType::Logic, FieldPath, TEXT("Used undeclared property")));
				}
			}
		}
	}

	for (auto& Pair : FDataReflection::GetFields(this->GetClass()))
	{
		if (Pair.Value->Context->IsData())
		{
			if (Pair.Value->Context->IsArray())
			{
				TArray<UPsData*>* Value = nullptr;
				if (FDataReflectionTools::GetByField(Data, Pair.Value, Value))
				{
					for (UPsData* Element : *Value)
					{
						if (Element)
						{
							Result.Append(Element->Validation());
						}
					}
				}
			}
			else if (Pair.Value->Context->IsMap())
			{
				TMap<FString, UPsData*>* Value = nullptr;
				if (FDataReflectionTools::GetByField(Data, Pair.Value, Value))
				{
					for (const auto& ElementPair : *Value)
					{
						if (ElementPair.Value)
						{
							Result.Append(ElementPair.Value->Validation());
						}
					}
				}
			}
			else
			{
				UPsData** Value = nullptr;
				if (FDataReflectionTools::GetByField(Data, Pair.Value, Value))
				{
					if ((*Value))
					{
						Result.Append((*Value)->Validation());
					}
				}
			}
		}
	}

	return Result;
}
