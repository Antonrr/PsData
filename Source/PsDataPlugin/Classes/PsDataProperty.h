// Copyright 2015-2020 Mail.Ru Group. All Rights Reserved.

#pragma once

#include "PsData.h"
#include "PsDataEvent.h"
#include "PsDataField.h"
#include "PsDataTraits.h"
#include "PsDataUtils.h"
#include "Serialize/PsDataSerialization.h"

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/***********************************
 * 
 ***********************************/

namespace FDataReflectionTools
{
template <typename T>
bool UnsafeGet(UPsData* Instance, const TSharedPtr<const FDataField>& Field, T*& OutValue);
template <typename T>
void UnsafeSet(UPsData* Instance, const TSharedPtr<const FDataField>& Field, typename TConstRef<T>::Type NewValue);
} // namespace FDataReflectionTools

/***********************************
* Comparison
***********************************/

namespace FDataReflectionTools
{
template <typename T>
struct FTypeComparator
{
	static bool Compare(const T& Value0, const T& Value1)
	{
		return Value0 == Value1;
	}
};

template <typename T>
struct FTypeComparator<TArray<T>>
{
	static bool Compare(const TArray<T>& Value0, const TArray<T>& Value1)
	{
		bool bChange = false;
		if (Value0.Num() == Value1.Num())
		{
			auto Iterator0 = Value0.CreateConstIterator();
			auto Iterator1 = Value1.CreateConstIterator();
			while (Iterator0)
			{
				if (!FTypeComparator<T>::Compare(*Iterator0, *Iterator1))
				{
					return false;
				}
				++Iterator0;
				++Iterator1;
			}
		}
		else
		{
			return false;
		}
		return true;
	}
};

template <typename T>
struct FTypeComparator<TMap<FString, T>>
{
	static bool Compare(const TMap<FString, T>& Value0, const TMap<FString, T>& Value1)
	{
		bool bChange = false;
		if (Value0.Num() == Value1.Num())
		{
			auto Iterator0 = Value0.CreateConstIterator();
			auto Iterator1 = Value1.CreateConstIterator();
			while (Iterator0)
			{
				if (!FTypeComparator<FString>::Compare(Iterator0.Key(), Iterator1.Key()) || !FTypeComparator<T>::Compare(Iterator0.Value(), Iterator1.Value()))
				{
					return false;
				}
				++Iterator0;
				++Iterator1;
			}
		}
		else
		{
			return false;
		}
		return true;
	}
};
} // namespace FDataReflectionTools

/***********************************
 * Serizlize/Deserialize
 ***********************************/

namespace FDataReflectionTools
{
template <typename T>
struct FTypeDefault
{
	static const T GetDefaultValue() { return T(); }
};
} // namespace FDataReflectionTools

/***********************************
 * Serizlize/Deserialize
 ***********************************/

