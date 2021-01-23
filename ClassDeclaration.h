﻿#pragma once

#include <ConfigurableFirmata.h>
#include "ObjectVector.h"
#include "Variable.h"
#include "KnownTypeTokens.h"

struct Method
{
	// Our own token
	int32_t token;
	// Other method tokens that could be seen meaning this method (i.e. from virtual base implementations)
	stdSimple::vector<int> declarationTokens;
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

class SortedClassList
{
private:
	stdSimple::vector<ClassDeclaration*> _entries;
public:
	class Iterator
	{
	private:
		stdSimple::vector<ClassDeclaration*>* _list;
		int _currentIndex;
	public:
		Iterator(stdSimple::vector<ClassDeclaration*>* list)
		{
			_list = list;
			// Start at the element before the start, so that the first Next() goes to the first element
			_currentIndex = -1;
		}
		
		ClassDeclaration* Current()
		{
			return _list->at(_currentIndex);
		}

		bool Next()
		{
			return (++_currentIndex) < (int)_list->size();
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

	void clear();

	Iterator GetIterator()
	{
		return Iterator(&_entries);
	}
};
