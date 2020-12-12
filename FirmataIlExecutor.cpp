/*
  FirmataIlExecutor

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  See file LICENSE.txt for further informations on licensing terms.

*/


#include <ConfigurableFirmata.h>
#include "FreeMemory.h"
#include "FirmataIlExecutor.h"
#include "openum.h"
#include "ObjectVector.h"
#include "ObjectStack.h"
#include "Encoder7Bit.h"

typedef byte BYTE;
typedef uint32_t DWORD;

// #define TRACE(x) x
#define TRACE(x)

// TODO: Remove opcodes we'll never support (i.e MONO from definition list)
const byte OpcodeInfo[] PROGMEM =
{
	#define OPDEF(c,s,pop,push,type,args,l,s1,s2,ctrl) type,
	//OPDEF(CEE_NOP, "nop", Pop0, Push0, InlineNone, 0, 1, 0xFF, 0x00, NEXT)
	#include "opcode.def.h"
	#undef OPDEF
};

#define VarPop 0x7f
#define Pop1 1
#define Pop0 0
#define PopRef 1
#define PopI 1
#define PopI8 2
#define PopR4 1
#define PopR8 2
const byte OpcodePops[] PROGMEM =
{
#define OPDEF(c,s,pop,push,type,args,l,s1,s2,ctrl) pop,
#include "opcode.def.h"
#undef OPDEF
};

#define ASSERT(x) if (!(x)) {\
	ExceptionOccurred(currentFrame, SystemException::MissingMethod, currentFrame->_executingMethod->methodToken); \
	return MethodState::Aborted; \
	}

OPCODE DecodeOpcode(const BYTE *pCode, DWORD *pdwLen);

boolean FirmataIlExecutor::handlePinMode(byte pin, int mode)
{
  // This class does not handle individual pin modes
  return false;
}

FirmataIlExecutor::FirmataIlExecutor()
{
	_methodCurrentlyExecuting = nullptr;
	_firstMethod = nullptr;
}

void FirmataIlExecutor::handleCapability(byte pin)
{
}

boolean FirmataIlExecutor::handleSysex(byte command, byte argc, byte* argv)
{
	ExecutorCommand subCommand = ExecutorCommand::None;
	if (command == SCHEDULER_DATA)
	{
		if (argc < 3)
		{
			Firmata.sendString(F("Error in Scheduler command: Not enough parameters"));
			return false;
		}
		if (argv[0] != 0xFF)
		{
			// Scheduler message type must be 0xFF, specific meaning follows
			Firmata.sendString(F("Error in Scheduler command: Unknown command syntax"));
			return false;
		}
		subCommand = (ExecutorCommand)argv[1];

		TRACE(Firmata.sendString(F("Handling client command "), (int)subCommand));
		if (IsExecutingCode() && subCommand != ExecutorCommand::ResetExecutor && subCommand != ExecutorCommand::KillTask)
		{
			Firmata.sendString(F("Execution engine busy. Ignoring command."));
			SendAckOrNack(subCommand, ExecutionError::EngineBusy);
			return true;
		}

		switch (subCommand)
		{
		case ExecutorCommand::LoadIl:
			if (argc < 8)
			{
				Firmata.sendString(F("Not enough IL data parameters"));
				SendAckOrNack(subCommand, ExecutionError::InvalidArguments);
				return true;
			}
			// 14-bit values transmitted for length and offset
			SendAckOrNack(subCommand, LoadIlDataStream(DecodePackedUint14(argv + 2), argv[4] | argv[5] << 7, argv[6] | argv[7] << 7, argc - 8, argv + 8));
			break;
		case ExecutorCommand::StartTask:
			DecodeParametersAndExecute(DecodePackedUint14(argv + 2), argc - 4, argv + 4);
			SendAckOrNack(subCommand, ExecutionError::None);
			break;
		case ExecutorCommand::DeclareMethod:
			if (argc < 6)
			{
				Firmata.sendString(F("Not enough IL data parameters"));
				SendAckOrNack(subCommand, ExecutionError::InvalidArguments);
				return true;
			}
			SendAckOrNack(subCommand, LoadIlDeclaration(DecodePackedUint14(argv + 2), argv[4], argv[5], argv[6],
				(NativeMethod)DecodePackedUint32(argv + 7), DecodePackedUint32(argv + 7 + 5)));
			break;
		case ExecutorCommand::MethodSignature:
			if (argc < 4)
			{
				Firmata.sendString(F("Not enough IL data parameters"));
				SendAckOrNack(subCommand, ExecutionError::InvalidArguments);
				return true;
			}
			SendAckOrNack(subCommand, LoadMethodSignature(DecodePackedUint14(argv + 2), argv[4], argc - 5, argv + 6));
			break;
		case ExecutorCommand::ClassDeclaration:
			if (argc < 19)
			{
				Firmata.sendString(F("Not enough IL data parameters"));
				SendAckOrNack(subCommand, ExecutionError::InvalidArguments);
			}

			SendAckOrNack(subCommand, LoadClassSignature(DecodePackedUint32(argv + 2),
				DecodePackedUint32(argv + 2 + 5), DecodePackedUint14(argv + 2 + 5 + 5), DecodePackedUint14(argv + 2 + 5 + 5 + 2) << 2,
				DecodePackedUint14(argv + 2 + 5 + 5 + 2 + 2), DecodePackedUint14(argv + 2 + 5 + 5 + 2 + 2 + 2), argc - 20, argv + 20));
			break;
		case ExecutorCommand::Interfaces:
			if (argc < 6)
			{
				Firmata.sendString(F("Not enough parameters"));
				SendAckOrNack(subCommand, ExecutionError::InvalidArguments);
			}
			SendAckOrNack(subCommand, LoadInterfaces(DecodePackedUint32(argv + 2), argc - 2, argv + 2));
			break;
		case ExecutorCommand::SendObject:
			// Not implemented
			ReceiveObjectData(argc, argv);
			SendAckOrNack(subCommand, ExecutionError::None);
			break;
		case ExecutorCommand::ConstantData:
			SendAckOrNack(subCommand, LoadConstant(subCommand, DecodePackedUint32(argv + 2), DecodePackedUint32(argv + 2 + 5), 
				DecodePackedUint32(argv + 2 + 5 + 5), argc - 17, argv + 17));
			break;
		case ExecutorCommand::ResetExecutor:
			if (argv[2] == 1)
			{
				KillCurrentTask();
				reset();
				SendAckOrNack(subCommand, ExecutionError::None);
			}
			else
			{
				SendAckOrNack(subCommand, ExecutionError::InvalidArguments);
			}
			break;
		case ExecutorCommand::KillTask:
		{
			KillCurrentTask();
			SendAckOrNack(subCommand, ExecutionError::None);
			break;
		}
		default:
			// Unknown command
			SendAckOrNack(subCommand, ExecutionError::InvalidArguments);
			break;

		} // End of switch

		return true;
	}
	return false;
}

ExecutionError FirmataIlExecutor::LoadConstant(ExecutorCommand executor_command, uint32_t constantToken, uint32_t totalLength, uint32_t offset, byte argc, byte* argv)
{
	byte* data;
	if (offset == 0)
	{
		int numToDecode = num7BitOutbytes(argc);
		data = (byte*)malloc(totalLength);
		Encoder7Bit.readBinary(numToDecode, argv, data);
		_constants.insert(constantToken, data);
		return ExecutionError::None;
	}

	int numToDecode = num7BitOutbytes(argc);
	data = _constants.at(constantToken);
	Encoder7Bit.readBinary(numToDecode, argv, data + offset);
	
	return ExecutionError::None;
}

ExecutionError FirmataIlExecutor::ReceiveObjectData(byte argc, byte* argv)
{
	// Parameters are (all of type int32)
	// 1 - Class type of target
	// 2 - Member of target
	// 3 - Instance or null for static members
	// 4 - Operation flags (i.e. newobj
	// 5 - Index if target is an array
	// 6 - value
	return ExecutionError::None;
}

/// <summary>
/// Decodes an uint 32 from 5 bytes
/// </summary>
uint32_t FirmataIlExecutor::DecodePackedUint32(byte* argv)
{
	uint32_t result = 0;
	result = argv[0];
	result |= ((uint32_t)argv[1]) << 7;
	result |= ((uint32_t)argv[2]) << 14;
	result |= ((uint32_t)argv[3]) << 21;
	result |= ((uint32_t)argv[4]) << 28;
	return result;
}

/// <summary>
/// Decode a uint14 from 2 x 7 bit
/// </summary>
uint16_t FirmataIlExecutor::DecodePackedUint14(byte *argv)
{
	uint32_t result = 0;
	result = argv[0];
	result |= ((uint32_t)argv[1]) << 7;
	return result;
}

bool FirmataIlExecutor::IsExecutingCode()
{
	return _methodCurrentlyExecuting != nullptr;
}

// TODO: Keep track, implement GC, etc...
byte* AllocGcInstance(size_t bytes)
{
	byte* ret = (byte*)malloc(bytes);
	memset(ret, 0, bytes);
	return ret;
}

// Used if it is well known that a reference now runs out of scope
void FreeGcInstance(Variable& obj)
{
	if (obj.Object != nullptr)
	{
		free(obj.Object);
	}
	obj.Object = nullptr;
	obj.Type = VariableKind::Void;
}

void FirmataIlExecutor::KillCurrentTask()
{
	if (_methodCurrentlyExecuting == nullptr)
	{
		return;
	}

	byte topLevelMethod = _methodCurrentlyExecuting->MethodIndex();

	// Ignore result - any exceptions that just occurred will be dropped by the abort request
	UnrollExecutionStack();

	// Send a status report, to end any process waiting for this method to return.
	SendExecutionResult(topLevelMethod, nullptr, Variable(), MethodState::Killed);
	Firmata.sendString(F("Code execution aborted"));
	_methodCurrentlyExecuting = nullptr;
}

RuntimeException* FirmataIlExecutor::UnrollExecutionStack()
{
	if (_methodCurrentlyExecuting == nullptr)
	{
		return nullptr;
	}

	RuntimeException* exceptionReturn = nullptr;
	
	ExecutionState** currentFrameVar = &_methodCurrentlyExecuting;
	ExecutionState* currentFrame = _methodCurrentlyExecuting;
	vector<int> stackTokens;
	while (currentFrame != nullptr)
	{
		// destruct the stack top to bottom (to ensure we regain the complete memory chain)
		while (currentFrame->_next != nullptr)
		{
			
			currentFrameVar = &currentFrame->_next;
			currentFrame = currentFrame->_next;
		}

		// Push all methods we saw to the list, this will hopefully get us something like a stack trace
		stackTokens.push_back(currentFrame->_executingMethod->methodToken);
		
		// The first exception we find (hopefully there's only one) is moved out of its container (so it doesn't get deleted here)
		if (currentFrame->_runtimeException != nullptr && exceptionReturn == nullptr)
		{
			exceptionReturn = currentFrame->_runtimeException;
			currentFrame->_runtimeException = nullptr;
		}

		delete currentFrame;
		*currentFrameVar = nullptr; // sets the parent's _next pointer to null

		currentFrame = _methodCurrentlyExecuting;
		currentFrameVar = &_methodCurrentlyExecuting;
	}

	if (exceptionReturn != nullptr)
	{
		for (size_t i = 0; i < stackTokens.size(); i++)
		{
			exceptionReturn->StackTokens.push_back(stackTokens.at(i));
		}
	}
	return exceptionReturn;
}

