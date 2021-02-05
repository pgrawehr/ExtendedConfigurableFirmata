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

void SortedClassList::clear(bool includingFlash)
{
	if (includingFlash)
	{
		_flashEntries.clear();
		FlashManager.Clear();
	}
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
		totalSize += sizeof(Method);
		totalSize += dynamic->methodTypes[i]._numBaseTokens * sizeof(int32_t);
	}

	byte* flashCopy = (byte*)malloc(totalSize);
	byte* flashTarget = (byte*)FlashManager.FlashAlloc(totalSize);
	
	byte* temp = flashCopy;
	// Reserve space for main class
	temp = AddBytes(temp, sizeof(ClassDeclarationFlash));
	
	ClassDeclarationFlash* flash = new ClassDeclarationFlash(dynamic);
	flash->_fieldTypeCount = dynamic->fieldTypes.size();
	size_t fieldTypesLength = dynamic->fieldTypes.size() * sizeof(Variable);
	if (fieldTypesLength > 0)
	{
		memcpy(temp, &dynamic->fieldTypes.at(0), fieldTypesLength);
		flash->_fieldTypes = (Variable*)Relocate(flashCopy, temp, flashTarget);
		temp = AddBytes(temp, fieldTypesLength);
	}
	else
	{
		flash->_fieldTypes = nullptr;
	}

	size_t interfaceTokenLength = dynamic->interfaceTokens.size() * sizeof(int32_t);
	flash->_interfaceTokenCount = dynamic->interfaceTokens.size();
	if (interfaceTokenLength > 0)
	{
		memcpy(temp, &dynamic->interfaceTokens.at(0), interfaceTokenLength);
		flash->_interfaceTokens = (int*)Relocate(flashCopy, temp, flashTarget);
		temp = AddBytes(temp, interfaceTokenLength);
	}
	else
	{
		flash->_interfaceTokens = nullptr;
	}

	Method* methodList = (Method*)temp;
	// First, all the methods, then for each method the corresponding base tokens. Note that the datastructure requires that the methods
	// all be in sequence with constant size.
	Method* currentMethod = methodList;
	temp = AddBytes(temp, sizeof(Method) * dynamic->methodTypes.size());
	flash->_methodTypesCount = dynamic->methodTypes.size();
	flash->_methodTypes = (Method*)Relocate(flashCopy, (byte*)methodList, flashTarget);
	
	for (size_t i = 0; i < dynamic->methodTypes.size(); i++)
	{
		Method& src = dynamic->methodTypes[i];
		size_t baseTokenLen = src._numBaseTokens * sizeof(int32_t);
		memcpy(temp, src._baseTokens, baseTokenLen);
		byte* tokenList = temp;
		temp = AddBytes(temp, baseTokenLen);
		
		Method* method = currentMethod;
		method->_methodToken = src.MethodToken();
		method->_numBaseTokens = src._numBaseTokens;
		if (src._numBaseTokens > 0)
		{
			method->_baseTokens = (int*)Relocate(flashCopy, tokenList, flashTarget);
		}
		else
		{
			method->_baseTokens = nullptr;
		}
		
		currentMethod = AddBytes(currentMethod, sizeof(Method));
	}

	// The class declaration comes first, copy it there
	memcpy(flashCopy, (void*)flash, sizeof(ClassDeclarationFlash));

	FlashManager.CopyToFlash(flashCopy, flashTarget, totalSize);
	delete flash;
	free(flashCopy);

	return (ClassDeclarationFlash*)flashTarget;
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
