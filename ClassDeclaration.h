#pragma once

#include <ConfigurableFirmata.h>
#include "ObjectVector.h"
#include "Variable.h"
#include "KnownTypeTokens.h"
#include "Exceptions.h"
#include "MemoryManagement.h"
#include "FlashMemoryManager.h"

enum class ClassProperties
{
	None = 0,
	ValueType = 1,
	Enum = 2,
	Array = 4,
};

struct Method
{
private:
	friend class SortedClassList;
public:
	Method(int methodToken, int numBaseTokens, int* baseTokens)
	{
		_methodToken = methodToken;
		_numBaseTokens = numBaseTokens;
		_baseTokens = baseTokens;
	}

	// Not a dtor, because the class instance must be copyable
	void clear()
	{
		freeEx(_baseTokens);
		_baseTokens = nullptr;
	}

	int32_t MethodToken() const
	{
		return _methodToken;
	}

	bool ImplementsMethod(int methodToken) const
	{
		for (int i = 0; i < _numBaseTokens; i++)
		{
			if (_baseTokens[i] == methodToken)
			{
				return true;
			}
		}

		return false;
	}

private:
	// Our own token
	int32_t _methodToken;
	// Other method tokens that could be seen meaning this method (i.e. from virtual base implementations)
	int32_t _numBaseTokens;
	int* _baseTokens;
};

// Hand-Made type info, because the arduino compiler doesn't support dynamic_cast (not even on the Due)
enum class ClassDeclarationType
{
	None,
	Dynamic,
	Flash,
};

class ClassDeclaration
{
protected:
	ClassDeclaration(int32_t token, int32_t parent, int16_t dynamicSize, int16_t staticSize, ClassProperties flags)
	{
		ClassToken = token;
		ParentToken = parent;
		ClassDynamicSize = dynamicSize;
		ClassStaticSize = staticSize;
		ClassFlags = flags;
	}


	// this class shall not be copy-assigned
	ClassDeclaration(const ClassDeclaration&) = delete;
	ClassDeclaration(ClassDeclaration&&) = delete;
	ClassDeclaration& operator=(const ClassDeclaration&) = delete;
	ClassDeclaration& operator=(ClassDeclaration&&) = delete;

public:
	virtual ~ClassDeclaration()
	{
	}

	/// <summary>
	/// Get the field with the given index. Returns null if index is out of bounds
	/// </summary>
	virtual Variable* GetFieldByIndex(uint32_t idx) = 0;

	virtual Method* GetMethodByIndex(uint32_t idx) = 0;

	virtual bool ImplementsInterface(int token) = 0;

	virtual ClassDeclarationType GetType() = 0;

	bool IsValueType() const
	{
		return (int)ClassFlags & (int)ClassProperties::ValueType;
	}

	bool IsEnum() const
	{
		return (int)ClassFlags & (int)ClassProperties::Enum;
	}

	bool IsArray() const
	{
		return (int)ClassFlags & (int)ClassProperties::Array;
	}

	uint32_t GetKey() const
	{
		return ClassToken;
	}

	ClassProperties ClassFlags;
	int32_t ClassToken;
	int32_t ParentToken;
	uint16_t ClassDynamicSize; // Including superclasses, but without vtable
	uint16_t ClassStaticSize; // Size of static members 
};

class ClassDeclarationDynamic : public ClassDeclaration
{
public:
	ClassDeclarationDynamic(int32_t token, int32_t parent, int16_t dynamicSize, int16_t staticSize, ClassProperties flags)
		: ClassDeclaration(token, parent, dynamicSize, staticSize, flags)
	{
	}

	virtual ~ClassDeclarationDynamic()
	{
		fieldTypes.clear();
		// The struct Method has no dtor, because it must not contain virtual members and it must be blittable
		for (size_t i = 0; i < methodTypes.size(); i++)
		{
			methodTypes[i].clear();
		}
		methodTypes.clear(true);
		interfaceTokens.clear(true);
	}

	virtual Variable* GetFieldByIndex(uint32_t idx) override;

	virtual Method* GetMethodByIndex(uint32_t idx) override;

	virtual bool ImplementsInterface(int token) override;

	virtual ClassDeclarationType GetType() override
	{
		return ClassDeclarationType::Dynamic;
	}