void FirmataIlExecutor::report(bool elapsed)
{
	// Check that we have an existing execution context, and if so continue there.
	if (!IsExecutingCode())
	{
		return;
	}

	Variable retVal;
	MethodState execResult = ExecuteIlCode(_methodCurrentlyExecuting, &retVal);

	if (execResult == MethodState::Running)
	{
		// The method is still running
		return;
	}

	int methodindex = _methodCurrentlyExecuting->MethodIndex();
	RuntimeException* ex = UnrollExecutionStack();
	SendExecutionResult(methodindex, ex, retVal, execResult);

	// The method ended
	delete _methodCurrentlyExecuting;
	_methodCurrentlyExecuting = nullptr;
}

ExecutionError FirmataIlExecutor::LoadIlDeclaration(u16 codeReference, int flags, byte maxLocals, byte argCount,
	NativeMethod nativeMethod, int token)
{
	TRACE(Firmata.sendStringf(F("Loading declaration for codeReference %d, Flags 0x%x"), 6, (int)codeReference, (int)flags));
	IlCode* method = GetMethodByCodeReference(codeReference);
	if (method != nullptr)
	{
		method->Clear();
		method->codeReference = codeReference;
	}
	else
	{
		method = new IlCode();
		method->codeReference = codeReference;
		// And immediately attach to the list
		AttachToMethodList(method);
	}

	method->methodFlags = flags;
	method->maxLocals = maxLocals;
	method->nativeMethod = nativeMethod;
	method->numArgs = argCount; // Argument count
	method->methodToken = token;

	Firmata.sendStringf(F("Loaded metadata for token 0x%lx, Flags 0x%x"), 6, token, (int)flags);
	return ExecutionError::None;
}

ExecutionError FirmataIlExecutor::LoadMethodSignature(u16 codeReference, byte signatureType, byte argc, byte* argv)
{
	TRACE(Firmata.sendStringf(F("Loading Declaration."), 0));
	IlCode* method = GetMethodByCodeReference(codeReference);
	if (method == nullptr)
	{
		// This operation is illegal if the method is unknown
		Firmata.sendString(F("LoadMethodSignature for unknown codeReference"));
		return ExecutionError::InvalidArguments;
	}

	if (signatureType == 0)
	{
		// Argument types. (This can be called multiple times for very long argument lists)
		for (byte i = 0; i < argc; i++)
		{
			VariableKind v = (VariableKind)argv[i];
			method->argumentTypes.push_back(v);
		}
	}
	else if (signatureType == 1)
	{
		// Type of the locals (also possibly called several times)
		for (byte i = 0; i < argc; i++)
		{
			VariableKind v = (VariableKind)argv[i];
			method->localTypes.push_back(v);
		}
	}
	else
	{
		return ExecutionError::InvalidArguments;
	}
	
	return ExecutionError::None;
}

ExecutionError FirmataIlExecutor::LoadIlDataStream(u16 codeReference, u16 codeLength, u16 offset, byte argc, byte* argv)
{
	// TRACE(Firmata.sendStringf(F("Going to load IL Data for method %d, total length %d offset %x"), 6, codeReference, codeLength, offset));
	IlCode* method = GetMethodByCodeReference(codeReference);
	if (method == nullptr)
	{
		// This operation is illegal if the method is unknown
		Firmata.sendString(F("LoadIlDataStream for unknown codeReference 0x"), codeReference);
		return ExecutionError::InvalidArguments;
	}

	if (offset == 0)
	{
		if (method->methodIl != nullptr)
		{
			free(method->methodIl);
			method->methodIl = nullptr;
		}
		byte* decodedIl = (byte*)malloc(codeLength);
		if (decodedIl == nullptr)
		{
			Firmata.sendString(F("Not enough memory. "), codeLength);
			return ExecutionError::OutOfMemory;
		}
		int j = 0;
		for (byte i = 0; i < argc; i += 2) 
		{
			  decodedIl[j++] = argv[i] + (argv[i + 1] << 7);
		}
		method->methodLength = codeLength;
		method->methodIl = decodedIl;
	}
	else 
	{
		byte* decodedIl = method->methodIl + offset;
		int j = 0;
		for (byte i = 0; i < argc; i += 2) 
		{
			  decodedIl[j++] = argv[i] + (argv[i + 1] << 7);
		}
	}

	TRACE(Firmata.sendStringf(F("Loaded IL Data for method %d, offset %x"), 4, codeReference, offset));
	return ExecutionError::None;
}

uint32_t FirmataIlExecutor::DecodeUint32(byte* argv)
{
	uint32_t result = 0;
	int shift = 0;
	for (byte i = 0; i < 8; i += 2) 
	{
        uint32_t nextByte = argv[i] + (argv[i + 1] << 7);
		result = result + (nextByte << shift);
		shift += 8;
	}
	
	return result;
}

void FirmataIlExecutor::SendExecutionResult(u16 codeReference, RuntimeException* ex, Variable returnValue, MethodState execResult)
{
	// Reply format:
	// bytes 0-1: Reference of method that exited
	// byte 2: Status. See below
	// byte 3: Number of integer values returned
	// bytes 4+: Integer return values

	// Todo: Fix
	auto result = returnValue.Int32;

	Firmata.startSysex();
	Firmata.write(SCHEDULER_DATA);
	Firmata.write(codeReference & 0x7F);
	Firmata.write((codeReference >> 7) & 0x7F);
	
	// 0: Code execution completed, called method ended
	// 1: Code execution aborted due to exception (i.e. unsupported opcode, method not found)
	// 2: Intermediate data from method (not used here)
	Firmata.write((byte)execResult);
	if (ex != nullptr && execResult == MethodState::Aborted)
	{
		Firmata.write((byte)(1 + ex->ExceptionArgs.size() + ex->StackTokens.size() + (ex->StackTokens.size() > 0 ? 1 : 0))); // Number of arguments that follow
		if (ex->ExceptionType == SystemException::None)
		{
			SendPackedInt32(ex->TokenOfException);
		}
		else
		{
			SendPackedInt32((int32_t)ex->ExceptionType);
		}

		for (u32 i = 0; i < ex->ExceptionArgs.size(); i++)
		{
			SendPackedInt32(ex->ExceptionArgs.at(i).Int32);
		}

		if (ex->StackTokens.size() > 0)
		{
			SendPackedInt32(0); // A dummy marker
			for (u32 i = 0; i < ex->StackTokens.size(); i++)
			{
				SendPackedInt32(ex->StackTokens.at(i));
			}
		}
	}
	else
	{
		Firmata.write(1); // Number of arguments that follow
		SendPackedInt32(result);
	}
	Firmata.endSysex();
}

void FirmataIlExecutor::SendPackedInt32(int32_t value)
{
	Firmata.write((byte)(value & 0x7F));
	Firmata.write((byte)((value >> 7) & 0x7F));
	Firmata.write((byte)((value >> 14) & 0x7F));
	Firmata.write((byte)((value >> 21) & 0x7F));
	Firmata.write((byte)((value >> 28) & 0x0F)); // only 4 bits left, and we don't care about the sign here
}


void FirmataIlExecutor::DecodeParametersAndExecute(u16 codeReference, byte argc, byte* argv)
{
	Variable result;
	IlCode* method = GetMethodByCodeReference(codeReference);
	Firmata.sendStringf(F("Code execution for %d starts. Stack Size is %d."), 4, codeReference, method->maxLocals);
	ExecutionState* rootState = new ExecutionState(codeReference, method->maxLocals, method->numArgs, method);
	_methodCurrentlyExecuting = rootState;
	for (int i = 0; i < method->numArgs; i++)
	{
		rootState->SetArgumentValue(i, DecodeUint32(argv + (8 * i)));
	}

	MethodState execResult = ExecuteIlCode(rootState, &result);
	if (execResult == MethodState::Running)
	{
		// The method is still running
		return;
	}

	Firmata.sendStringf(F("Code execution for %d has ended normally."), 2, codeReference);
	auto ex = UnrollExecutionStack();
	SendExecutionResult(codeReference, ex, result, execResult);
	
	// The method ended very quickly
	_methodCurrentlyExecuting = nullptr;
}

void FirmataIlExecutor::InvalidOpCode(u16 pc, OPCODE opCode)
{
	Firmata.sendStringf(F("Invalid/Unsupported opcode 0x%x at method offset 0x%x"), 4, opCode, pc);
	
	ExceptionOccurred(_methodCurrentlyExecuting, SystemException::InvalidOpCode, opCode);
}

MethodState FirmataIlExecutor::GetTypeFromHandle(ExecutionState* currentFrame, Variable& result, Variable type)
{
	SystemException ex = SystemException::None;
	// Create an instance of "System.Type"
	void* newObjInstance = CreateInstanceOfClass(2, &ex);
	if (newObjInstance == nullptr)
	{
		ExceptionOccurred(currentFrame, ex, 0);
		return MethodState::Aborted;
	}

	ClassDeclaration* cls = (ClassDeclaration*)(*(int32_t*)newObjInstance);
			
	// This is now a quite normal object instance
	result.Type = VariableKind::Object;
	result.Object = newObjInstance;
	// Set the "_internalType" member to point to the class declaration
	SetField(*cls, type, result, 0);
	return MethodState::Running;
}

int FirmataIlExecutor::GetHandleFromType(Variable& object) const
{
	ClassDeclaration* cls = (ClassDeclaration*)(*(int32_t*)object.Object);
	return cls->ClassToken;
}