namespace FDataReflectionTools
{

template <typename T>
struct FTypeSerializer
{
	static void Serialize(const UPsData* Instance, const TSharedPtr<const FDataField>& Field, FPsDataSerializer* Serializer, const T& Value) = 0;
};

template <typename T>
struct FTypeDeserializer
{
	static T Deserialize(UPsData* Instance, const TSharedPtr<const FDataField>& Field, FPsDataDeserializer* Deserializer, const T& Value) = 0;
};

template <typename T, typename L>
struct FTypeSerializerExtended
{
	static void Serialize(const UPsData* Instance, const TSharedPtr<const FDataField>& Field, FPsDataSerializer* Serializer, const T& Value)
	{
		L::TypeSerialize(Instance, Field, Serializer, Value);
	}
};

template <typename T, typename L>
struct FTypeDeserializerExtended
{
	static T Deserialize(UPsData* Instance, const TSharedPtr<const FDataField>& Field, FPsDataDeserializer* Deserializer, const T& Value)
	{
		return L::TypeDeserialize(Instance, Field, Deserializer, Value);
	}
};

template <typename T>
struct FTypeSerializer<TArray<T>>
{
	static void Serialize(const UPsData* Instance, const TSharedPtr<const FDataField>& Field, FPsDataSerializer* Serializer, const TArray<T>& Value)
	{
		Serializer->WriteArray();
		for (const T& Element : Value)
		{
			FDataReflectionTools::FTypeSerializer<T>::Serialize(Instance, Field, Serializer, Element);
		}
		Serializer->PopArray();
	}
};

template <typename T>
struct FTypeDeserializer<TArray<T>>
{
	static TArray<T> Deserialize(UPsData* Instance, const TSharedPtr<const FDataField>& Field, FPsDataDeserializer* Deserializer, const TArray<T>& Value)
	{
		TArray<T> NewValue;
		if (Deserializer->ReadArray())
		{
			int i = 0;
			while (Deserializer->ReadIndex())
			{
				if (Value.IsValidIndex(i))
				{
					NewValue.Add(FDataReflectionTools::FTypeDeserializer<T>::Deserialize(Instance, Field, Deserializer, Value[i]));
				}
				else
				{
					NewValue.Add(FDataReflectionTools::FTypeDeserializer<T>::Deserialize(Instance, Field, Deserializer, FDataReflectionTools::FTypeDefault<T>::GetDefaultValue()));
				}
				i++;
				Deserializer->PopIndex();
			}
			Deserializer->PopArray();
		}
		else
		{
			UE_LOG(LogData, Warning, TEXT("Can't deserialize \"%s::%s\" as \"%s\""), *Instance->GetClass()->GetName(), *Field->Name, *FDataReflectionTools::FType<TArray<T>>::Type())
		}

		return NewValue;
	}
};

template <typename T>
struct FTypeSerializer<TMap<FString, T>>
{
	static void Serialize(const UPsData* Instance, const TSharedPtr<const FDataField>& Field, FPsDataSerializer* Serializer, const TMap<FString, T>& Value)
	{
		Serializer->WriteObject();
		for (auto& Pair : Value)
		{
			Serializer->WriteKey(Pair.Key);
			FDataReflectionTools::FTypeSerializer<T>::Serialize(Instance, Field, Serializer, Pair.Value);
			Serializer->PopKey(Pair.Key);
		}
		Serializer->PopObject();
	}
};

template <typename T>
struct FTypeDeserializer<TMap<FString, T>>
{
	static TMap<FString, T> Deserialize(UPsData* Instance, const TSharedPtr<const FDataField>& Field, FPsDataDeserializer* Deserializer, const TMap<FString, T>& Value)
	{
		TMap<FString, T> NewValue;
		if (Deserializer->ReadObject())
		{
			FString Key;
			while (Deserializer->ReadKey(Key))
			{
				if (Value.Contains(Key))
				{
					NewValue.Add(Key, FDataReflectionTools::FTypeDeserializer<T>::Deserialize(Instance, Field, Deserializer, Value[Key]));
				}
				else
				{
					NewValue.Add(Key, FDataReflectionTools::FTypeDeserializer<T>::Deserialize(Instance, Field, Deserializer, FDataReflectionTools::FTypeDefault<T>::GetDefaultValue()));
				}
				Deserializer->PopKey(Key);
			}
			Deserializer->PopObject();
		}
		else
		{
			UE_LOG(LogData, Warning, TEXT("Can't deserialize \"%s::%s\" as \"%s\""), *Instance->GetClass()->GetName(), *Field->Name, *FDataReflectionTools::FType<TMap<FString, T>>::Type())
		}
		return NewValue;
	}
};
} // namespace FDataReflectionTools

/***********************************
* Property
***********************************/

template <typename T>
struct FDataProperty : public FAbstractDataProperty
{
	T Value;

	FDataProperty()
		: Value(FDataReflectionTools::FTypeDefault<T>::GetDefaultValue())
	{
	}
	virtual ~FDataProperty() {}

	virtual void Serialize(const UPsData* Instance, FPsDataSerializer* Serializer) override
	{
		FDataReflectionTools::FTypeSerializer<T>::Serialize(Instance, GetField(), Serializer, Get());
	}

	virtual void Deserialize(UPsData* Instance, FPsDataDeserializer* Deserializer) override
	{
		Set(FDataReflectionTools::FTypeDeserializer<T>::Deserialize(Instance, GetField(), Deserializer, Value), Instance);
	}

	virtual void Reset(UPsData* Instance) override
	{
		Set(FDataReflectionTools::FTypeDefault<T>::GetDefaultValue(), Instance);
	}

	const T& Get() const
	{
		return Value;
	}

	T& Get()
	{
		return Value;
	}

	void Set(const T& NewValue, UPsData* Instance)
	{
		if (FDataReflectionTools::FTypeComparator<T>::Compare(Value, NewValue))
		{
			return;
		}

		Value = NewValue;

		FDataReflectionTools::FPsDataFriend::Changed(Instance, GetField());
	}
};

/***********************************
* Property for array
***********************************/

template <typename T>
struct FDataProperty<TArray<T>> : public FAbstractDataProperty
{
	TArray<T> Value;

	FDataProperty()
	{
		Value.Shrink();
	}

	virtual ~FDataProperty() {}

	virtual void Serialize(const UPsData* Instance, FPsDataSerializer* Serializer) override
	{
		FDataReflectionTools::FTypeSerializer<TArray<T>>::Serialize(Instance, GetField(), Serializer, Get());
	}