	// TODO: These are public for now, as they're initialized from outside. But this should be changed
	// Here, the value is the metadata token
	stdSimple::vector<Variable> fieldTypes;
	// List of indirectly callable methods of this class (ctors, virtual methods and interface implementations)
	stdSimple::vector<Method> methodTypes;
	// List of interfaces implemented by this class
	stdSimple::vector<int> interfaceTokens;
};

/// <summary>
/// A class whose memory is in flash
/// </summary>
class ClassDeclarationFlash : public ClassDeclaration
{
	friend class SortedClassList;
public:
	ClassDeclarationFlash(ClassDeclarationDynamic* source)
		: ClassDeclaration(source->ClassToken, source->ParentToken, source->ClassDynamicSize, source->ClassStaticSize, source->ClassFlags)
	{
		_fieldTypeCount = 0;
		_fieldTypes = nullptr;
		_methodTypesCount = 0;
		_methodTypes = nullptr;
		_interfaceTokenCount = 0;
		_interfaceTokens = nullptr;
	}

	virtual ~ClassDeclarationFlash() override
	{
		// This is in flash - cannot delete
	}

	virtual Variable* GetFieldByIndex(uint32_t idx) override;

	virtual Method* GetMethodByIndex(uint32_t idx) override;

	virtual bool ImplementsInterface(int token) override;

	virtual ClassDeclarationType GetType() override
	{
		return ClassDeclarationType::Flash;
	}
	
private:
	uint32_t _fieldTypeCount;
	Variable* _fieldTypes; // Pointer to list
	uint32_t _methodTypesCount;
	Method* _methodTypes;
	uint32_t _interfaceTokenCount;
	int* _interfaceTokens;
};

template<class TBase>
class SortedList
{

protected:
	stdSimple::vector<TBase*> _ramEntries;
	stdSimple::vector<TBase*> _flashEntries;
public:
	/// <summary>
	/// This iterator iterates over both provided lists in sequence
	/// </summary>
	class Iterator : public stdSimple::complexIteratorBase<TBase>
	{
	private:
		stdSimple::vector<TBase*>* _list;
		stdSimple::vector<TBase*>* _list2;
		size_t _currentIndex;
	public:
		Iterator(stdSimple::vector<TBase*>* list, stdSimple::vector<TBase*>* list2)
		{
			_list = list;
			_list2 = list2;
			// Start at the element before the start, so that the first Next() goes to the first element
			_currentIndex = -1;
		}

		TBase* Current()
		{
			if (_currentIndex < _list->size())
			{
				return _list->at(_currentIndex);
			}

			return _list2->at(_currentIndex - _list->size());
		}

		bool Next()
		{
			return (++_currentIndex) < (_list->size() + _list2->size());
		}

		void Reset()
		{
			_currentIndex = -1;
		}
	};

	SortedList()
	{
	}

	virtual ~SortedList()
	{
	}

	void Insert(TBase* entry)
	{
		_ramEntries.push_back(entry);
	}

	/// <summary>
	/// Validates that all keys in the list are ascending (for both the ram and the flash container) and no duplicates exist.
	/// Note that 0 is also not a valid key.
	/// </summary>
	void ValidateListOrder()
	{
		uint32_t previousKey = 0;
		for(size_t i = 0; i < _ramEntries.size(); i++)
		{
			TBase* base = _ramEntries.at(i);
			uint32_t currentKey = base->GetKey();
			if (previousKey >= currentKey)
			{
				throw stdSimple::ExecutionEngineException("Ram list is not ordered correctly for binary search");
			}
		}

		previousKey = 0;
		for (size_t i = 0; i < _flashEntries.size(); i++)
		{
			TBase* base = _flashEntries.at(i);
			uint32_t currentKey = base->GetKey();
			if (previousKey >= currentKey)
			{
				throw stdSimple::ExecutionEngineException("Flash list is not ordered correctly for binary search");
			}
		}
	}

