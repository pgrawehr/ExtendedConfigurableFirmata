#pragma once

#include <ConfigurableFirmata.h>
#include "Variable.h"

struct Method
{
	// Our own token
	int32_t token;
	// Other method tokens that could be seen meaning this method (i.e. from virtual base implementations)
	stdSimple::vector<int> declarationTokens;
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
	virtual Variable* GetFieldByIndex(int idx) = 0;

	virtual Method* GetMethodByIndex(int idx) = 0;

	virtual int* GetInterfaceTokenByIndex(int idx) = 0;

	virtual bool ImplementsInterface(int token) = 0;

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
	SortedClassList();

	ClassDeclaration* GetClassWithToken(int token, bool throwIfNotFound = true);

	void Insert(ClassDeclaration* entry);
};
