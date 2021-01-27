#include <ConfigurableFirmata.h>
#include "ClassDeclaration.h"

#include "SystemException.h"
#include "Exceptions.h"
#include "FlashMemoryManager.h"

using namespace stdSimple;


ClassDeclaration* SortedClassList::GetClassWithToken(int token, bool throwIfNotFound)
{
	for(auto elem = GetIterator(); elem.Next();)
	{
		if (elem.Current()->ClassToken == token)
		{
			return elem.Current();
		}
	}

	if (throwIfNotFound)
	{
		throw ClrException(SystemException::ClassNotFound, token);
	}

	return nullptr;
}

void SortedClassList::Insert(ClassDeclaration* entry)
{
	_ramEntries.push_back(entry);
}

void SortedClassList::clear()
{
	_ramEntries.clear();
}

void SortedClassList::CopyToFlash()
{
	for(auto iterator = _ramEntries.begin(); iterator != _ramEntries.end(); ++iterator)
	{
		ClassDeclarationFlash* flash = CreateFlashDeclaration((ClassDeclarationDynamic*)*iterator);
		_flashEntries.push_back(flash);
	}

	_ramEntries.clear();
}

ClassDeclarationFlash* SortedClassList::CreateFlashDeclaration(ClassDeclarationDynamic* dynamic)
{
	// First create the class in RAM
	int totalSize = sizeof(ClassDeclarationFlash) + dynamic->fieldTypes.size() * sizeof(Variable) + dynamic->interfaceTokens.size() * sizeof(int32_t);
	for (size_t i = 0; i < dynamic->methodTypes.size(); i++)
	{
		totalSize += sizeof(int32_t); // method token
		totalSize += sizeof(int32_t); // length
		totalSize += dynamic->methodTypes[i]._numBaseTokens * sizeof(int32_t);
	}

	byte* flashCopy = (byte*)malloc(totalSize);
	byte* temp = flashCopy;
	// Reserve space for main class
	temp = AddBytes(temp, sizeof(ClassDeclarationFlash));
	
	ClassDeclarationFlash* flash = new ClassDeclarationFlash(dynamic);
	flash->_fieldTypeCount = dynamic->fieldTypes.size();
	size_t fieldTypesLength = dynamic->fieldTypes.size() * sizeof(Variable);
	memcpy(temp, &dynamic->fieldTypes.at(0), fieldTypesLength);
	flash->_fieldTypes = (Variable*)temp;
	temp = AddBytes(temp, fieldTypesLength);

	size_t interfaceTokenLength = dynamic->interfaceTokens.size() * sizeof(int32_t);
	flash->_interfaceTokenCount = dynamic->interfaceTokens.size();
	memcpy(temp, &dynamic->interfaceTokens.at(0), interfaceTokenLength);
	flash->_interfaceTokens = (int*)temp;
	temp = AddBytes(temp, interfaceTokenLength);

	Method* methodList = (Method*)temp;
	// First, all the methods, then for each method the corresponding base tokens. Note that the datastructure requires that the methods
	// all be in sequence with constant size.
	Method* currentMethod = methodList;
	temp = AddBytes(temp, sizeof(Method) * dynamic->methodTypes.size());
	flash->_methodTypesCount = dynamic->methodTypes.size();
	flash->_methodTypes = methodList;
	
	for (size_t i = 0; i < dynamic->methodTypes.size(); i++)
	{
		Method& src = dynamic->methodTypes[i];
		size_t baseTokenLen = dynamic->methodTypes[i]._numBaseTokens * sizeof(int32_t);
		memcpy(temp, src._baseTokens, baseTokenLen);
		byte* tokenList = temp;
		temp = AddBytes(temp, baseTokenLen);
		
		Method* method = currentMethod;
		method->_methodToken = src.MethodToken();
		method->_numBaseTokens = src._numBaseTokens;
		method->_baseTokens = (int*)tokenList;
		currentMethod = AddBytes(currentMethod, sizeof(Method));
	}

	// The class declaration comes first, copy it there
	memcpy(temp, (void*)flash, sizeof(ClassDeclarationFlash));

	ClassDeclarationFlash* flashTarget = (ClassDeclarationFlash*)FlashManager.FlashAlloc(totalSize);
	FlashManager.CopyToFlash(temp, flashTarget, totalSize);
	delete flash;

	return flashTarget;
}


Variable* ClassDeclarationDynamic::GetFieldByIndex(uint32_t idx)
{
	if (idx < fieldTypes.size())
	{
		return &fieldTypes.at(idx);
	}
	
	return nullptr;
}

Method* ClassDeclarationDynamic::GetMethodByIndex(uint32_t idx)
{
	if (idx < methodTypes.size())
	{
		return &methodTypes.at(idx);
	}

	return nullptr;
}

bool ClassDeclarationDynamic::ImplementsInterface(int token)
{
	for (uint32_t idx = 0; idx < interfaceTokens.size(); ++idx)
	{
		if (interfaceTokens[idx] == token)
		{
			return true;
		}
	}

	return false;
}

Variable* ClassDeclarationFlash::GetFieldByIndex(uint32_t idx)
{
	if (idx >= _fieldTypeCount)
	{
		return nullptr;
	}

	return _fieldTypes + idx;
}

Method* ClassDeclarationFlash::GetMethodByIndex(uint32_t idx)
{
	if (idx >= _methodTypesCount)
	{
		return nullptr;
	}

	return _methodTypes + idx;
}

bool ClassDeclarationFlash::ImplementsInterface(int token)
{
	for (uint32_t i = 0; i < _interfaceTokenCount; i++)
	{
		int* ptr = _interfaceTokens + i;
		if (*ptr == token)
		{
			return true;
		}
	}

	return false;
}
