
#include <ConfigurableFirmata.h>
#include "Exceptions.h"
#include "ClrException.h"
#include "FirmataIlExecutor.h"



ClrException::ClrException(SystemException exceptionType, int exceptionToken)
	: Exception(GetExceptionText(exceptionType))
{
	_exceptionType = exceptionType;
	_tokenCausingException = exceptionToken;
}

ClrException::ClrException(const char* msg, SystemException exceptionType, int exceptionToken)
	: Exception(msg)
{
	_exceptionType = exceptionType;
	_tokenCausingException = exceptionToken;
}

/// <summary>
/// Creates a CLR exception from a system exception
/// </summary>
/// <returns>A managed pointer</returns>
void* ClrException::ExceptionObject(FirmataIlExecutor* executor)
{
	const char* msg = Message();
	if (strlen(msg) > 0)
	{
		Variable ex = executor->GetExceptionObjectFromToken(_exceptionType, msg);
		return ex.Object;
	}
	else
	{
		char buf[256];
		Firmata.sendString(STRING_DATA, msg);
		snprintf(buf, 256, "Unspecified error near token 0x%x", _tokenCausingException);
		Variable ex = executor->GetExceptionObjectFromToken(_exceptionType, buf);
		return ex.Object;
	}
}

const char* ClrException::GetExceptionText(SystemException systemException)
{
	switch (systemException)
	{
	case SystemException::StackOverflow:
		return "Stack overflow";
	case SystemException::NullReference:
		return "Nullreference Exception";
	case SystemException::MissingMethod:
		return "Method not found";
	case SystemException::DivideByZero:
		return "Attempted to divide by zero.";
	case SystemException::IndexOutOfRange:
		return "Index was out of range";
	case SystemException::ArrayTypeMismatch:
		return "Array type mismatch";
	case SystemException::InvalidOperation:
		return "Attempted an invalid operation.";
	case SystemException::ClassNotFound:
		return "Class not found";
	case SystemException::InvalidCast:
		return "Attempted to perform an invalid cast";
	case SystemException::NotSupported:
		return "The requested operation is not supported";
	case SystemException::FieldAccess:
		return "Error accessing a field";
	case SystemException::Overflow:
		return "An overflow error occurred";
	case SystemException::Io:
		return "An I/O Error occurred";
	case SystemException::Arithmetic:
		return "A mathematical operation caused an error";
	default:
		return "";
	}
}
