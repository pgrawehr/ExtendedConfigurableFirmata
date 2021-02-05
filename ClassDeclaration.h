#pragma once

#include <ConfigurableFirmata.h>
#include "ObjectVector.h"
#include "Variable.h"
#include "KnownTypeTokens.h"

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
		free(_baseTokens);
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
	ClassDeclaration(int32_t token, int32_t parent, int16_t dynamicSize, int16_t staticSize, bool valueType)
	{
		ClassToken = token;
		ParentToken = parent;
		ClassDynamicSize = dynamicSize;
		ClassStaticSize = staticSize;
		ValueType = valueType;
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

	bool ValueType;
	int32_t ClassToken;
	int32_t ParentToken;
	uint16_t ClassDynamicSize; // Including superclasses, but without vtable
	uint16_t ClassStaticSize; // Size of static members 
};

class ClassDeclarationDynamic : public ClassDeclaration
{
public:
	ClassDeclarationDynamic(int32_t token, int32_t parent, int16_t dynamicSize, int16_t staticSize, bool valueType)
		: ClassDeclaration(token, parent, dynamicSize, staticSize, valueType)
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
		methodTypes.clear();
		interfaceTokens.clear();
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
		: ClassDeclaration(source->ClassToken, source->ParentToken, source->ClassDynamicSize, source->ClassStaticSize, source->ValueType)
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


class SortedClassList
{
private:
	stdSimple::vector<ClassDeclaration*> _ramEntries;
	stdSimple::vector<ClassDeclaration*> _flashEntries;
public:
	/// <summary>
	/// This iterator iterates over both provided lists in sequence
	/// </summary>
	class Iterator
	{
	private:
		stdSimple::vector<ClassDeclaration*>* _list;
		stdSimple::vector<ClassDeclaration*>* _list2;
		size_t _currentIndex;
	public:
		Iterator(stdSimple::vector<ClassDeclaration*>* list, stdSimple::vector<ClassDeclaration*>* list2)
		{
			_list = list;
			_list2 = list2;
			// Start at the element before the start, so that the first Next() goes to the first element
			_currentIndex = -1;
		}
		
		ClassDeclaration* Current()
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
	};
	
	SortedClassList()
	{
		// TODO: This needs a "Reserve" method, then _entries shall be replaced by two static arrays (one in flash, the other in RAM)
	}

	/// <summary>
	/// Gets the class declaration for a given token. Throws an exception if the token is not found, unless throwIfNotFound is false.
	/// </summary>
	/// <param name="token">Token to find</param>
	/// <param name="throwIfNotFound">True (the default) to throw if the token was not found. Needs to be true also if token is possibly 0</param>
	/// <returns>The class declaration for the class with the given token or null.</returns>
	ClassDeclaration* GetClassWithToken(int token, bool throwIfNotFound = true);

	ClassDeclaration* GetClassWithToken(KnownTypeTokens token)
	{
		// These must always be present
		return GetClassWithToken((int)token, true);
	}

	void Insert(ClassDeclaration* entry);

	void CopyToFlash();
	
	void clear(bool includingFlash);

	Iterator GetIterator()
	{
		return Iterator(&_flashEntries, &_ramEntries);
	}

private:
	ClassDeclarationFlash* CreateFlashDeclaration(ClassDeclarationDynamic* dynamic);

};
