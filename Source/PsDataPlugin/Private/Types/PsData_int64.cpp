// Copyright 2015-2020 Mail.Ru Group. All Rights Reserved.

#include "Types/PsData_int64.h"

DEFINE_FUNCTION(UPsDataInt64Library::execSetMapProperty)
{
	P_GET_OBJECT(UPsData, Target);
	P_GET_PROPERTY(FIntProperty, Hash);
	P_GET_TMAP_REF(FString, int64, Value);
	P_FINISH;
	P_NATIVE_BEGIN;
	FDataReflectionTools::SetByHash<TMap<FString, int64>>(Target, Hash, Value);
	P_NATIVE_END;
}

DEFINE_FUNCTION(UPsDataInt64Library::execGetMapProperty)
{
	P_GET_OBJECT(UPsData, Target);
	P_GET_PROPERTY(FIntProperty, Hash);
	P_GET_TMAP_REF(FString, int64, Out);
	P_FINISH;
	P_NATIVE_BEGIN;
	TMap<FString, int64>* Result = nullptr;
	FDataReflectionTools::GetByHash(Target, Hash, Result);
	Out = *Result;
	P_NATIVE_END;
}

DEFINE_FUNCTION(UPsDataInt64Library::execSetArrayProperty)
{
	P_GET_OBJECT(UPsData, Target);
	P_GET_PROPERTY(FIntProperty, Hash);
	P_GET_TARRAY_REF(int64, Value);
	P_FINISH;
	P_NATIVE_BEGIN;
	FDataReflectionTools::SetByHash<TArray<int64>>(Target, Hash, Value);
	P_NATIVE_END;
}

DEFINE_FUNCTION(UPsDataInt64Library::execGetArrayProperty)
{
	P_GET_OBJECT(UPsData, Target);
	P_GET_PROPERTY(FIntProperty, Hash);
	P_GET_TARRAY_REF(int64, Out);
	P_FINISH;
	P_NATIVE_BEGIN;
	TArray<int64>* Result = nullptr;
	FDataReflectionTools::GetByHash(Target, Hash, Result);
	Out = *Result;
	P_NATIVE_END;
}

DEFINE_FUNCTION(UPsDataInt64Library::execSetProperty)
{
	P_GET_OBJECT(UPsData, Target);
	P_GET_PROPERTY(FIntProperty, Hash);
	P_GET_PROPERTY(FInt64Property, Value);
	P_FINISH;
	P_NATIVE_BEGIN;
	FDataReflectionTools::SetByHash<int64>(Target, Hash, Value);
	P_NATIVE_END;
}

DEFINE_FUNCTION(UPsDataInt64Library::execGetProperty)
{
	P_GET_OBJECT(UPsData, Target);
	P_GET_PROPERTY(FIntProperty, Hash);
	P_GET_PROPERTY_REF(FInt64Property, Out);
	P_FINISH;
	P_NATIVE_BEGIN;
	int64* Result = nullptr;
	FDataReflectionTools::GetByHash(Target, Hash, Result);
	Out = *Result;
	P_NATIVE_END;
}

void UPsDataInt64Library::TypeSerialize(const UPsData* const Instance, const TSharedPtr<const FDataField>& Field, FPsDataSerializer* Serializer, const int64& Value)
{
	Serializer->WriteValue(Value);
}

int64 UPsDataInt64Library::TypeDeserialize(const UPsData* const Instance, const TSharedPtr<const FDataField>& Field, FPsDataDeserializer* Deserializer, const int64& Value)
{
	int64 Result = Value;
	if (Deserializer->ReadValue(Result))
	{
		return Result;
	}

	UE_LOG(LogData, Warning, TEXT("Can't deserialize \"%s::%s\" as \"%s\""), *Instance->GetClass()->GetName(), *Field->Name, *FDataReflectionTools::FType<int64>::Type());
	return Value;
}