// Executes the given OS function. Note that args[0] is the this pointer for instance methods
MethodState FirmataIlExecutor::ExecuteSpecialMethod(ExecutionState* currentFrame, NativeMethod method, const vector<Variable>& args, Variable& result)
{
	u32 mil = 0;
	int pin;
	int mode;
	MethodState state = MethodState::Running;
	switch (method)
	{
	case NativeMethod::SetPinMode: // PinMode(int pin, PinMode mode)
	{
		pin = args[1].Int32;
		
		if (args[2].Int32 == 0)
		{
			// Input
			Firmata.setPinMode(pin, INPUT);
			Firmata.setPinState(pin, 0);
		}
		if (args[2].Int32 == 1) // Must match PullMode enum on C# side
		{
			Firmata.setPinMode(pin, OUTPUT);
		}
		if (args[2].Int32 == 3)
		{
			Firmata.setPinMode(pin, INPUT);
			Firmata.setPinState(pin, 1);
		}
		
		break;
	}
	case NativeMethod::WritePin: // Write(int pin, int value)
			// Firmata.sendStringf(F("Write pin %ld value %ld"), 8, args->Get(1), args->Get(2));
		digitalWrite(args[1].Int32, args[2].Int32 != 0);
		break;
	case NativeMethod::ReadPin:
		result = { (int32_t)digitalRead(args[1].Int32), VariableKind::Int32 };
		break;
	case NativeMethod::EnvironmentTickCount: // TickCount
		mil = millis();
		// this one returns signed, because it replaces a standard library function
		result = { (int32_t)mil, VariableKind::Int32 };
		break;
	case NativeMethod::SleepMicroseconds:
		delayMicroseconds(args[0].Uint32);
		break;
	case NativeMethod::GetMicroseconds:
		result = { (uint32_t)micros(), VariableKind::Uint32 };
		break;
	case NativeMethod::Debug:
		Firmata.sendString(F("Debug "), args[1].Uint32);
		break;
	case NativeMethod::TypeEquals:
		ASSERT(args.size() == 2);
		{
			// This implements System::Type::Equals(object)
			result.Type = VariableKind::Boolean;
			Variable type1 = args[0]; // type1.
			Variable type2 = args[1]; // type2.
			ClassDeclaration& ty = _classes.at(2);
			Variable tok1 = GetField(ty, type1, 0);
			Variable tok2 = GetField(ty, type2, 0);
			result.Boolean = tok1.Int32 == tok2.Int32;
		}
		break;
	case NativeMethod::GetPinCount:
		ASSERT(args.size() == 1) // unused this pointer
		result.Int32 = TOTAL_PINS;
		result.Type = VariableKind::Int32;
		break;
	case NativeMethod::IsPinModeSupported:
		ASSERT(args.size() == 3)
			// TODO: Ask firmata (but for simplicity, we can assume the Digital I/O module is always present)
			// We support Output, Input and PullUp
			result.Boolean = (args[2].Int32 == 0 || args[2].Int32 == 1 || args[2].Int32 == 3) && IS_PIN_DIGITAL(args[1].Int32);
			result.Type = VariableKind::Boolean;
			break;
	case NativeMethod::GetPinMode:
		ASSERT(args.size() == 2)
			mode = Firmata.getPinMode(args[1].Int32);
			if (mode == INPUT)
			{
				result.Int32 = 0;
				if (Firmata.getPinState(args[1].Int32) == 1)
				{
					result.Int32 = 3; // INPUT_PULLUP instead of input
				}
			}
			else if (mode == OUTPUT)
			{
				result.Int32 = 1;
			}
			else
			{
				// This is invalid for this method. GpioDriver.GetPinMode is only valid if the pin is in one of the GPIO modes
				ExceptionOccurred(currentFrame, SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);
				state = MethodState::Aborted;
				break;
			}
			result.Type = VariableKind::Int32;
			break;
	case NativeMethod::RuntimeHelpersInitializeArray:
		ASSERT(args.size() == 2);
		{
			Variable array = args[0]; // Target array
			ASSERT(array.Type == VariableKind::ValueArray);
			Variable field = args[1]; // Runtime field type instance
			ASSERT(field.Type == VariableKind::RuntimeFieldHandle);
			uint32_t* data = (uint32_t*)array.Object;
			// TODO: Maybe we should directly store the class pointer instead of the token - or at least use a fast map<> implementation
			ClassDeclaration& ty = _classes.at(*(data + 1));
			int32_t size = *(data);
			byte* targetPtr = (byte*)(data + 2);
			memcpy(targetPtr, field.Object, size * ty.ClassDynamicSize);
		}
		break;
	case NativeMethod::TypeGetTypeFromHandle:
		ASSERT(args.size() == 1);
		{
			Variable type = args[0]; // type handle
			ASSERT(type.Type == VariableKind::RuntimeTypeHandle);
			state = GetTypeFromHandle(currentFrame, result, type);
		}
		break;
	case NativeMethod::ObjectEquals: // this is an instance method with 1 argument
	case NativeMethod::ObjectReferenceEquals: // This is a static method with 2 arguments, but implicitly the same as the above
		ASSERT(args.size() == 2)
			result.Type = VariableKind::Boolean;
			result.Boolean = args[0].Object == args[1].Object; // This implements reference equality (or binary equality for value types)
		break;
	case NativeMethod::TypeMakeGenericType:
		ASSERT(args.size() == 2);
		{
			Variable type = args[0]; // type or type handle
			Variable arguments = args[1]; // An array of types
			ASSERT(arguments.Type == VariableKind::ReferenceArray);
			uint32_t* data = (uint32_t*)arguments.Object;
			int32_t size = *(data);
			ClassDeclaration& typeOfType = _classes.at(2);
			if (size != 1)
			{
				ExceptionOccurred(currentFrame, SystemException::NotSupported, currentFrame->_executingMethod->methodToken);
				state = MethodState::Aborted;
				break;
			}
			uint32_t parameter = *(data + 2);
			Variable argumentTypeInstance;
			// First, get element of array (an object)
			argumentTypeInstance.Uint32 = parameter;
			argumentTypeInstance.Type = VariableKind::Object;
			// then get its first field, which is the type token
			Variable argumentType = GetField(typeOfType, argumentTypeInstance, 0);
			int32_t genericToken = 0;
			if (type.Type == VariableKind::RuntimeTypeHandle)
			{
				// Type handle
				genericToken = type.Int32;
			}
			else if (type.Type == VariableKind::Object)
			{
				// Type instance
				Variable token = GetField(typeOfType, type, 0);
				genericToken = token.Int32 & 0xFF000000; // Make sure this is a generic token (allows reusing this function for TypeCreateInstanceForAnotherGenericType)
			}
			else
			{
				ExceptionOccurred(currentFrame, SystemException::NotSupported, currentFrame->_executingMethod->methodToken);
				state = MethodState::Aborted;
				break;
			}

			// The sum of a generic type and its only type argument will yield the token for the combination
			type.Int32 = genericToken + argumentType.Int32;
			type.Type = VariableKind::RuntimeTypeHandle;
			state = GetTypeFromHandle(currentFrame, result, type);
			// result is returning the newly constructed type instance
		}
		break;

	case NativeMethod::CreateInstanceForAnotherGenericParameter:
		{
			// The definition of this (private) function isn't 100% clear, but
			// "CreateInstanceForAnotherGenericType(typeof(List<int>), typeof(bool))" should return an instance(!) of List<bool>.
			Variable type1 = args[0]; // type1. An instantiated generic type
			Variable type2 = args[1]; // type2. a type parameter
			ClassDeclaration& ty = _classes.at(2);
			Variable tok1 = GetField(ty, type1, 0);
			Variable tok2 = GetField(ty, type2, 0);
			int token = tok1.Int32;
			token = token & 0xFF000000;
			token = token + tok2.Int32;
			
			SystemException exception;
			if (!_classes.contains(token))
			{
				ExceptionOccurred(currentFrame, SystemException::ClassNotFound, token);
				state = MethodState::Aborted;
				break;
			}
			void* ptr = CreateInstanceOfClass(token, &exception);
			if (ptr == nullptr)
			{
				ExceptionOccurred(currentFrame, exception, token);
				state = MethodState::Aborted;
				break;
			}
			// TODO: We still have to execute the newly created instance's ctor
			result.Object = ptr;
			result.Type = VariableKind::Object;
		}
		break;
	case NativeMethod::TypeIsAssignableTo:
		{
			// Returns true if "this" type can be assigned to a variable of type "other"
			ASSERT(args.size() == 2);
			Variable ownTypeInstance = args[0]; // A type instance
			Variable otherTypeInstance = args[1]; // A type instance
			ClassDeclaration* typeClassDeclaration = GetClassDeclaration(ownTypeInstance);
			Variable ownToken = GetField(*typeClassDeclaration, ownTypeInstance, 0);
			Variable otherToken = GetField(*typeClassDeclaration, otherTypeInstance, 0);
			result.Type = VariableKind::Boolean;
			
			if (ownToken.Int32 == otherToken.Int32)
			{
				result.Boolean = true;
				break;
			}
			
			ClassDeclaration& t1 = _classes.at(ownToken.Int32);
			ClassDeclaration& t2 = _classes.at(otherToken.Int32);
			
			ClassDeclaration* parent = &_classes.at(t1.ParentToken);
			while (parent != nullptr)
			{
				if (parent->ClassToken == t2.ClassToken)
				{
					result.Boolean = true;
					break;
				}

				parent = &_classes.at(parent->ParentToken);
			}

			// Note: List is not filled yet.
			if (t1.interfaceTokens.contains(t2.ClassToken))
			{
				result.Boolean = true;
				break;
			}
			result.Boolean = false;
		}
		break;
	case NativeMethod::TypeIsEnum:
		ASSERT(args.size() == 1)
	{
		// Find out whether the current type inherits (directly) from System.Enum
			Variable ownTypeInstance = args[0]; // A type instance
			ClassDeclaration* typeClassDeclaration = GetClassDeclaration(ownTypeInstance);
			Variable ownToken = GetField(*typeClassDeclaration, ownTypeInstance, 0);
			result.Type = VariableKind::Boolean;
			ClassDeclaration& t1 = _classes.at(ownToken.Int32);
			// IsEnum returns true for enum types, but not if the type itself is "System.Enum".
			result.Boolean = t1.ParentToken == (int)KnownTypeTokens::Enum;
	}
		break;
	default:
		Firmata.sendString(F("Unknown internal method: "), (int)method);
		ExceptionOccurred(currentFrame, SystemException::MissingMethod, currentFrame->_executingMethod->methodToken);
		return MethodState::Aborted;
	}

	return state;
}

Variable FirmataIlExecutor::GetField(ClassDeclaration& type, const Variable& instancePtr, int fieldNo)
{
	int idx = 0;
	uint32_t offset = sizeof(void*);
	// We could be faster by doing
	// offset += Variable::datasize() * fieldNo;
	// but we still need the field handle for the type
	byte* o = (byte*)instancePtr.Object;
	for (auto handle = type.fieldTypes.begin(); handle != type.fieldTypes.end(); ++handle)
	{
		if (idx == fieldNo)
		{
			// Found the member
			Variable v;
			memcpy(&v.Object, (o + offset), Variable::datasize());
			v.Type = handle->Type;
			return v;
		}

		offset += Variable::datasize();
		idx++;
		if ((uint32_t)offset >= (SizeOfClass(&type)))
		{
			// Something is wrong.
			Firmata.sendString(F("Member offset exceeds class size"));
		}
	}
	
	return Variable();
}


void FirmataIlExecutor::SetField(ClassDeclaration& type, const Variable& data, Variable& instance, int fieldNo)
{
	uint32_t offset = sizeof(void*);
	offset += Variable::datasize() * fieldNo;
	memcpy(((byte*)instance.Object + offset), &data, Variable::datasize());
}

ClassDeclaration* FirmataIlExecutor::GetClassDeclaration(Variable& obj)
{
	byte* o = (byte*)obj.Object;
	ClassDeclaration* vtable = (ClassDeclaration*)(*(int32_t*)o);
	return vtable;
}

Variable FirmataIlExecutor::Ldfld(IlCode* currentMethod, Variable& obj, int32_t token)
{
	byte* o = (byte*)obj.Object;
	ClassDeclaration* vtable = GetClassDeclaration(obj);

	// Assuming sizeof(void*) == sizeof(any pointer type)
	// and sizeof(void*) >= sizeof(int)
	// Our members start here
	int offset = sizeof(void*);
	// Todo: Check base classes
	for (auto handle = vtable->fieldTypes.begin(); handle != vtable->fieldTypes.end(); ++handle)
	{
		if (handle->Int32 == token)
		{
			// Found the member
			Variable v;
			memcpy(&v.Object, (o + offset), Variable::datasize());
			v.Type = handle->Type;
			return v;
		}

		offset += Variable::datasize();
		if ((uint32_t)offset >= (SizeOfClass(vtable)))
		{
			// Something is wrong.
			Firmata.sendString(F("Member offset exceeds class size"));
		}
	}

	Firmata.sendStringf(F("Class %lx has no member %lx"), 8, vtable->ClassToken, token);
	return Variable();
}


Variable FirmataIlExecutor::Ldsfld(int token)
{
	// Find the class that has this member token
	/* ClassDeclaration* cls = nullptr;
	int offset;
	auto entry = _classes.begin();
	for (; entry != _classes.end(); ++entry)
	{
		cls = &entry.second();
		offset = 0;
		for (auto member = cls->memberTypes.begin(); member != cls->memberTypes.end(); ++member)
		{
			if (member->Int32 == token)
			{
				Variable v;
				memcpy(&v.Object, (byte*)cls->statics + offset, Variable::datasize());
				v.Type = member->Type;
				return v;
			}
			offset += Variable::datasize();
		}
	}

	Firmata.sendStringf(F("No such static member anywhere: %lx"), 4, token); */

	if (_statics.contains(token))
	{
		return _statics.at(token);
	}

	// TODO: We do not currently execute the static cctors, otherwise uninitialized statics would not be valid
	_statics.insert(token, Variable());

	return Variable();
}