	virtual void Deserialize(UPsData* Instance, FPsDataDeserializer* Deserializer) override
	{
		Set(FDataReflectionTools::FTypeDeserializer<TArray<T>>::Deserialize(Instance, GetField(), Deserializer, Value), Instance);
	}

	virtual void Reset(UPsData* Instance) override
	{
		Set({}, Instance);
	}

	TArray<T>& Get()
	{
		return Value;
	}

	void Set(const TArray<T>& NewValue, UPsData* Instance)
	{
		if (FDataReflectionTools::FTypeComparator<TArray<T>>::Compare(Value, NewValue))
		{
			return;
		}

		Value = NewValue;

		FDataReflectionTools::FPsDataFriend::Changed(Instance, GetField());
	}
};

/***********************************
* Property for basic type in map
***********************************/

template <typename T>
struct FDataProperty<TMap<FString, T>> : public FAbstractDataProperty
{
	TMap<FString, T> Value;

	FDataProperty()
	{
		Value.Shrink();
	}

	virtual ~FDataProperty() {}

	virtual void Serialize(const UPsData* Instance, FPsDataSerializer* Serializer) override
	{
		FDataReflectionTools::FTypeSerializer<TMap<FString, T>>::Serialize(Instance, GetField(), Serializer, Get());
	}

	virtual void Deserialize(UPsData* Instance, FPsDataDeserializer* Deserializer) override
	{
		Set(FDataReflectionTools::FTypeDeserializer<TMap<FString, T>>::Deserialize(Instance, GetField(), Deserializer, Value), Instance);
	}

	virtual void Reset(UPsData* Instance) override
	{
		Set({}, Instance);
	}

	TMap<FString, T>& Get()
	{
		return Value;
	}

	void Set(const TMap<FString, T>& NewValue, UPsData* Instance)
	{
		if (FDataReflectionTools::FTypeComparator<TMap<FString, T>>::Compare(Value, NewValue))
		{
			return;
		}

		Value = NewValue;
		Value.KeyStableSort([](const FString& A, const FString& B) {
			return A < B;
		});

		FDataReflectionTools::FPsDataFriend::Changed(Instance, GetField());
	}
};

/***********************************
* Property for UPsData*
***********************************/

template <typename T>
struct FDataProperty<T*> : public FAbstractDataProperty
{
	T* Value;

	FDataProperty()
		: Value(nullptr)
	{
	}

	virtual ~FDataProperty() {}

	virtual void Serialize(const UPsData* Instance, FPsDataSerializer* Serializer) override
	{
		FDataReflectionTools::FTypeSerializer<T*>::Serialize(Instance, GetField(), Serializer, Get());
	}

	virtual void Deserialize(UPsData* Instance, FPsDataDeserializer* Deserializer) override
	{
		Set(FDataReflectionTools::FTypeDeserializer<T*>::Deserialize(Instance, GetField(), Deserializer, Value), Instance);
	}

	virtual void Reset(UPsData* Instance) override
	{
		if (GetField()->Meta.bStrict)
		{
			Allocate(Instance);
		}
		else
		{
			Set(nullptr, Instance);
		}
	}

	virtual void Allocate(UPsData* Instance) override
	{
		FPsDataAllocator Allocator(GetField()->Context->GetUE4Type(), Instance);
		Set(static_cast<T*>(static_cast<void*>(Allocator())), Instance);
	}

	const T* Get() const
	{
		return Value;
	}

	T*& Get()
	{
		return Value;
	}

	void Set(T* NewValue, UPsData* Instance)
	{
		auto Field = GetField();
		check(!Field->Meta.bStrict || NewValue != nullptr);

		if (Value == NewValue)
		{
			return;
		}

		if (Value)
		{
			FDataReflectionTools::FPsDataFriend::RemoveChild(Instance, static_cast<UPsData*>(static_cast<void*>(Value)));
		}

		Value = NewValue;

		if (NewValue)
		{
			FDataReflectionTools::FPsDataFriend::ChangeDataName(static_cast<UPsData*>(static_cast<void*>(NewValue)), Field->Name, TEXT(""));
			FDataReflectionTools::FPsDataFriend::AddChild(Instance, static_cast<UPsData*>(static_cast<void*>(NewValue)));
		}

		FDataReflectionTools::FPsDataFriend::Changed(Instance, Field);
	}
};

/***********************************
* Property for TArray<UPsData*>
***********************************/

template <typename T>
struct FDataProperty<TArray<T*>> : public FAbstractDataProperty
{
	TArray<T*> Value;

	FDataProperty()
	{
		Value.Shrink();
	}

