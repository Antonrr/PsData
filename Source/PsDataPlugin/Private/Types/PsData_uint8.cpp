// Copyright 2015-2020 Mail.Ru Group. All Rights Reserved.

#include "Types/PsData_uint8.h"

DEFINE_FUNCTION(UPsDataUint8Library::execSetMapProperty)
{
	P_GET_OBJECT(UPsData, Target);
	P_GET_PROPERTY(FIntProperty, Hash);
	P_GET_TMAP_REF(FString, uint8, Value);
	P_FINISH;
	P_NATIVE_BEGIN;
	FDataReflectionTools::SetByHash<TMap<FString, uint8>>(Target, Hash, Value);
	P_NATIVE_END;
}

DEFINE_FUNCTION(UPsDataUint8Library::execGetMapProperty)
{
	P_GET_OBJECT(UPsData, Target);
	P_GET_PROPERTY(FIntProperty, Hash);
	P_GET_TMAP_REF(FString, uint8, Out);
	P_FINISH;
	P_NATIVE_BEGIN;
	TMap<FString, uint8>* Result = nullptr;
	FDataReflectionTools::GetByHash(Target, Hash, Result);
	Out = *Result;
	P_NATIVE_END;
}

DEFINE_FUNCTION(UPsDataUint8Library::execSetArrayProperty)
{
	P_GET_OBJECT(UPsData, Target);
	P_GET_PROPERTY(FIntProperty, Hash);
	P_GET_TARRAY_REF(uint8, Value);
	P_FINISH;
	P_NATIVE_BEGIN;
	FDataReflectionTools::SetByHash<TArray<uint8>>(Target, Hash, Value);
	P_NATIVE_END;
}

DEFINE_FUNCTION(UPsDataUint8Library::execGetArrayProperty)
{
	P_GET_OBJECT(UPsData, Target);
	P_GET_PROPERTY(FIntProperty, Hash);
	P_GET_TARRAY_REF(uint8, Out);
	P_FINISH;
	P_NATIVE_BEGIN;
	TArray<uint8>* Result = nullptr;
	FDataReflectionTools::GetByHash(Target, Hash, Result);
	Out = *Result;
	P_NATIVE_END;
}

DEFINE_FUNCTION(UPsDataUint8Library::execSetProperty)
{
	P_GET_OBJECT(UPsData, Target);
	P_GET_PROPERTY(FIntProperty, Hash);
	P_GET_PROPERTY(FByteProperty, Value);
	P_FINISH;
	P_NATIVE_BEGIN;
	FDataReflectionTools::SetByHash<uint8>(Target, Hash, Value);
	P_NATIVE_END;
}

DEFINE_FUNCTION(UPsDataUint8Library::execGetProperty)
{
	P_GET_OBJECT(UPsData, Target);
	P_GET_PROPERTY(FIntProperty, Hash);
	P_GET_PROPERTY_REF(FByteProperty, Out);
	P_FINISH;
	P_NATIVE_BEGIN;
	uint8* Result = nullptr;
	FDataReflectionTools::GetByHash(Target, Hash, Result);
	Out = *Result;
	P_NATIVE_END;
}

void UPsDataUint8Library::TypeSerialize(const UPsData* const Instance, const TSharedPtr<const FDataField>& Field, FPsDataSerializer* Serializer, const uint8& Value)
{
	Serializer->WriteValue(Value);
}

uint8 UPsDataUint8Library::TypeDeserialize(const UPsData* const Instance, const TSharedPtr<const FDataField>& Field, FPsDataDeserializer* Deserializer, const uint8& Value)
{
	uint8 Result = Value;
	if (Deserializer->ReadValue(Result))
	{
		return Result;
	}

	UE_LOG(LogData, Warning, TEXT("Can't deserialize \"%s::%s\" as \"%s\""), *Instance->GetClass()->GetName(), *Field->Name, *FDataReflectionTools::FType<uint8>::Type());
	return Value;
}