void FirmataIlExecutor::Stsfld(int token, Variable value)
{
	if (_statics.contains(token))
	{
		_statics.at(token) = value;
		return;
	}

	_statics.insert(token, value);
}

void FirmataIlExecutor::Stfld(IlCode* currentMethod, Variable& obj, int32_t token, Variable& var)
{
	// The vtable is actually a pointer to the class declaration and at the beginning of the object memory
	byte* o = (byte*)obj.Object;
	// Get the first data element of where the object points to
	ClassDeclaration* cls = ((ClassDeclaration*)(*(int32_t*)o));

	// Assuming sizeof(void*) == sizeof(any pointer type)
	// Our members start here
	int offset = sizeof(void*);
	// Todo: Check base classes
	for (auto handle = cls->fieldTypes.begin(); handle != cls->fieldTypes.end(); ++handle)
	{
		// Ignore static member here
		if ((int)handle->Type & 0x80)
		{
			continue;
		}
		
		if (handle->Int32 == token)
		{
			// Found the member
			memcpy(o + offset, &var.Object, Variable::datasize());
			return;
		}

		if (offset >= SizeOfClass(cls))
		{
			// Something is wrong.
			Firmata.sendString(F("Member offset exceeds class size"));
			break;
		}
		
		offset += Variable::datasize();
	}

	Firmata.sendStringf(F("Class %lx has no member %lx"), 8, cls->ClassToken, token);
}

void FirmataIlExecutor::ExceptionOccurred(ExecutionState* state, SystemException error, int32_t errorLocationToken)
{
	state->_runtimeException = new RuntimeException(error, Variable(errorLocationToken, VariableKind::Int32));
}

MethodState FirmataIlExecutor::BasicStackInstructions(ExecutionState* state, u16 PC, stack<Variable>* stack, vector<Variable>* locals, vector<Variable>* arguments, 
	OPCODE instr, Variable value1, Variable value2, Variable value3)
{
	Variable intermediate;
	switch (instr)
	{
	case CEE_THROW:
		InvalidOpCode(PC, instr);
		return MethodState::Aborted;
	case CEE_NOP:
		break;
	case CEE_BREAK:
		// This should not normally occur in code
		InvalidOpCode(PC, instr);
		return MethodState::Aborted;
	case CEE_LDARG_0:
		stack->push(arguments->at(0));
		break;
	case CEE_LDARG_1:
		stack->push(arguments->at(1));
		break;
	case CEE_LDARG_2:
		stack->push(arguments->at(2));
		break;
	case CEE_LDARG_3:
		stack->push(arguments->at(3));
		break;
	case CEE_STLOC_0:
		intermediate = value1;
		locals->at(0) = intermediate;
		break;
	case CEE_STLOC_1:
		intermediate = value1;
		locals->at(1) = intermediate;
		break;
	case CEE_STLOC_2:
		intermediate = value1;
		locals->at(2) = intermediate;
		break;
	case CEE_STLOC_3:
		intermediate = value1;
		locals->at(3) = intermediate;
		break;
	case CEE_LDLOC_0:
		stack->push(locals->at(0));
		break;
	case CEE_LDLOC_1:
		stack->push(locals->at(1));
		break;
	case CEE_LDLOC_2:
		stack->push(locals->at(2));
		break;
	case CEE_LDLOC_3:
		stack->push(locals->at(3));
		break;
	case CEE_LDNULL:
		stack->push(Variable(VariableKind::Object));
		break;
	case CEE_CEQ:
		intermediate.Type = VariableKind::Boolean;
		intermediate.Boolean = (value1.Uint32 == value2.Uint32);
		stack->push(intermediate);
		break;
		
	case CEE_ADD:
		// TODO: Verify. It seems that the compiler guarantees that the left and right hand operator of a binary operation are always the exactly same type
		// For some of the operations, the result doesn't depend on the sign, due to correct overflow
		intermediate = { value1.Uint32 + value2.Uint32, value1.Type };
		stack->push(intermediate);
		break;
	case CEE_SUB:
		intermediate = { value1.Uint32 - value2.Uint32, value1.Type };
		stack->push(intermediate);
		break;
	case CEE_MUL:
		if (value1.Type == VariableKind::Int32)
		{
			intermediate.Int32 = value1.Int32 * value2.Int32;
			intermediate.Type = value1.Type;
		}
		else
		{
			intermediate = { value1.Uint32 * value2.Uint32, value1.Type };
		}
		stack->push(intermediate);
		break;
	case CEE_DIV:
		if (value2.Uint32 == 0)
		{
			ExceptionOccurred(state, SystemException::DivideByZero, state->_executingMethod->methodToken);
			return MethodState::Aborted;
		}
		
		if (value1.Type == VariableKind::Int32)
		{
			intermediate.Int32 = value1.Int32 / value2.Int32;
			intermediate.Type = value1.Type;
		}
		else
		{
			intermediate = { value1.Uint32 / value2.Uint32, value1.Type };
		}
		stack->push(intermediate);
		break;
	case CEE_REM:
		if (value2.Uint32 == 0)
		{
			ExceptionOccurred(state, SystemException::DivideByZero, state->_executingMethod->methodToken);
			return MethodState::Aborted;
		}
		if (value1.Type == VariableKind::Int32)
		{
			intermediate.Int32 = value1.Int32 % value2.Int32;
			intermediate.Type = value1.Type;
		}
		else
		{
			intermediate = { value1.Uint32 % value2.Uint32, value1.Type };
		}
		stack->push(intermediate);
		break;
	case CEE_DIV_UN:
		if (value2.Uint32 == 0)
		{
			ExceptionOccurred(state, SystemException::DivideByZero, state->_executingMethod->methodToken);
			return MethodState::Aborted;
		}
		intermediate = { value1.Uint32 / value2.Uint32, VariableKind::Uint32 };
		stack->push(intermediate);
		break;
	case CEE_REM_UN:
		if (value2.Uint32 == 0)
		{
			ExceptionOccurred(state, SystemException::DivideByZero, state->_executingMethod->methodToken);
			return MethodState::Aborted;
		}
		intermediate = { value1.Uint32 % value2.Uint32, VariableKind::Uint32 };
		stack->push(intermediate);
		break;
	case CEE_CGT:
		{
			if (value1.Type == VariableKind::Int32)
			{
				intermediate.Boolean = value1.Int32 > value2.Int32;
				intermediate.Type = VariableKind::Boolean;
			}
			else
			{
				intermediate = { (uint32_t)(value1.Uint32 > value2.Uint32), VariableKind::Boolean };
			}
			stack->push(intermediate);
			break;
		}
	case CEE_CGT_UN:
		intermediate = { (uint32_t)(value1.Uint32 > value2.Uint32), VariableKind::Boolean };
		stack->push(intermediate);
		break;
	case CEE_NOT:
		stack->push({ ~value1.Uint32, value1.Type });
		break;
	case CEE_NEG:
		stack->push({ -value1.Int32, value1.Type });
		break;
	case CEE_AND:
		stack->push({ value1.Uint32 & value2.Uint32, value1.Type });
		break;
	case CEE_OR:
		stack->push({ value1.Uint32 | value2.Uint32, value1.Type });
		break;
	case CEE_XOR:
		stack->push({ value1.Uint32 ^ value2.Uint32, value1.Type });
		break;
	case CEE_CLT:
		if (value1.Type == VariableKind::Int32)
		{
			intermediate.Boolean = value1.Int32 < value2.Int32;
			intermediate.Type = VariableKind::Boolean;
		}
		else
		{
			intermediate = { (uint32_t)(value1.Uint32 < value2.Uint32), VariableKind::Boolean };
		}
		stack->push(intermediate);
		break;
	case CEE_SHL:
		if (value1.Type == VariableKind::Int32)
		{
			intermediate.Int32 = value1.Int32 << value2.Int32;
			intermediate.Type = VariableKind::Int32;
		}
		else
		{
			intermediate = { value1.Uint32 << value2.Uint32, VariableKind::Uint32 };
		}
		stack->push(intermediate);
		break;
	case CEE_SHR:
		// The right-hand-side of a shift operation always requires to be of type signed int
		if (value1.Type == VariableKind::Int32)
		{
			intermediate.Int32 = value1.Int32 >> value2.Int32;
			intermediate.Type = VariableKind::Int32;
		}
		else
		{
			intermediate = { value1.Uint32 >> value2.Int32, VariableKind::Uint32 };
		}
		stack->push(intermediate);
		break;
	case CEE_SHR_UN:
		intermediate = { value1.Uint32 >> value2.Int32, VariableKind::Uint32 };
		stack->push(intermediate);
		break;
	case CEE_LDC_I4_0:
		stack->push({ (int32_t)0, VariableKind::Int32 });
		break;
	case CEE_LDC_I4_1:
		stack->push({ (int32_t)1, VariableKind::Int32 });
		break;
	case CEE_LDC_I4_2:
		stack->push({ (int32_t)2, VariableKind::Int32 });
		break;
	case CEE_LDC_I4_3:
		stack->push({ (int32_t)3, VariableKind::Int32 });
		break;
	case CEE_LDC_I4_4:
		stack->push({ (int32_t)4, VariableKind::Int32 });
		break;
	case CEE_LDC_I4_5:
		stack->push({ (int32_t)5, VariableKind::Int32 });
		break;
	case CEE_LDC_I4_6:
		stack->push({ (int32_t)6, VariableKind::Int32 });
		break;
	case CEE_LDC_I4_7:
		stack->push({ (int32_t)7, VariableKind::Int32 });
		break;
	case CEE_LDC_I4_8:
		stack->push({ (int32_t)8, VariableKind::Int32 });
		break;
	case CEE_LDC_I4_M1:
		stack->push({ (int32_t)-1, VariableKind::Int32 });
		break;
	case CEE_DUP:
		intermediate = value1;
		stack->push(intermediate);
		stack->push(intermediate);
		break;
	case CEE_POP:
		// Nothing to do, already popped
		break;
		/* Commented out because untested - enable when use case known and test case defined
	case CEE_LDIND_I1:
		// TODO: Fix type of stack (must support dynamic typing)
		{
			int8_t b = *((int8_t*)value1.Object);
			stack->push({ (int32_t)b, VariableKind::Int32 });
		}
		break;
	case CEE_LDIND_I2:
		{
			int16_t s = *((int16_t*)value1.Object);
			stack->push({ (int32_t)s, VariableKind::Int32 });
		}
		break;
	case CEE_LDIND_I4:
		{
			int32_t i = *((int32_t*)value1.Object);
			stack->push({ (int32_t)i, VariableKind::Int32 });
		}
		break;
	case CEE_LDIND_U1:
		{
			// Weird: The definition says that this loads as Int32 as well
			byte b = *((byte*)value1.Object);
			stack->push({ (int32_t)b, VariableKind::Int32 });
		}
		break;
	case CEE_LDIND_U2:
		{
			uint16_t s = *((uint16_t*)value1.Object);
			stack->push({ (int32_t)s, VariableKind::Int32 });
		}
		break;
	case CEE_LDIND_U4:
		{
			uint32_t i = *((uint32_t*)value1.Object);
			stack->push({ (int32_t)i, VariableKind::Int32 });
		}
		break; */

	case CEE_LDLEN:
		{
			// Get the address of the array and push the array size (at index 0)
			if (value1.Object == nullptr)
			{
				ExceptionOccurred(state, SystemException::NullReference, state->_executingMethod->methodToken);
				return MethodState::Aborted;
			}
			uint32_t i = *((uint32_t*)value1.Object);
			stack->push({ i, VariableKind::Uint32 });
		}
		break;

	case CEE_LDELEM_U2:
	case CEE_LDELEM_I2:
	{
		if (value1.Object == nullptr)
		{
			ExceptionOccurred(state, SystemException::NullReference, state->_executingMethod->methodToken);
			return MethodState::Aborted;
		}
		// The instruction suffix (here .i2) indicates the element size
		uint32_t* data = (uint32_t*)value1.Object;
		int32_t size = *(data);
		int32_t index = value2.Int32;
		if (index < 0 || index >= size)
		{
			ExceptionOccurred(state, SystemException::IndexOutOfRange, state->_executingMethod->methodToken);
			return MethodState::Aborted;
		}

		// This can only be a value type (of type short or ushort)
		u16* sPtr = (u16*)data;
		if (instr == CEE_LDELEM_I1)
		{
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = *(sPtr + 4 + index);
		}
		else
		{
			intermediate.Type = VariableKind::Uint32;
			intermediate.Uint32 = *(sPtr + 4 + index);
		}

		stack->push(intermediate);
	}
	break;
	case CEE_STELEM_I2:
	{
		if (value1.Object == nullptr)
		{
			ExceptionOccurred(state, SystemException::NullReference, state->_executingMethod->methodToken);
			return MethodState::Aborted;
		}
		// The instruction suffix (here .i2) indicates the element size
		uint32_t* data = (uint32_t*)value1.Object;
		int32_t size = *(data);
		int32_t index = value2.Int32;
		if (index < 0 || index >= size)
		{
			ExceptionOccurred(state, SystemException::IndexOutOfRange, state->_executingMethod->methodToken);
			return MethodState::Aborted;
		}

		// This can only be a value type (of type short or ushort)
		u16* sPtr = (u16*)data;
		*(sPtr + 4 + index) = (short)value3.Int32;
	}
	break;

	case CEE_LDELEM_U1:
	case CEE_LDELEM_I1:
	{
		if (value1.Object == nullptr)
		{
			ExceptionOccurred(state, SystemException::NullReference, state->_executingMethod->methodToken);
			return MethodState::Aborted;
		}
		// The instruction suffix (here .i1) indicates the element size
		uint32_t* data = (uint32_t*)value1.Object;
		int32_t size = *(data);
		int32_t index = value2.Int32;
		if (index < 0 || index >= size)
		{
			ExceptionOccurred(state, SystemException::IndexOutOfRange, state->_executingMethod->methodToken);
			return MethodState::Aborted;
		}

		// This can only be a value type (of type byte or sbyte)
		byte* bytePtr = (byte*)data;
		if (instr == CEE_LDELEM_I1)
		{
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = *(bytePtr + 8 + index);
		}
		else
		{
			intermediate.Type = VariableKind::Uint32;
			intermediate.Uint32 = *(bytePtr + 8 + index);
		}
			
		stack->push(intermediate);
	}
	break;
	case CEE_STELEM_I1:
	{
		if (value1.Object == nullptr)
		{
			ExceptionOccurred(state, SystemException::NullReference, state->_executingMethod->methodToken);
			return MethodState::Aborted;
		}
		// The instruction suffix (here .i4) indicates the element size
		uint32_t* data = (uint32_t*)value1.Object;
		int32_t size = *(data);
		int32_t index = value2.Int32;
		if (index < 0 || index >= size)
		{
			ExceptionOccurred(state, SystemException::IndexOutOfRange, state->_executingMethod->methodToken);
			return MethodState::Aborted;
		}

		// This can only be a value type (of type byte or sbyte)
		byte* bytePtr = (byte*)data;
		*(bytePtr + 8 + index) = (byte)value3.Int32;
	}
	break;
	case CEE_LDELEM_REF:
	case CEE_LDELEM_U4:
	case CEE_LDELEM_I4:
		{
			if (value1.Object == nullptr)
			{
				ExceptionOccurred(state, SystemException::NullReference, state->_executingMethod->methodToken);
				return MethodState::Aborted;
			}
			// The instruction suffix (here .i4) indicates the element size
			uint32_t* data = (uint32_t*)value1.Object;
			int32_t size = *(data);
			int32_t index = value2.Int32;
			if (index < 0 || index >= size)
			{
				ExceptionOccurred(state, SystemException::IndexOutOfRange, state->_executingMethod->methodToken);
				return MethodState::Aborted;
			}

			// Note: Here, size of Variable is equal size of pointer, but this doesn't hold for the other LDELEM variants
			if (value1.Type == VariableKind::ValueArray)
			{
				if (instr == CEE_LDELEM_I4)
				{
					intermediate.Type = VariableKind::Int32;
					intermediate.Int32 = *(data + 2 + index);
				}
				else
				{
					intermediate.Type = VariableKind::Uint32;
					intermediate.Uint32 = *(data + 2 + index);
				}

				stack->push(intermediate);
			}
			else
			{
				Variable r(*(data + 2 + index), VariableKind::Object);
				stack->push(r);
			}
		}
		break;
	case CEE_STELEM_REF:
	case CEE_STELEM_I4:
	{
		if (value1.Object == nullptr)
		{
			ExceptionOccurred(state, SystemException::NullReference, state->_executingMethod->methodToken);
			return MethodState::Aborted;
		}
		// The instruction suffix (here .i4) indicates the element size
		uint32_t* data = (uint32_t*)value1.Object;
		int32_t size = *(data);
		int32_t index = value2.Int32;
		if (index < 0 || index >= size)
		{
			ExceptionOccurred(state, SystemException::IndexOutOfRange, state->_executingMethod->methodToken);
			return MethodState::Aborted;
		}

		if (value1.Type == VariableKind::ValueArray)
		{
			*(data + 2 + index) = value3.Int32;
		}
		else
		{
			if (instr == CEE_STELEM_REF && value3.Type != VariableKind::Object)
			{
				// STELEM.ref shall throw if the value type doesn't match the array type. We don't test the dynamic type, but
				// at least it should be a reference
				ExceptionOccurred(state, SystemException::ArrayTypeMismatch, state->_executingMethod->methodToken);
				return MethodState::Aborted;
			}
			// can only be an object now
			*(data + 2 + index) = (uint32_t)value3.Object;
		}
	}
	break;
	case CEE_CONV_I:
	case CEE_CONV_I4:
		stack->push({ value1.Int32, VariableKind::Int32 });
		break;
	case CEE_CONV_U:
	case CEE_CONV_U4:
		stack->push({ value1.Uint32, VariableKind::Uint32 });
		break;
	default:
		InvalidOpCode(PC, instr);
		return MethodState::Aborted;
	}
	
	return MethodState::Running;
}