	virtual ~FDataProperty() {}

	virtual void Serialize(const UPsData* Instance, FPsDataSerializer* Serializer) override
	{
		FDataReflectionTools::FTypeSerializer<TArray<T*>>::Serialize(Instance, GetField(), Serializer, Get());
	}

	virtual void Deserialize(UPsData* Instance, FPsDataDeserializer* Deserializer) override
	{
		Set(FDataReflectionTools::FTypeDeserializer<TArray<T*>>::Deserialize(Instance, GetField(), Deserializer, Value), Instance);
	}

	virtual void Reset(UPsData* Instance) override
	{
		Set({}, Instance);
	}

	TArray<T*>& Get()
	{
		return Value;
	}

	void Set(const TArray<T*>& NewValue, UPsData* Instance)
	{
		bool bChange = false;
		auto Field = GetField();

		for (int32 i = 0; i < NewValue.Num(); ++i)
		{
			if (NewValue[i]->GetParent() != Instance)
			{
				FDataReflectionTools::FPsDataFriend::ChangeDataName(NewValue[i], FString::FromInt(i), Field->Name);
				FDataReflectionTools::FPsDataFriend::AddChild(Instance, NewValue[i]);
				bChange = true;
			}
		}

		for (int32 i = 0; i < Value.Num(); ++i)
		{
			if (!NewValue.Contains(Value[i])) //TODO: optimization
			{
				FDataReflectionTools::FPsDataFriend::RemoveChild(Instance, Value[i]);
				bChange = true;
			}
		}

		if (!bChange)
		{
			return;
		}

		Value = NewValue;

		FDataReflectionTools::FPsDataFriend::Changed(Instance, Field);
	}
};

/***********************************
* Property for TMap<FString, UPsData*>
***********************************/

template <typename T>
struct FDataProperty<TMap<FString, T*>> : public FAbstractDataProperty
{
	TMap<FString, T*> Value;

	FDataProperty()
	{
		Value.Shrink();
	}

	virtual ~FDataProperty() {}

	virtual void Serialize(const UPsData* Instance, FPsDataSerializer* Serializer) override
	{
		FDataReflectionTools::FTypeSerializer<TMap<FString, T*>>::Serialize(Instance, GetField(), Serializer, Get());
	}

	virtual void Deserialize(UPsData* Instance, FPsDataDeserializer* Deserializer) override
	{
		Set(FDataReflectionTools::FTypeDeserializer<TMap<FString, T*>>::Deserialize(Instance, GetField(), Deserializer, Value), Instance);
	}

	virtual void Reset(UPsData* Instance) override
	{
		Set({}, Instance);
	}

	TMap<FString, T*>& Get()
	{
		return Value;
	}

	void Set(const TMap<FString, T*>& NewValue, UPsData* Instance)
	{
		bool bChange = false;
		auto Field = GetField();

		for (auto& Pair : NewValue)
		{
			if (Pair.Value->GetParent() != Instance)
			{
				FDataReflectionTools::FPsDataFriend::ChangeDataName(Pair.Value, Pair.Key, Field->Name);
				FDataReflectionTools::FPsDataFriend::AddChild(Instance, Pair.Value);
				bChange = true;
			}
		}

		for (auto& Pair : Value)
		{
			auto Find = NewValue.Find(Pair.Key);
			if (!Find)
			{
				FDataReflectionTools::FPsDataFriend::RemoveChild(Instance, Pair.Value);
				bChange = true;
			}
		}

		if (!bChange)
		{
			return;
		}

		Value = NewValue;
		Value.KeyStableSort([](const FString& A, const FString& B) {
			return A < B;
		});

		FDataReflectionTools::FPsDataFriend::Changed(Instance, Field);
	}
};

namespace FDataReflectionTools
{

/***********************************
 * UNSAFE GET PROPERTY
 ***********************************/

template <typename T>
bool UnsafeGet(UPsData* Instance, const TSharedPtr<const FDataField>& Field, T*& OutValue)
{
	FDataProperty<T>* Property = static_cast<FDataProperty<T>*>(FPsDataFriend::GetProperties(Instance)[Field->Index]);
	OutValue = &Property->Get();
	return true;
}

/***********************************
 * UNSAFE SET PROPERTY
 ***********************************/

template <typename T>
void UnsafeSet(UPsData* Instance, const TSharedPtr<const FDataField>& Field, typename TConstRef<T>::Type NewValue)
{
	FDataProperty<T>* Property = static_cast<FDataProperty<T>*>(FPsDataFriend::GetProperties(Instance)[Field->Index]);
	Property->Set(NewValue, Instance);
}
} // namespace FDataReflectionTools