	/// <summary>
	/// Performs a binary search for the given key. The list must be ordered by key.
	/// </summary>
	/// <param name="key">The key to search for</param>
	/// <returns>A pointer to the element with the given key, or null if no such element was found</returns>
	TBase* BinarySearchKey(uint32_t key)
	{
		TBase* ret = BinarySearchKeyInternal(&_flashEntries, key);
		if (ret == nullptr)
		{
			ret = BinarySearchKeyInternal(&_ramEntries, key);
		}
		
		return ret;
	}

private:
	TBase* BinarySearchKeyInternal(stdSimple::vector<TBase*>* list, uint32_t key)
	{
		if (list->size() == 0)
		{
			return nullptr;
		}
		int32_t left = 0;
		int32_t right = list->size() - 1;
		int32_t current = (left + right) / 2;
		while (true)
		{
			TBase* currentEntry = list->at(current);
			uint32_t currentKey = currentEntry->GetKey();
			if (currentKey == key)
			{
				return currentEntry;
			}

			if (key > currentKey)
			{
				left = current;
			}
			if (key < currentKey)
			{
				right = current;
			}
			
			current = (left + right) / 2;

			// At most 2 elements left?
			if (left + 1 >= right)
			{
				currentEntry = list->at(left);
				if (currentEntry->GetKey() == key)
				{
					return currentEntry;
				}

				currentEntry = list->at(right);
				if (currentEntry->GetKey() == key)
				{
					return currentEntry;
				}

				return nullptr; // Not found
			}
		}
	}
public:
	virtual void* CopyListToFlash(FlashMemoryManager* manager)
	{
		if (_flashEntries.size() == 0)
		{
			return nullptr;
		}

		int sizeToAlloc = _flashEntries.size() * sizeof(TBase*);
		sizeToAlloc += sizeof(int); // for number of entries
		byte* target = (byte*)manager->FlashAlloc(sizeToAlloc);
		int size = _flashEntries.size();
		manager->CopyToFlash(&size, target, sizeof(int));
		manager->CopyToFlash(&_flashEntries.at(0), AddBytes(target, sizeof(int)), size * sizeof(TBase*));
		return target;
	}

	virtual void ReadListFromFlash(void* flashAddress)
	{
		if (flashAddress == nullptr)
		{
			return;
		}

		int size = *((int*)flashAddress);
		_flashEntries.initFrom(size, (TBase*)AddBytes(flashAddress, sizeof(int)));
	}

	virtual void CopyContentsToFlash(FlashMemoryManager* manager) = 0;

	virtual void ThrowNotFoundException(int token) = 0;

	/// <summary>
	/// Clears the list
	/// </summary>
	/// <param name="includingFlash">Whether also the flash list should be cleared. The flash itself is not cleared, so that the data could be recovered from there</param>
	virtual void clear(bool includingFlash) = 0;

	Iterator GetIterator()
	{
		return Iterator(&_flashEntries, &_ramEntries);
	}
};

class SortedClassList : public SortedList<ClassDeclaration>
{
public:
	void CopyContentsToFlash(FlashMemoryManager* manager) override;
	void ThrowNotFoundException(int token) override;
	void clear(bool includingFlash) override;
	
	/// <summary>
	/// Gets the class declaration for a given token. Throws an exception if the token is not found, unless throwIfNotFound is false.
	/// </summary>
	/// <param name="token">Token to find</param>
	/// <param name="throwIfNotFound">True (the default) to throw if the token was not found. Needs to be true also if token is possibly 0</param>
	/// <returns>The class declaration for the class with the given token or null.</returns>
	ClassDeclaration* GetClassWithToken(int token, bool throwIfNotFound = true)
	{
		ClassDeclaration* ret = BinarySearchKey(token);

		if (throwIfNotFound && ret == nullptr)
		{
			ThrowNotFoundException(token);
		}

		return ret;
	}

	ClassDeclaration* GetClassWithToken(KnownTypeTokens token)
	{
		// These must always be present
		return GetClassWithToken((int)token, true);
	}
private:
	ClassDeclarationFlash* CreateFlashDeclaration(FlashMemoryManager* manager, ClassDeclarationDynamic* dynamic);

};

class ConstantEntry
{
public:
	int Token;
	uint32_t Length;
	int DataStart; // data stored inline
	uint32_t GetKey() const
	{
		return Token;
	}
};

// The list of constants. Each element is prefixed with the token and the length
class SortedConstantList : public SortedList<ConstantEntry>
{
public:
	void CopyContentsToFlash(FlashMemoryManager* manager) override;
	void ThrowNotFoundException(int token) override;
	void clear(bool includingFlash) override;
};