// Preconditions for save execution: 
// - codeLength is correct
// - argc matches argList
// - It was validated that the method has exactly argc arguments
MethodState FirmataIlExecutor::ExecuteIlCode(ExecutionState *rootState, Variable* returnValue)
{
	const int NUM_INSTRUCTIONS_AT_ONCE = 50;
	
	ExecutionState* currentFrame = rootState;
	while (currentFrame->_next != NULL)
	{
		currentFrame = currentFrame->_next;
	}

	int instructionsExecuted = 0;
	u16 PC = 0;
	u32* hlpCodePtr;
	stack<Variable>* stack;
	vector<Variable>* locals;
	vector<Variable>* arguments;
	
	currentFrame->ActivateState(&PC, &stack, &locals, &arguments);

	IlCode* currentMethod = currentFrame->_executingMethod;

	byte* pCode = currentMethod->methodIl;
	TRACE(u32 startTime = micros());
	// The compiler always inserts a return statement, so we can never run past the end of a method,
	// however we use this counter to interrupt code execution every now and then to go back to the main loop
	// and check for other tasks (i.e. serial input data)
    while (instructionsExecuted < NUM_INSTRUCTIONS_AT_ONCE)
    {
		instructionsExecuted++;
		
		DWORD   len;
        OPCODE  instr;
		
		TRACE(Firmata.sendStringf(F("PC: 0x%x in Method %d (token 0x%lx)"), 8, PC, currentMethod->codeReference, currentMethod->methodToken));
    	/*if (!stack->empty())
    	{
			Firmata.sendStringf(F("Top of stack %lx"), 4, stack->peek());
    	}*/
    	
    	if (PC == 0 && (currentMethod->methodFlags & (byte)MethodFlags::Special))
		{
			NativeMethod specialMethod = currentMethod->nativeMethod;

			TRACE(Firmata.sendString(F("Executing special method "), (int)specialMethod));
			Variable retVal;
			MethodState internalResult = ExecuteSpecialMethod(currentFrame, specialMethod, *arguments, retVal);
    		if (internalResult != MethodState::Running)
    		{
				return internalResult;
    		}
			
			// We're called into a "special" (built-in) method. 
			// Perform a method return
			ExecutionState* frame = rootState; // start at root
			while (frame->_next != currentFrame)
			{
				frame = frame->_next;
			}
			// Remove the last frame and set the PC for the new current frame
			frame->_next = nullptr;
			
			delete currentFrame;
			currentFrame = frame;
			currentFrame->ActivateState(&PC, &stack, &locals, &arguments);
			// If the method we just terminated is not of type void, we push the result to the 
			// stack of the calling method
			if ((currentMethod->methodFlags & (byte)MethodFlags::VoidOrCtor) == 0)
			{
				stack->push(retVal);
			}

			currentMethod = currentFrame->_executingMethod;

			pCode = currentMethod->methodIl;
			continue;
		}
		
		if (PC >= currentMethod->methodLength)
		{
			// Except for a hacking attempt, this may happen if a branch instruction missbehaves
			Firmata.sendString(F("Security violation: Attempted to execute code past end of method"));
			return MethodState::Aborted;
		}

		instr = DecodeOpcode(&pCode[PC], &len);
        if (instr == CEE_COUNT)
        {
			InvalidOpCode(PC, instr);
            return MethodState::Aborted;
        }
		
		PC += len;
		
		Variable intermediate;
		
		byte opCodeType = pgm_read_byte(OpcodeInfo + instr);

		switch (opCodeType)
		{
			case InlineNone:
			{
				if (instr == CEE_RET)
				{
					if (!stack->empty())
					{
						*returnValue = stack->top();
						stack->pop();
					}
					else
					{
						*returnValue = Variable();
					}

					bool oldMethodIsVoid = currentMethod->methodFlags & (byte)MethodFlags::VoidOrCtor;
					// Remove current method from execution stack
					ExecutionState* frame = rootState;
					if (frame == currentFrame)
					{
						// We're at the outermost frame
						return MethodState::Stopped;
					}

					// Find the frame which has the current frame as next (should be the second-last on the stack now)
					while (frame->_next != currentFrame)
					{
						frame = frame->_next;
					}
					// Remove the last frame and set the PC for the new current frame

					frame->_next = nullptr;
					delete currentFrame;
					currentFrame = frame;
					currentFrame->ActivateState(&PC, &stack, &locals, &arguments);

					currentMethod = currentFrame->_executingMethod;

					pCode = currentMethod->methodIl;
					// If the method we just terminated is not of type void, we push the result to the 
					// stack of the calling method (methodIndex still points to the old frame)
					if (!oldMethodIsVoid)
					{
						stack->push(*returnValue);

						TRACE(Firmata.sendString(F("Pushing return value: "), returnValue->Uint32));
					}
					
					break;
				}
					
				byte numArgumensToPop = pgm_read_byte(OpcodePops + instr);
				Variable value1;
				Variable value2;
				Variable value3;
				if (numArgumensToPop == 1)
				{
					value1 = stack->top();
					stack->pop();
				}
				if (numArgumensToPop == 2)
				{
					value2 = stack->top();
					stack->pop();
					value1 = stack->top();
					stack->pop();
				}
				if (numArgumensToPop == 3)
				{
					value3 = stack->top();
					stack->pop();
					value2 = stack->top();
					stack->pop();
					value1 = stack->top();
					stack->pop();
				}

				MethodState errorState = BasicStackInstructions(currentFrame, PC, stack, locals, arguments, instr, value1, value2, value3);
				if (errorState != MethodState::Running)
				{
					return errorState;
				};
				
				break;
			}
			case ShortInlineI:
			case ShortInlineVar:
	            {
					byte data = (byte)pCode[PC];

					PC++;
		            switch(instr)
		            {
					case CEE_UNALIGNED_: /*Ignore prefix, we don't need alignment. Just execute the actual instruction*/
						PC--;
						break;
					case CEE_LDC_I4_S:
						stack->push({ (int32_t)data, VariableKind::Int32 });
						break;
					case CEE_LDLOC_S:
						stack->push(locals->at(data));
						break;
					case CEE_STLOC_S:
						locals->at(data) = stack->top();
						stack->pop();
						break;
					/* case CEE_LDLOCA_S:
						// Warn: Pointer to data conversion!
						intermediate.Object = &locals->at(data);
						intermediate.Type = VariableKind::Object;
						stack->push(intermediate);
						break; */
					case CEE_LDARG_S:
						stack->push(arguments->at(data));
						break;
					case CEE_LDARGA_S:
						// Get address of argument x. 
						// TODO: Byref parameter handling is not supported at the moment by the call implementation. 
						intermediate.Object = &arguments->at(data);
						intermediate.Type = VariableKind::Reference;
						stack->push(intermediate);
						break;
					case CEE_STARG_S:
						arguments->at(data) = stack->top();
						stack->pop();
						break;
					default:
						InvalidOpCode(PC, instr);
						return MethodState::Aborted;
		            }
	            }
                break;
            case InlineI:
            {
				hlpCodePtr = (u32*)(pCode + PC);
				u32 v = *hlpCodePtr;
                PC += 4;
				if (instr == CEE_LDC_I4)
				{
					stack->push({ (int32_t)v, VariableKind::Int32 });
				}
				else
				{
					InvalidOpCode(PC, instr);
					return MethodState::Aborted;
				}
				break;
            }
			
            case ShortInlineBrTarget:
			case InlineBrTarget:
            {
				byte numArgumensToPop = pgm_read_byte(OpcodePops + instr);
				Variable value1;
				Variable value2;
				if (numArgumensToPop == 1)
				{
					value1 = stack->top();
					stack->pop();
				}
				if (numArgumensToPop == 2)
				{
					value2 = stack->top();
					stack->pop();
					value1 = stack->top();
					stack->pop();
				}
            		
				bool doBranch = false;
				switch (instr)
				{
					case CEE_BR:
					case CEE_BR_S:
						doBranch = true;
						break;
					case CEE_BEQ:
					case CEE_BEQ_S:
						doBranch = value1.Uint32 == value2.Uint32;
						break;
					case CEE_BGE:
					case CEE_BGE_S:
						if (value1.Type == VariableKind::Int32)
						{
							doBranch = value1.Int32 >= value2.Int32;
						}
						else
						{
							doBranch = value1.Uint32 >= value2.Uint32;
						}
						break;
					case CEE_BLE:
					case CEE_BLE_S:
						if (value1.Type == VariableKind::Int32)
						{
							doBranch = value1.Int32 <= value2.Int32;
						}
						else
						{
							doBranch = value1.Uint32 <= value2.Uint32;
						}
						break;
						break;
					case CEE_BGT:
					case CEE_BGT_S:
						if (value1.Type == VariableKind::Int32)
						{
							doBranch = value1.Int32 > value2.Int32;
						}
						else
						{
							doBranch = value1.Uint32 > value2.Uint32;
						}
						break;
						break;
					case CEE_BLT:
					case CEE_BLT_S:
						if (value1.Type == VariableKind::Int32)
						{
							doBranch = value1.Int32 < value2.Int32;
						}
						else
						{
							doBranch = value1.Uint32 < value2.Uint32;
						}
						break;
						break;
					case CEE_BGE_UN:
					case CEE_BGE_UN_S:
						doBranch = value1.Uint32 >= value2.Uint32;
						break;
					case CEE_BGT_UN:
					case CEE_BGT_UN_S:
						doBranch = value1.Uint32 > value2.Uint32;
						break;
					case CEE_BLE_UN:
					case CEE_BLE_UN_S:
						doBranch = value1.Uint32 <= value2.Uint32;
						break;
					case CEE_BLT_UN:
					case CEE_BLT_UN_S:
						doBranch = value1.Uint32 < value2.Uint32;
						break;
					case CEE_BNE_UN:
					case CEE_BNE_UN_S:
						doBranch = value1.Uint32 != value2.Uint32;
						break;
					case CEE_BRFALSE:
					case CEE_BRFALSE_S:
						doBranch = value1.Uint32 == 0;
						break;
					case CEE_BRTRUE:
					case CEE_BRTRUE_S:
						doBranch = value1.Uint32 != 0;
						break;
					default:
						InvalidOpCode(PC, instr);
						return MethodState::Aborted;
				}

				if (opCodeType == ShortInlineBrTarget)
				{
					if (doBranch)
					{
						int32_t offset = pCode[PC];
						// Manually ensure correct sign extension
						if (offset & 0x80)
						{
							offset |= 0xFFFFFF00;
						}
						int32_t dest = (PC + 1) + offset;
						
						// This certainly is > 0 again, now. 
						PC = (short)dest;
					}
					else
					{
						PC++; // Skip offset byte
					}
				}
				else if (doBranch)
				{
					int32_t offset = static_cast<int32_t>(((u32)pCode[PC]) + (((u32)pCode[PC + 1]) << 8) + (((u32)pCode[PC + 2]) << 16) + (((u32)pCode[PC + 3]) << 24));
					int32_t dest = (PC + 4) + offset;
					PC = (short)dest;
				}
				else
				{
					PC += 4;
				}
				TRACE(Firmata.sendString(F("Branch instr. Next is "), PC));
				break;
            }
			case InlineField:
	            {
				int32_t token = 0;
				Variable obj, var;
		            switch(instr)
		            {
		            	// The ldfld instruction loads a field value of an object to the stack
					case CEE_LDFLD:
						{
						token = static_cast<int32_t>(((u32)pCode[PC]) + (((u32)pCode[PC + 1]) << 8) + (((u32)pCode[PC + 2]) << 16) + (((u32)pCode[PC + 3]) << 24));
						PC += 4;
						obj = stack->top();
						stack->pop();
						stack->push(Ldfld(currentMethod, obj, token));
						break;
						}
		            	// Store a value to a field
					case CEE_STFLD:
						token = static_cast<int32_t>(((u32)pCode[PC]) + (((u32)pCode[PC + 1]) << 8) + (((u32)pCode[PC + 2]) << 16) + (((u32)pCode[PC + 3]) << 24));
						PC += 4;
						var = stack->top();
						stack->pop();
						obj = stack->top();
						stack->pop();
						Stfld(currentMethod, obj, token, var);
						break;
		            	// Store a static field value on the stack
					case CEE_STSFLD:
						token = static_cast<int32_t>(((u32)pCode[PC]) + (((u32)pCode[PC + 1]) << 8) + (((u32)pCode[PC + 2]) << 16) + (((u32)pCode[PC + 3]) << 24));
						PC += 4;
						Stsfld(token, stack->top());
						stack->pop();
						break;
					case CEE_LDSFLD:
						token = static_cast<int32_t>(((u32)pCode[PC]) + (((u32)pCode[PC + 1]) << 8) + (((u32)pCode[PC + 2]) << 16) + (((u32)pCode[PC + 3]) << 24));
						PC += 4;
						stack->push(Ldsfld(token));
						break;
					default:
						InvalidOpCode(PC, instr);
						return MethodState::Aborted;
		            }
	            }
				break;
			case InlineMethod:
            {
				if (instr != CEE_CALLVIRT && instr != CEE_CALL && instr != CEE_NEWOBJ)
				{
					InvalidOpCode(PC, instr);
					return MethodState::Aborted;
				}

				void* newObjInstance = nullptr;
            	
				hlpCodePtr = (u32*)(pCode + PC);
				u32 tk = *hlpCodePtr;
                PC += 4;

				IlCode* newMethod = ResolveToken(currentMethod, tk);
            	if (newMethod == nullptr)
				{
					Firmata.sendString(F("Unknown token 0x"), tk);
            		ExceptionOccurred(currentFrame, SystemException::MissingMethod, tk);
					return MethodState::Aborted;
				}

				ClassDeclaration* cls = nullptr;
				if (instr == CEE_CALLVIRT)
				{
					// For a virtual call, we need to grab the instance we operate on from the stack.
					// The this pointer for the new method is the object that is last on the stack, so we need to use
					// the argument count. Fortunately, this also works if so far we only have the abstract base.
					Variable instance = stack->nth(newMethod->numArgs - 1); // numArgs includes the this pointer
					
					if (instance.Type != VariableKind::Object)
					{
						Firmata.sendString(F("Virtual function call on something that is not an object"));
						return MethodState::Aborted;
					}
					if (instance.Object == nullptr)
					{
						if (newMethod->nativeMethod != NativeMethod::None)
						{
							// For native methods, the this pointer may be null, that is ok (we're calling on an dummy interface)
							goto outer;
						}
						Firmata.sendString(F("NullReferenceException calling virtual method"));
						ExceptionOccurred(currentFrame, SystemException::NullReference, 0);
						return MethodState::Aborted;
					}
					
					// The vtable is actually a pointer to the class declaration and at the beginning of the object memory
					byte* o = (byte*)instance.Object;
					// Get the first data element of where the object points to
					cls = ((ClassDeclaration*)(*(int*)o));
					for (Method* met = cls->methodTypes.begin(); met != cls->methodTypes.end(); ++met)
					{
						// The method is being called using the static type of the target
						if (met->token == newMethod->methodToken)
						{
							break;
						}

						for (auto alt = met->declarationTokens.begin(); alt != met->declarationTokens.end(); ++alt)
						{
							if (*alt == newMethod->methodToken)
							{
								newMethod = ResolveToken(currentMethod, met->token);
								if (newMethod == nullptr)
								{
									Firmata.sendString(F("Implementation not found for 0x"), met->token);
									ExceptionOccurred(currentFrame, SystemException::NullReference, met->token);
									return MethodState::Aborted;
								}
								goto outer;
							}
						}
					}
					// We didn't find another method to call - we'd better already point to the right one
				}
				outer:

				// Call to an abstract base class or an interface method - if this happens,
				// we've probably not done the virtual function resolution correctly
				if ((int)newMethod->methodFlags & (int)MethodFlags::Abstract)
				{
					Firmata.sendString(F("Call to abstract method 0x"), tk);
					ExceptionOccurred(currentFrame, SystemException::MissingMethod, tk);
					return MethodState::Aborted;
				}

				if (instr == CEE_NEWOBJ)
				{
					SystemException ex = SystemException::None;
					newObjInstance = CreateInstance(newMethod->methodToken, &ex);
					if (newObjInstance == nullptr)
					{
						ExceptionOccurred(currentFrame, ex, newMethod->methodToken);
						return MethodState::Aborted;
					}
				}

					/* See the ResolveToken method why this is currently disabled
				if (instr != CEE_CALLVIRT)
				{
					u32 method = (u32)newMethod;
					method &= ~0xFF000000;

					// Patch the code to use the method pointer, that's faster for next time we see this piece of code.
					// But remove the top byte, this is the memory bank address, which is not 0 for some of the ARM boards
					*hlpCodePtr = method;
				}
				*/

            	// Save return PC
                currentFrame->UpdatePc(PC);
				
				int argumentCount = newMethod->numArgs;
				ExecutionState* newState = new ExecutionState(newMethod->codeReference, newMethod->maxLocals, argumentCount, newMethod);
				currentFrame->_next = newState;
				
				stdSimple::stack<Variable>* oldStack = stack;
				// Start of the called method
				currentFrame = newState;
				currentFrame->ActivateState(&PC, &stack, &locals, &arguments);

            	// Load data pointer for the new method
				currentMethod = newMethod;
				pCode = newMethod->methodIl;

				// Provide arguments to the new method
				if (newObjInstance != nullptr)
            	{
            		// We're calling a ctor. The first argument (0) is the newly created object instance and so does not come from the stack
					while (argumentCount > 1)
					{
						argumentCount--;
						arguments->at(argumentCount) = oldStack->top();
						oldStack->pop();
					}

					// The last argument to push is the new object
					Variable v;
					v.Type = VariableKind::Object;
					v.Object = newObjInstance;
					arguments->at(0) = v;

					// Also push it to the stack of the calling method - this will be the implicit return value of
					// the newobj call.
					oldStack->push(v);
            	}
				else
				{
					while (argumentCount > 0)
					{
						argumentCount--;
						Variable v = oldStack->top();
						if (argumentCount == 0 && v.Type == VariableKind::Reference)
						{
							// TODO: The "this" pointer of a value type is passed by reference (it is loaded using a ldarga instruction)
							// But for us, it nevertheless points to an object variable slot. (Why?) Therefore, unbox the reference.
							// There are a few more special cases to consider it seems, especially when the called method is virtual, see §8.6.1.5
							v.Object = (void*)(*((uint32_t*)v.Object));
							v.Type = VariableKind::Object;
							arguments->at(0) = v;
						}
						else
						{
							arguments->at(argumentCount) = v;
						}
						oldStack->pop();
					}
				}
            		
				TRACE(Firmata.sendStringf(F("Pushed stack to method %d"), 2, currentMethod->codeReference));
				break;
            }
			case InlineType:
			{
				int token = static_cast<int32_t>(((u32)pCode[PC]) + (((u32)pCode[PC + 1]) << 8) + (((u32)pCode[PC + 2]) << 16) + (((u32)pCode[PC + 3]) << 24));
				PC += 4;
				int size;
				switch(instr)
				{
				case CEE_NEWARR:
					size = stack->top().Int32;
					stack->pop();
					if (_classes.contains(token))
					{
						ClassDeclaration& ty = _classes.at(token);
						uint32_t* data;
						Variable v1;
						if (ty.ValueType)
						{
							// Value types are stored directly in the array. Element 0 (of type int32) will contain the array length, Element 1 is the array type token
							// For value types, ClassDynamicSize may be smaller than a memory slot, because we don't want to store char[] or byte[] with 32 bits per element
							data = (uint32_t*)AllocGcInstance(ty.ClassDynamicSize * size + 8);
							v1.Type = VariableKind::ValueArray;
						}
						else
						{
							// Otherwise we just store pointers
							data = (uint32_t*)AllocGcInstance(size * sizeof(void*) + 8);
							v1.Type = VariableKind::ReferenceArray;
						}
						
						*data = size;
						*(data + 1) = token;
						v1.Object = data;
						stack->push(v1);
					}
					else
					{
						Firmata.sendStringf(F("Unknown class token in NEWARR instruction: %lx"), 4, token);
						return MethodState::Aborted;
					}
					break;
				case CEE_STELEM:
					{
						Variable value3 = stack->top();
						stack->pop();
						Variable value2 = stack->top();
						stack->pop();
						Variable value1 = stack->top();
						stack->pop();
						if (value1.Object == nullptr)
						{
							ExceptionOccurred(currentFrame, SystemException::NullReference, currentFrame->_executingMethod->methodToken);
							return MethodState::Aborted;
						}
						// The instruction suffix (here .i4) indicates the element size
						uint32_t* data = (uint32_t*)value1.Object;
						int32_t arraysize = *(data);
						int32_t targetType = *(data + 1);
						if (token != targetType)
						{
							ExceptionOccurred(currentFrame, SystemException::ArrayTypeMismatch, currentFrame->_executingMethod->methodToken);
							return MethodState::Aborted;
						}
						int32_t index = value2.Int32;
						if (index < 0 || index >= arraysize)
						{
							ExceptionOccurred(currentFrame, SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
							return MethodState::Aborted;
						}

						if (value1.Type == VariableKind::ValueArray)
						{
							*(data + 2 + index) = value3.Int32;
						}
						else
						{
							// can only be an object now
							*(data + 2 + index) = (uint32_t)value3.Object;
						}
					}
					break;
				case CEE_LDELEM:
				{
					Variable value2 = stack->top();
					stack->pop();
					Variable value1 = stack->top();
					stack->pop();
					if (value1.Object == nullptr)
					{
						ExceptionOccurred(currentFrame, SystemException::NullReference, currentFrame->_executingMethod->methodToken);
						return MethodState::Aborted;
					}
					// The instruction suffix (here .i4) indicates the element size
					uint32_t* data = (uint32_t*)value1.Object;
					int32_t arraysize = *(data);
					int32_t targetType = *(data + 1);
					if (token != targetType)
					{
						ExceptionOccurred(currentFrame, SystemException::ArrayTypeMismatch, currentFrame->_executingMethod->methodToken);
						return MethodState::Aborted;
					}
					int32_t index = value2.Int32;
					if (index < 0 || index >= arraysize)
					{
						ExceptionOccurred(currentFrame, SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
						return MethodState::Aborted;
					}

					Variable v1;
					if (value1.Type == VariableKind::ValueArray)
					{
						// This should always exist
						ClassDeclaration& ty = _classes.at(token);
						v1.Int32 = *(data + 2 + index);
						// Value arrays are expected to have just one type for now
						// TODO: This needs a bitwise copy of the whole instance, but how do we handle it if sizeof(ValueType) > 4 (so that it doesn't fit in a stack slot)?
						v1.Type = ty.fieldTypes[0].Type;
					}
					else
					{
						// can only be an object now
						v1.Object = (void*)*(data + 2 + index);
						v1.Type = VariableKind::Object;
					}
					stack->push(v1);
					break;
				}
				case CEE_BOX:
				{
					Variable value1 = stack->top();
					stack->pop();
					if (_classes.contains(token))
					{
						ClassDeclaration& ty = _classes.at(token);
						Variable r;
						if (ty.ValueType)
						{
							// TODO: This requires special handling for types derived from Nullable<T>

							// Here, the boxed size is expected
							size_t sizeOfClass = SizeOfClass(&ty);
							TRACE(Firmata.sendString(F("Boxed class size is 0x"), sizeOfClass));
							void* ret = AllocGcInstance(sizeOfClass);
							if (ret == nullptr)
							{
								ExceptionOccurred(currentFrame, SystemException::OutOfMemory, token);
								return MethodState::Aborted;
							}

							// Save a reference to the class declaration in the first entry of the newly created instance.
							// this will serve as vtable.
							ClassDeclaration** vtable = (ClassDeclaration**)ret;
							*vtable = &ty;
							
							r.Object = ret;
							r.Type = VariableKind::Object;
							// Copy the value to the newly allocated boxed instance (just the first member for now)
							SetField(ty, value1, r, 0);
						}
						else
						{
							// If ty is a reference type, box does nothing
							r.Object = value1.Object;
							r.Type = value1.Type;
						}
						stack->push(r);
					}
					else
					{
						Firmata.sendStringf(F("Unknown type token in BOX instruction: %lx"), 4, token);
						ExceptionOccurred(currentFrame, SystemException::ClassNotFound, token);
						return MethodState::Aborted;
					}
					break;
				}
				case CEE_UNBOX_ANY:
				{
					// Note: UNBOX and UNBOX.any are quite different
					Variable value1 = stack->top();
					stack->pop();
					if (_classes.contains(token))
					{
						ClassDeclaration& ty = _classes.at(token);
						Variable r;
						if (ty.ValueType)
						{
							// TODO: This requires special handling for types derived from Nullable<T>

							// Note: Still not sure how large value types need to be handled (with size > 4 or multiple fields)
							r = GetField(ty, value1, 0);
						}
						else
						{
							// If ty is a reference type, unbox.any does nothing fancy, except a type test
							MethodState result = IsAssignableFrom(ty, value1);
							if (result != MethodState::Running)
							{
								return result;
							}
							r.Object = value1.Object;
							r.Type = value1.Type;
						}
						stack->push(r);
					}
					else
					{
						Firmata.sendStringf(F("Unknown type token in BOX instruction: %lx"), 4, token);
						ExceptionOccurred(currentFrame, SystemException::ClassNotFound, token);
						return MethodState::Aborted;
					}
					break;
				}
				case CEE_CASTCLASS:
				{
					Variable value1 = stack->top();
					stack->pop();
					if (value1.Object == nullptr)
					{
						// Castclass on a null pointer just returns the same
						stack->push(value1);
						break;
					}
					if (!_classes.contains(token))
					{
						ExceptionOccurred(currentFrame, SystemException::ClassNotFound, token);
						return MethodState::Aborted;
					}

					ClassDeclaration& ty = _classes.at(token);
					if (IsAssignableFrom(ty, value1) == MethodState::Running)
					{
						// if the cast is fine, just return the original object
						stack->push(value1);
						break;
					}
					// The cast fails. Throw a InvalidCastException
					ExceptionOccurred(currentFrame, SystemException::InvalidCast, ty.ClassToken);
					return MethodState::Aborted;
				}
				default:
					InvalidOpCode(PC, instr);
					return MethodState::Aborted;
				}
				break;
			}
			case InlineTok:
				{
					int token = static_cast<int32_t>(((u32)pCode[PC]) + (((u32)pCode[PC + 1]) << 8) + (((u32)pCode[PC + 2]) << 16) + (((u32)pCode[PC + 3]) << 24));
					PC += 4;
					switch(instr)
					{
					case CEE_LDTOKEN:
						{
							if (_constants.contains(token))
							{
								byte* data = _constants.at(token);
								intermediate.Object = data;
								intermediate.Type = VariableKind::RuntimeFieldHandle;
								stack->push(intermediate);
							}
							else if (_classes.contains(token))
							{
								intermediate.Int32 = token;
								intermediate.Type = VariableKind::RuntimeTypeHandle;
								stack->push(intermediate);
							}
							else
							{
								// Unsupported case
								ExceptionOccurred(currentFrame, SystemException::ClassNotFound, token);
								return MethodState::Aborted;
							}
						}
						break;
					default:
						InvalidOpCode(PC, instr);
						return MethodState::Aborted;
					}
				}
				break;
			default:
				InvalidOpCode(PC, instr);
				return MethodState::Aborted;
        }
	}

	currentFrame->UpdatePc(PC);

	TRACE(startTime = (micros() - startTime) / NUM_INSTRUCTIONS_AT_ONCE);
	TRACE(Firmata.sendString(F("Interrupting method at 0x"), PC));
	TRACE(Firmata.sendStringf(F("Average time per IL instruction: %ld microseconds"), 4, startTime));

	// We interrupted execution to not waste to much time here - the parent will return to us asap
	return MethodState::Running;
}

/// <summary>
/// Returns MethodState::Running if true, MethodState::Aborted otherwise. The caller must decide on context whether
/// that should throw an exception
/// </summary>
/// <param name="typeToAssignTo">The type that should be the assignment target</param>
/// <param name="object">The value that should be assigned</param>
/// <returns>See above</returns>
MethodState FirmataIlExecutor::IsAssignableFrom(ClassDeclaration& typeToAssignTo, const Variable& object)
{
	byte* o = (byte*)object.Object;
	ClassDeclaration* sourceType = (ClassDeclaration*)(*(int32_t*)o);
	// If the types are the same, they're assignable
	if (sourceType->ClassToken == typeToAssignTo.ClassToken)
	{
		return MethodState::Running;
	}

	// Special handling for types derived from "System.Type", because this runtime implements a subset of the type library only
	if (sourceType->ClassToken == 2 && (typeToAssignTo.ClassToken == 5 || typeToAssignTo.ClassToken == 6))
	{
		// Casting System.Type to System.RuntimeType or System.Reflection.TypeInfo is fine
		return MethodState::Running;
	}

	// If sourceType derives from typeToAssign, that works as well
	ClassDeclaration* parent = &_classes.at(sourceType->ParentToken);
	while (parent != nullptr)
	{
		if (parent->ClassToken == typeToAssignTo.ClassToken)
		{
			return MethodState::Running;
		}
		
		parent = &_classes.at(parent->ParentToken);
	}

	// Note: List is not filled yet.
	if (typeToAssignTo.interfaceTokens.contains(sourceType->ClassToken))
	{
		return MethodState::Running;
	}

	return MethodState::Stopped;
}

/// <summary>
/// Creates a class directly by its type (used i.e. to create instances of System::Type)
/// </summary>
void* FirmataIlExecutor::CreateInstanceOfClass(int32_t typeToken, SystemException* exception)
{
	ClassDeclaration& cls = _classes.at(typeToken);
	TRACE(Firmata.sendString(F("Class to create is 0x"), cls.ClassToken));
	// Compute sizeof(class)
	size_t sizeOfClass = SizeOfClass(&cls);

	TRACE(Firmata.sendString(F("Class size is 0x"), sizeOfClass));
	void* ret = AllocGcInstance(sizeOfClass);
	if (ret == nullptr)
	{
		*exception = SystemException::OutOfMemory;
		Firmata.sendString(F("Not enough memory for allocating an instance of 0x"), cls.ClassToken);
		return nullptr;
	}

	// Save a reference to the class declaration in the first entry of the newly created instance.
	// this will serve as vtable.
	ClassDeclaration** vtable = (ClassDeclaration**)ret;
	*vtable = &cls;
	return ret;
}

void* FirmataIlExecutor::CreateInstance(int32_t ctorToken, SystemException* exception)
{
	TRACE(Firmata.sendString(F("Creating instance via .ctor 0x"), ctorToken));
	for (auto iterator = _classes.begin(); iterator != _classes.end(); ++iterator)
	{
		ClassDeclaration& cls = iterator.second();
		// TRACE(Firmata.sendString(F("Class "), cls.ClassToken));
		for(size_t j = 0; j < cls.methodTypes.size(); j++)
		{
			Method& member = cls.methodTypes.at(j);
			// TRACE(Firmata.sendString(F("Member "), member.Uint32));
			if (member.token == ctorToken)
			{
				TRACE(Firmata.sendString(F("Class to create is 0x"), cls.ClassToken));
				// The constructor that was called belongs to this class
				// Compute sizeof(class)
				size_t sizeOfClass = SizeOfClass(&cls);

				TRACE(Firmata.sendString(F("Class size is 0x"), sizeOfClass));
				void* ret = AllocGcInstance(sizeOfClass);
				if (ret == nullptr)
				{
					*exception = SystemException::OutOfMemory;
					Firmata.sendString(F("Not enough memory for allocating an instance of 0x"), cls.ClassToken);
					return nullptr;
				}

				// Save a reference to the class declaration in the first entry of the newly created instance.
				// this will serve as vtable.
				ClassDeclaration** vtable = (ClassDeclaration**)ret;
				*vtable = &cls;
				return ret;
			}
		}
	}

	*exception = SystemException::MissingMethod;
	Firmata.sendString(F("No class found with that .ctor"));
	return nullptr;
}

/// <summary>
/// Returns the size of the memory that needs to be allocated for a dynamic class instance.
/// For value types, this returns the boxed size (which is at least the size of a variable slot + vtable)
/// </summary>
uint16_t FirmataIlExecutor::SizeOfClass(ClassDeclaration* cls)
{
	// + (platform specific) vtable* size
	if (cls->ValueType)
	{
		if  (cls->ClassDynamicSize > 0 && cls->ClassDynamicSize <= Variable::datasize())
		{
			return (uint16_t)(Variable::datasize() + sizeof(void*));
		}
	}
	return cls->ClassDynamicSize + sizeof(void*);
}

ExecutionError FirmataIlExecutor::LoadClassSignature(u32 classToken, u32 parent, u16 dynamicSize, u16 staticSize, u16 flags, u16 offset, byte argc, byte* argv)
{
	Firmata.sendStringf(F("Class %lx has parent %lx and size %d."), 10, classToken, parent, dynamicSize);
	bool alreadyExists = _classes.contains(classToken);

	ClassDeclaration* decl;
	if (alreadyExists)
	{
		decl = &_classes.at(classToken);
	}
	else
	{
		// The only flag is currently "isvaluetype"
		bool isValueType = flags != 0;
		if (!isValueType)
		{
			// For reference types, the class size given is shifted by two (because it's always a multiple of 4).
			// Value types are not expected to exceed more than a few words
			dynamicSize = dynamicSize << 2;
		}
		ClassDeclaration newC(classToken, parent, dynamicSize, staticSize, isValueType);
		_classes.insert(classToken, newC);
		decl = &_classes.at(classToken);
	}
	
	// Reinit
	if (offset == 0)
	{
		decl->fieldTypes.clear();
		decl->methodTypes.clear();
	}

	if (argc == 0)
	{
		// A class without a ctor or a field - this is an interface
		return ExecutionError::None;
	}

	// A member, either a field or a method (only ctors and virtual methods provided here)
	int i = 0;
	Variable v;
	v.Type = (VariableKind)argv[i];
	v.Int32 = DecodePackedUint32(argv + i + 1); // uses 5 bytes
	i += 6;
	if (v.Type != VariableKind::Method)
	{
		decl->fieldTypes.push_back(v);
		return ExecutionError::None;
	}

	// if there are more arguments, these are the method tokens that point back to this implementation
	Method me;
	me.token = v.Int32;
	decl->methodTypes.push_back(me);
	// Weird reference handling, but since we have no working copy-ctor for vector<T>, we need to copy it before we start filling
	Method& me2 = decl->methodTypes.back();
	for (;i < argc - 4; i += 5)
	{
		// These are tokens of possible base implementations of this method - that means methods whose dynamic invocation target will be this method.
		me2.declarationTokens.push_back(DecodePackedUint32(argv + i));
	}

	return ExecutionError::None;
}

ExecutionError FirmataIlExecutor::LoadInterfaces(int32_t classToken, byte argc, byte* argv)
{
	if (!_classes.contains(classToken))
	{
		return ExecutionError::InvalidArguments;
	}

	ClassDeclaration& ty = _classes.at(classToken);
	for (int i = 0; i < argc;)
	{
		int token = DecodePackedUint32(argv + i);
		ty.interfaceTokens.push_back(token);
		i += 5;
	}
	return ExecutionError::None;
}

IlCode* FirmataIlExecutor::ResolveToken(IlCode* code, int32_t token)
{
	// All tokens are resolved and combined into a single namespace by the host already.
	// However, if we want to still use the address patching (which is a good idea), we need to
	// figure out how we determine the two cases (i.e. find illegal memory addresses, or also patch the opcode)
	/*
	IlCode* method;
	if (((token >> 24) & 0xFF) == 0x0)
	{
		// We've previously patched the code directly with the lower 3 bytes of the method pointer
		// Now we extend that again with the RAM base address (0x20000000 on a Due, 0x0 on an Uno)
		token = token | ((u32)_firstMethod & 0xFF000000);
		return (IlCode*)token;
	}
	if (((token >> 24) & 0xFF) == 0x0A)
	{
		// Use the token map first
		int mapEntry = 0;

		method = GetMethodByCodeReference(code->codeReference);
		int32_t* entries = method -> tokenMap;
		while (mapEntry < method->tokenMapEntries * 2)
		{
			int32_t memberRef = entries[mapEntry + 1];
			// TRACE(Firmata.sendString(F("MemberRef token 0x"), entries[mapEntry + 1]));
			// TRACE(Firmata.sendString(F("MethodDef token 0x"), entries[mapEntry]));
			if (memberRef == token)
			{
				token = entries[mapEntry];
				break;
			}
			mapEntry += 2;
		}
	}

*/
	return GetMethodByToken(code, token);
}

void FirmataIlExecutor::SendAckOrNack(ExecutorCommand subCommand, ExecutionError errorCode)
{
	Firmata.startSysex();
	Firmata.write(SCHEDULER_DATA);
	Firmata.write((byte)(errorCode == ExecutionError::None ? ExecutorCommand::Ack : ExecutorCommand::Nack));
	Firmata.write((byte)subCommand);
	Firmata.write((byte)errorCode);
	Firmata.endSysex();
}


void FirmataIlExecutor::AttachToMethodList(IlCode* newCode)
{
	if (_firstMethod == nullptr)
	{
		_firstMethod = newCode;
		return;
	}

	IlCode* parent = _firstMethod;
	while (parent->next != nullptr)
	{
		parent = parent->next;
	}

	parent->next = newCode;
}

IlCode* FirmataIlExecutor::GetMethodByCodeReference(u16 codeReference)
{
	IlCode* current = _firstMethod;
	while (current != nullptr)
	{
		if (current->codeReference == codeReference)
		{
			return current;
		}

		current = current->next;
	}

	TRACE(Firmata.sendString(F("Reference not found: "), codeReference));
	return nullptr;
}

IlCode* FirmataIlExecutor::GetMethodByToken(IlCode* code, int32_t token)
{
	// Methods in the method list have their top nibble patched with the module ID.
	// if the token to be searched has module 0, we need to add the current module Id (from the
	// token of the currently executing method)

	IlCode* current = _firstMethod;
	while (current != nullptr)
	{
		if (current->methodToken == token)
		{
			return current;
		}

		current = current->next;
	}

	Firmata.sendString(F("Token not found: "), token);
	return nullptr;
}

OPCODE DecodeOpcode(const BYTE *pCode, DWORD *pdwLen)
{
    OPCODE opcode;

    *pdwLen = 1;
    opcode = OPCODE(pCode[0]);
    switch(opcode) {
        case CEE_PREFIX1:
            opcode = OPCODE(pCode[1] + 256);
            if (opcode < 0 || opcode >= CEE_COUNT)
                opcode = CEE_COUNT;
            *pdwLen = 2;
            break;
        case CEE_PREFIXREF:
        case CEE_PREFIX2:
        case CEE_PREFIX3:
        case CEE_PREFIX4:
        case CEE_PREFIX5:
        case CEE_PREFIX6:
        case CEE_PREFIX7:
            *pdwLen = 3;
            return CEE_COUNT;
        default:
            break;
        }
    return opcode;
}

void FirmataIlExecutor::reset()
{
	auto method = _firstMethod;

	while (method != nullptr)
	{
		auto current = method;
		method = method->next;
		delete current;
	}

	_firstMethod = nullptr;

	_classes.clear();

	for (auto c = _constants.begin(); c != _constants.end(); ++c)
	{
		free(c.second());
	}
	
	_constants.clear();

	Firmata.sendString(F("Execution memory cleared. Free bytes: 0x"), freeMemory());
}
