// CustomClrException.h

#ifndef _CUSTOMCLREXCEPTION_h
#define _CUSTOMCLREXCEPTION_h

#include <ConfigurableFirmata.h>
#include "Exceptions.h"
#include "Variable.h"
#include "ClrException.h"

// A CLR exception where a managed exception object already exists
class CustomClrException : public ClrException
{
private:
	void* _object; // Points to the managed heap. And we know it's of type Object.
public:
	CustomClrException(Variable& exceptionData, int exceptionToken);

	virtual void* ExceptionObject(FirmataIlExecutor* executor) override
	{
		return _object;
	}
};

#endif
