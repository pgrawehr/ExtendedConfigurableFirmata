#pragma once

#include "Exceptions.h"


class ClrException : public stdSimple::Exception
{
private:
	int _tokenCausingException;
	SystemException _exceptionType;

public:
	ClrException(SystemException exceptionType, int exceptionToken);
	ClrException(const char* msg, SystemException exceptionType, int exceptionToken);

	SystemException ExceptionType()
	{
		return _exceptionType;
	}

	int ExceptionToken()
	{
		return _tokenCausingException;
	}

	virtual void* ExceptionObject();
};
