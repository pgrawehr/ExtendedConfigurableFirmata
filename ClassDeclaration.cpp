#include <ConfigurableFirmata.h>
#include "ClassDeclaration.h"

#include "SystemException.h"
#include "Exceptions.h"

using namespace stdSimple;


ClassDeclaration* SortedClassList::GetClassWithToken(int token, bool throwIfNotFound)
{
	for(auto elem = _entries.begin(); elem != _entries.end(); ++elem)
	{
		if ((*elem)->ClassToken == token)
		{
			return *elem;
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
	_entries.push_back(entry);
}

void SortedClassList::clear()
{
	_entries.clear();
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
