// 
// 
// 

#include <ConfigurableFirmata.h>
#include "Exceptions.h"
#include "SystemException.h"
#include "CustomClrException.h"


CustomClrException::CustomClrException(Variable& msg, int exceptionToken)
	: ClrException("Custom Exception", SystemException::CustomException, exceptionToken)
{
	if (msg.Type != VariableKind::Object)
	{
		throw stdSimple::ExecutionEngineException("Throwing a managed exception which is not of type Object");
	}

	_object = msg.Object;
}

