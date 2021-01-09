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
#include "SelfTest.h"
#include "HardwareAccess.h"
#include <stdint.h>

typedef byte BYTE;
typedef uint32_t DWORD;

// #define TRACE(x) x
#define TRACE(x)

#pragma warning (error:4244)

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

OPCODE DecodeOpcode(const BYTE *pCode, u16 *pdwLen);

boolean FirmataIlExecutor::handlePinMode(byte pin, int mode)
{
  // This class does not handle individual pin modes
  return false;
}

FirmataIlExecutor::FirmataIlExecutor()
{
	_methodCurrentlyExecuting = nullptr;
	_firstMethod = nullptr;
	SelfTest test;
	test.PerformSelfTest();
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

uint64_t FirmataIlExecutor::DecodePackedUint64(byte* argv)
{
	uint64_t result = 0;
	result += DecodePackedUint32(argv);
	result += static_cast<uint64_t>(DecodePackedUint32(argv + 5)) << 32;
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
	return (uint16_t)result;
}

bool FirmataIlExecutor::IsExecutingCode()
{
	return _methodCurrentlyExecuting != nullptr;
}

// TODO: Keep track, implement GC, etc...
byte* AllocGcInstance(size_t bytes)
{
	byte* ret = (byte*)malloc(bytes);
	if (ret == nullptr)
	{
		return nullptr;
	}
	
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

	int topLevelMethod = _methodCurrentlyExecuting->MethodIndex();

	// Ignore result - any exceptions that just occurred will be dropped by the abort request
	UnrollExecutionStack();

	// Send a status report, to end any process waiting for this method to return.
	SendExecutionResult((u16)topLevelMethod, nullptr, Variable(), MethodState::Killed);
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
	SendExecutionResult((u16)methodindex, ex, retVal, execResult);

	// The method ended
	delete _methodCurrentlyExecuting;
	_methodCurrentlyExecuting = nullptr;
}

ExecutionError FirmataIlExecutor::LoadIlDeclaration(u16 codeReference, int flags, byte maxStack, byte argCount,
	NativeMethod nativeMethod, int token)
{
	TRACE(Firmata.sendStringf(F("Loading declaration for codeReference %d, Flags 0x%x"), 6, (int)codeReference, (int)flags));
	MethodBody* method = GetMethodByCodeReference(codeReference);
	if (method != nullptr)
	{
		method->Clear();
		method->codeReference = codeReference;
	}
	else
	{
		method = new MethodBody();
		method->codeReference = codeReference;
		// And immediately attach to the list
		AttachToMethodList(method);
	}

	method->methodFlags = (byte)flags;
	method->maxStack = maxStack;
	method->nativeMethod = nativeMethod;
	method->numArgs = argCount; // Argument count
	method->methodToken = token;

	TRACE(Firmata.sendStringf(F("Loaded metadata for token 0x%lx, Flags 0x%x"), 6, token, (int)flags));
	return ExecutionError::None;
}

ExecutionError FirmataIlExecutor::LoadMethodSignature(u16 codeReference, byte signatureType, byte argc, byte* argv)
{
	TRACE(Firmata.sendStringf(F("Loading Declaration."), 0));
	MethodBody* method = GetMethodByCodeReference(codeReference);
	if (method == nullptr)
	{
		// This operation is illegal if the method is unknown
		Firmata.sendString(F("LoadMethodSignature for unknown codeReference"));
		return ExecutionError::InvalidArguments;
	}

	VariableDescription desc;
	int size;
	if (signatureType == 0)
	{
		// Argument types. (This can be called multiple times for very long argument lists)
		for (byte i = 0; i < argc - 1;) // The last byte in the arglist is the END_SYSEX byte
		{
			desc.Type = (VariableKind)argv[i];
			size = argv[i + 1] | argv[i + 2] << 7;
			desc.Size = (u16)(size << 2); // Size is given as multiples of 4 (so we can again gain the full 16 bit with only 2 7-bit values)
			method->argumentTypes.push_back(desc);
			i += 3;
		}
	}
	else if (signatureType == 1)
	{
		// Type of the locals (also possibly called several times)
		for (byte i = 0; i < argc - 1;)
		{
			desc.Type = (VariableKind)argv[i];
			size = argv[i + 1] | argv[i + 2] << 7;
			desc.Size = (u16)(size << 2); // Size is given as multiples of 4 (so we can again gain the full 16 bit with only 2 7-bit values)
			method->localTypes.push_back(desc);
			i += 3;
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
	MethodBody* method = GetMethodByCodeReference(codeReference);
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

		int numToDecode = num7BitOutbytes(argc);
		Encoder7Bit.readBinary(numToDecode, argv, decodedIl);
		method->methodLength = codeLength;
		method->methodIl = decodedIl;
	}
	else 
	{
		byte* decodedIl = method->methodIl + offset;
		int numToDecode = num7BitOutbytes(argc);
		Encoder7Bit.readBinary(numToDecode, argv, decodedIl);
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

/// <summary>
/// Simple 7 bit encoding for a 32 bit value. Inverse to the above. Note that this is not equivalent to SendPackedUint32
/// </summary>
void FirmataIlExecutor::SendUint32(uint32_t value)
{
	// It doesn't matter whether the shift is arithmetic or not here
	Firmata.sendValueAsTwo7bitBytes(value & 0xFF);
	Firmata.sendValueAsTwo7bitBytes(value >> 8 & 0xFF);
	Firmata.sendValueAsTwo7bitBytes(value >> 16 & 0xFF);
	Firmata.sendValueAsTwo7bitBytes(value >> 24 & 0xFF);
}

void FirmataIlExecutor::SendExecutionResult(u16 codeReference, RuntimeException* ex, Variable returnValue, MethodState execResult)
{
	// Reply format:
	// bytes 0-1: Reference of method that exited
	// byte 2: Status. See below
	// byte 3: Number of integer values returned
	// bytes 4+: Return values

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
		if (returnValue.fieldSize() <= 4)
		{
			Firmata.write(8); // Number of bytes that follow
			SendUint32(returnValue.Int32);
		}
		else
		{
			Firmata.write(16); // Number of bytes that follow
			SendUint32((uint32_t)returnValue.Int64);
			SendUint32((uint32_t)(returnValue.Int64 >> 32));
		}
		
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

void FirmataIlExecutor::SendPackedInt64(int64_t value)
{
	SendPackedInt32(value & 0xFFFFFFFF);
	SendPackedInt32(value >> 32);
}


void FirmataIlExecutor::DecodeParametersAndExecute(u16 codeReference, byte argc, byte* argv)
{
	Variable result;
	MethodBody* method = GetMethodByCodeReference(codeReference);
	TRACE(Firmata.sendStringf(F("Code execution for %d starts. Stack Size is %d."), 4, codeReference, method->maxStack));
	ExecutionState* rootState = new ExecutionState(codeReference, method->maxStack, method);
	_methodCurrentlyExecuting = rootState;
	int idx = 0;
	for (int i = 0; i < method->numArgs; i++)
	{
		VariableDescription desc = method->argumentTypes[i];
		VariableKind k = desc.Type;
		if (((int)k & 16) == 0)
		{
			rootState->SetArgumentValue(i, DecodeUint32(argv + idx), k);
			idx += 8;
		}
		else
		{
			uint64_t combined = DecodeUint32(argv + idx);
			combined += static_cast<uint64_t>(DecodeUint32(argv + idx + 8)) << 32;
			rootState->SetArgumentValue(i, combined, k);
			idx += 16;
		}
	}

	MethodState execResult = ExecuteIlCode(rootState, &result);
	if (execResult == MethodState::Running)
	{
		// The method is still running
		return;
	}

	TRACE(Firmata.sendStringf(F("Code execution for %d has ended normally."), 2, codeReference));
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

void FirmataIlExecutor::GetTypeFromHandle(ExecutionState* currentFrame, Variable& result, Variable type)
{
	// Create an instance of "System.Type"
	void* newObjInstance = CreateInstanceOfClass(2, 0);

	ClassDeclaration* cls = (ClassDeclaration*)(*(int32_t*)newObjInstance);
			
	// This is now a quite normal object instance
	result.Type = VariableKind::Object;
	result.Object = newObjInstance;
	// Set the "_internalType" member to point to the class declaration
	SetField4(*cls, type, result, 0);
}

int FirmataIlExecutor::GetHandleFromType(Variable& object) const
{
	ClassDeclaration* cls = (ClassDeclaration*)(*(int32_t*)object.Object);
	return cls->ClassToken;
}

const int LogTable32[32] = {
	 0,  9,  1, 10, 13, 21,  2, 29,
	11, 14, 16, 18, 22, 25,  3, 30,
	 8, 12, 20, 28, 15, 17, 24,  7,
	19, 27, 23,  6, 26,  5,  4, 31 };

int Log2_32(uint32_t value)
{
	value |= value >> 1;
	value |= value >> 2;
	value |= value >> 4;
	value |= value >> 8;
	value |= value >> 16;
	return LogTable32[(uint32_t)(value * 0x07C4ACDD) >> 27];
}

const int TrailingZeroTable[32] =
{
	0, 1, 28, 2, 29, 14, 24, 3,
	30, 22, 20, 15, 25, 17, 4, 8,
	31, 27, 13, 23, 21, 19, 16, 7,
	26, 12, 18, 6, 11, 05, 10, 9
};

int TrailingZeroCount(uint32_t value)
{
	// Unguarded fallback contract is 0->0, BSF contract is 0->undefined
	if (value == 0)
	{
		return 32;
	}

	return TrailingZeroTable[(uint32_t)(value * 0x077CB531u) >> 27];
}

// Executes the given OS function. Note that args[0] is the this pointer for instance methods
void FirmataIlExecutor::ExecuteSpecialMethod(ExecutionState* currentFrame, NativeMethod method, const VariableVector& args, Variable& result)
{
	if (HardwareAccess::ExecuteHardwareAccess(currentFrame, method, args, result))
	{
		return;
	}
	switch (method)
	{
	
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
	
	case NativeMethod::RuntimeHelpersInitializeArray:
		ASSERT(args.size() == 2);
		{
			Variable array = args[0]; // Target array
			ASSERT(array.Type == VariableKind::ValueArray);
			Variable field = args[1]; // Runtime field type instance
			ASSERT(field.Type == VariableKind::RuntimeFieldHandle);
			uint32_t* data = (uint32_t*)array.Object;
			// TODO: Maybe we should directly store the class pointer instead of the token - or at least use a fast map<> implementation
			ClassDeclaration& ty = _classes.at(*(data + 2));
			int32_t size = *(data + 1);
			byte* targetPtr = (byte*)(data + 3);
			memcpy(targetPtr, field.Object, size * ty.ClassDynamicSize);
		}
		break;
	case NativeMethod::TypeGetTypeFromHandle:
		ASSERT(args.size() == 1);
		{
			Variable type = args[0]; // type handle
			ASSERT(type.Type == VariableKind::RuntimeTypeHandle);
			GetTypeFromHandle(currentFrame, result, type);
		}
		break;
	case NativeMethod::ObjectEquals: // this is an instance method with 1 argument
	case NativeMethod::ObjectReferenceEquals: // This is a static method with 2 arguments, but implicitly the same as the above
		ASSERT(args.size() == 2);
			result.Type = VariableKind::Boolean;
			result.Boolean = args[0].Object == args[1].Object; // This implements reference equality (or binary equality for value types)
		break;
	case NativeMethod::ObjectMemberwiseClone:
		ASSERT(args.size() == 1); // just the "this" pointer
		{
			Variable& self = args[0];
			ClassDeclaration* ty = GetClassDeclaration(self);
			if (ty->ClassToken == (int)KnownTypeTokens::String)
			{
				// Strings are immutable (and have dynamic length, therefore the shortcut is easier)
				result = self;
				break;
			}

			if (ty->ClassToken == (int)KnownTypeTokens::Array)
			{
				// Arrays need special handling, since they have dynamic length
				uint32_t* data = (uint32_t*)self.Object;
				uint32_t length = *(data + 1);
				uint32_t contentType = *(data + 2);
				int bytesAllocated = AllocateArrayInstance(contentType, length, result);
				memcpy(AddBytes(result.Object, 12), AddBytes(self.Object, 12), bytesAllocated);
				break;
			}
		
			// TODO: Validate this operation on value types
			if (ty->ValueType)
			{
				throw ClrException(SystemException::NotSupported, currentFrame->_executingMethod->methodToken);
			}

			void* data = CreateInstanceOfClass(ty->ClassToken, 0);
			memcpy(AddBytes(data, 4), AddBytes(args[0].Object, 4), ty->ClassDynamicSize);
			result.Type = self.Type;
			result.setSize(4);
			result.Object = data;
			break;
		}
	case NativeMethod::TypeMakeGenericType:
		ASSERT(args.size() == 2);
		{
			Variable type = args[0]; // type or type handle
			Variable arguments = args[1]; // An array of types
			ASSERT(arguments.Type == VariableKind::ReferenceArray);
			uint32_t* data = (uint32_t*)arguments.Object;
			int32_t size = *(data + 1);
			ClassDeclaration& typeOfType = _classes.at(2);
			if (size != 1)
			{
				throw ClrException(SystemException::NotSupported, currentFrame->_executingMethod->methodToken);
			}
			uint32_t parameter = *(data + 3); // First element of array
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
				genericToken = token.Int32 & GENERIC_TOKEN_MASK; // Make sure this is a generic token (allows reusing this function for TypeCreateInstanceForAnotherGenericType)
			}
			else
			{
				throw ClrException(SystemException::NotSupported, currentFrame->_executingMethod->methodToken);
			}

			// The sum of a generic type and its only type argument will yield the token for the combination
			type.Int32 = genericToken + argumentType.Int32;
			type.Type = VariableKind::RuntimeTypeHandle;
			GetTypeFromHandle(currentFrame, result, type);
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
			token = token & GENERIC_TOKEN_MASK;
			token = token + tok2.Int32;
			
			if (!_classes.contains(token))
			{
				throw ClrException(SystemException::ClassNotFound, token);
			}
			void* ptr = CreateInstanceOfClass(token, 0);

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

			// a type can be assigned to it's nullable variant
			if (ownToken.Int32 == (otherToken.Int32 & ~NULLABLE_TOKEN_MASK))
			{
				result.Boolean = true;
				break;
			}
			
			ClassDeclaration& t1 = _classes.at(ownToken.Int32);
			if (!_classes.contains(otherToken.Int32))
			{
				throw ClrException(SystemException::ClassNotFound, otherToken.Int32);
			}
			
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

			if (t1.interfaceTokens.contains(t2.ClassToken))
			{
				result.Boolean = true;
				break;
			}
			result.Boolean = false;
		}
		break;
	case NativeMethod::TypeIsAssignableFrom:
	{
		// Returns true if "this" type can be assigned from a variable of type "other"
		// This is almost the inverse of the above, except for the identity case
		// TODO: Combine the two
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

		// a type can be assigned to it's nullable variant
		if ((ownToken.Int32 & ~NULLABLE_TOKEN_MASK) == otherToken.Int32)
		{
			result.Boolean = true;
			break;
		}

		ClassDeclaration& t1 = _classes.at(ownToken.Int32);
		if (!_classes.contains(otherToken.Int32))
		{
			throw ClrException(SystemException::ClassNotFound, otherToken.Int32);
		}

		ClassDeclaration& t2 = _classes.at(otherToken.Int32);

		// Am I a base class of the other?
		ClassDeclaration* parent = &_classes.at(t2.ParentToken);
		while (parent != nullptr)
		{
			if (parent->ClassToken == t1.ClassToken)
			{
				result.Boolean = true;
				break;
			}

			parent = &_classes.at(parent->ParentToken);
		}

		// Am I an interface the other implements?
		if (t2.interfaceTokens.contains(t1.ClassToken))
		{
			result.Boolean = true;
			break;
		}
		result.Boolean = false;
	}
	break;
	case NativeMethod::TypeGetGenericTypeDefinition:
		{
			ASSERT(args.size() == 1);
			Variable type1 = args[0]; // type1. An (instantiated) generic type
			ClassDeclaration& ty = _classes.at((int)KnownTypeTokens::Type);
			Variable tok1 = GetField(ty, type1, 0);
			int token = tok1.Int32;
			token = token & GENERIC_TOKEN_MASK;

			if (token == 0)
			{
				// Type was not generic -> throw InvalidOperationException
				throw ClrException(SystemException::InvalidOperation, token);
			}

			if (!_classes.contains(token))
			{
				throw ClrException(SystemException::ClassNotFound, token);
			}
			
			void* ptr = CreateInstanceOfClass((int)KnownTypeTokens::Type, 0);
			
			result.Object = ptr;
			result.Type = VariableKind::Object;

			tok1.Int32 = token;
			tok1.Type = VariableKind::Int32;
			SetField4(ty, tok1, result, 0);
		}
		break;
	case NativeMethod::TypeIsEnum:
		ASSERT(args.size() == 1);
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
	case NativeMethod::TypeGetGenericArguments:
		ASSERT(args.size() == 1);
		{
			// Get the type of the generic argument as an array. It is similar to GenericTypeDefinition, but returns the other part (for a single generic argument)
			// Note: This function has a few other cases, which we do not handle here.
			// In particular, we only support the case for one element
			ASSERT(args.size() == 1);
			Variable type1 = args[0]; // type1. An (instantiated) generic type
			ClassDeclaration& ty = _classes.at((int)KnownTypeTokens::Type);
			Variable tok1 = GetField(ty, type1, 0);
			int token = tok1.Int32;
			// If the token is a generic (open) type, we return "type"
			if ((int)(token & GENERIC_TOKEN_MASK) == token)
			{
				token = (int)KnownTypeTokens::Type;
			}
			else
			{
				// otherwise we return the type of the generic argument
				token = token & ~GENERIC_TOKEN_MASK;
			}

			if (!_classes.contains(token))
			{
				throw ClrException(SystemException::ClassNotFound, token);
			}

			void* ptr = CreateInstanceOfClass((int)KnownTypeTokens::Type, 0);

			Variable t1;
			t1.Object = ptr;
			t1.Type = VariableKind::Object;

			tok1.Int32 = token;
			SetField4(ty, tok1, t1, 0);
			AllocateArrayInstance((int)KnownTypeTokens::Type, 1, result);
			uint32_t* data = (uint32_t*)result.Object;
			// Set the first (and for us, only) element of the array to the type instance
			*(data + 3) = (uint32_t)ptr;
		}
		break;
	case NativeMethod::BitOperationsLog2SoftwareFallback:
		{
		ASSERT(args.size() == 1);
		result.Int32 = Log2_32(args[0].Uint32);
		result.Type = VariableKind::Int32;
		}
		break;
	case NativeMethod::BitOperationsTrailingZeroCount:
		{
		ASSERT(args.size() == 1);
		result.Int32 = TrailingZeroCount(args[0].Uint32);
		result.Type = VariableKind::Int32;
		}
		break;
	case NativeMethod::UnsafeNullRef:
		{
			// This just returns a null pointer
		result.Object = nullptr;
		result.Type = VariableKind::AddressOfVariable;
		}
		break;
	default:
		throw ClrException("Unknown internal method", SystemException::MissingMethod, currentFrame->_executingMethod->methodToken);
	}

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
		// Ignore static member here
		if ((handle->Type & VariableKind::StaticMember) != VariableKind::Void)
		{
			continue;
		}

		if (idx == fieldNo)
		{
			// Found the member
			// TODO: This is just wrong: Use a reference instead (not critical as long as this method is only
			// used from native methods, where the actual size of the arguments is known)
			Variable v;
			memcpy(&v.Object, (o + offset), handle->fieldSize());
			v.Type = handle->Type;
			return v;
		}

		offset += handle->fieldSize();
		idx++;
		if ((uint32_t)offset >= (SizeOfClass(&type)))
		{
			// Something is wrong.
			throw ExecutionEngineException("Member offset exceeds class size");
		}
	}
	
	throw ExecutionEngineException("Field not found in class.");
}


void FirmataIlExecutor::SetField4(ClassDeclaration& type, const Variable& data, Variable& instance, int fieldNo)
{
	uint32_t offset = sizeof(void*);
	offset += Variable::datasize() * fieldNo;
	memcpy(((byte*)instance.Object + offset), &(data.Int64), data.fieldSize());
}

ClassDeclaration* FirmataIlExecutor::GetClassDeclaration(Variable& obj)
{
	byte* o = (byte*)obj.Object;
	ClassDeclaration* vtable = (ClassDeclaration*)(*(int32_t*)o);
	return vtable;
}

/// <summary>
/// Gets the variable description for a field (of type Variable, because it includes the token as value)
/// </summary>
Variable FirmataIlExecutor::GetVariableDescription(ClassDeclaration* vtable, int32_t token)
{
	if (vtable->ParentToken > 1) // Token 1 is the token of System::Object, which does not have any fields, so we don't need to go there.
	{
		ClassDeclaration* parent = &_classes.at(vtable->ParentToken);
		Variable v = GetVariableDescription(parent, token);
		if (v.Type != VariableKind::Void)
		{
			return v;
		}
	}

	for (auto handle = vtable->fieldTypes.begin(); handle != vtable->fieldTypes.end(); ++handle)
	{
		if (handle->Int32 == token)
		{
			return *handle;
		}
	}

	return Variable(); // Not found
}

void FirmataIlExecutor::CollectFields(ClassDeclaration* vtable, vector<Variable*>& vector)
{
	// Do a prefix-recursion to collect all fields in the class pointed to by vtable and its bases. The updated
	// vector must be sorted base-class members first
	if (vtable->ParentToken > 1) // Token 1 is the token of System::Object, which does not have any fields, so we don't need to go there.
	{
		ClassDeclaration* parent = &_classes.at(vtable->ParentToken);
		CollectFields(parent, vector);
	}

	for (auto handle = vtable->fieldTypes.begin(); handle != vtable->fieldTypes.end(); ++handle)
	{
		vector.push_back(handle);
	}
}

/// <summary>
/// Load a value from field "token" of instance "obj". Returns the pointer to the location of the value (which might have arbitrary size)
/// </summary>
/// <param name="currentMethod">The method being executed. For error handling only</param>
/// <param name="obj">The instance which contains the field (can be an object, a reference to an object or a value type)</param>
/// <param name="token">The field token</param>
/// <param name="description">[Out] The description of the field returned</param>
/// <returns>Pointer to the data of the field</returns>
byte* FirmataIlExecutor::Ldfld(MethodBody* currentMethod, Variable& obj, int32_t token, VariableDescription& description)
{
	byte* o;
	ClassDeclaration* vtable;
	int offset;
	if (obj.Type == VariableKind::AddressOfVariable)
	{
		vtable = &ResolveClassFromFieldToken(token);
		offset = 0; // No extra header
		o = (byte*)obj.Object; // Data being pointed to
	}
	else if (obj.Type != VariableKind::Object)
	{
		// Ldfld from a value type needs one less indirection, but we need to get the type first.
		// The value type does not carry the type information. Lets derive it from the field token.
		// TODO: This is slow, not what one expects from accessing a value type
		vtable = &ResolveClassFromFieldToken(token);
		offset = 0; // No extra header
		o = (byte*)&obj.Int32; // Data is right there
	}
	else
	{
		o = (byte*)obj.Object;
		if (o == nullptr)
		{
			throw ClrException(SystemException::NullReference, token);
		}
		
		vtable = GetClassDeclaration(obj);

		// Assuming sizeof(void*) == sizeof(any pointer type)
		// and sizeof(void*) >= sizeof(int)
		// Our members start here
		offset = sizeof(void*);
	}

	// Early abort. Offsetting to a null ptr does not give a valid field
	if (o == nullptr)
	{
		throw ClrException(SystemException::NullReference, token);
	}
	
	vector<Variable*> allfields;
	CollectFields(vtable, allfields);
	for (auto handle1 = allfields.begin(); handle1 != allfields.end(); ++handle1)
	{
		// The type of handle1 is Variable**, because we must make sure not to use copy operations above
		Variable* handle = (*handle1);
		// Ignore static member here
		if ((handle->Type & VariableKind::StaticMember) != VariableKind::Void)
		{
			continue;
		}
		
		if (handle->Int32 == token)
		{
			// Found the member
			description.Marker = VARIABLE_DEFAULT_MARKER;
			description.Type = handle->Type;
			description.Size = handle->fieldSize();
			return o + offset;
		}

		offset += handle->fieldSize();
	}

	Firmata.sendStringf(F("Class %lx has no member %lx"), 8, vtable->ClassToken, token);
	throw ClrException("Token not found in class", SystemException::FieldAccess, token);
}


/// <summary>
/// Load a value from field "token" of instance "obj". Returns the pointer to the location of the value (which might have arbitrary size)
/// </summary>
/// <param name="obj">The instance which contains the field (can be an object, a reference to an object or a value type)</param>
/// <param name="token">The field token</param>
/// <returns>The return value (of type AddressOfVariable, managed pointer)</returns>
Variable FirmataIlExecutor::Ldflda(Variable& obj, int32_t token)
{
	byte* o;
	ClassDeclaration* vtable;
	int offset;
	if (obj.Type == VariableKind::AddressOfVariable)
	{
		vtable = &ResolveClassFromFieldToken(token);
		offset = 0; // No extra header
		o = (byte*)obj.Object; // Data being pointed to
	}
	else if (obj.Type != VariableKind::Object)
	{
		// Ldfld from a value type needs one less indirection, but we need to get the type first.
		// The value type does not carry the type information. Lets derive it from the field token.
		// TODO: This is slow, not what one expects from accessing a value type
		vtable = &ResolveClassFromFieldToken(token);
		offset = 0; // No extra header
		o = (byte*)&obj.Int32; // Data is right there
	}
	else
	{
		o = (byte*)obj.Object;
		if (o == nullptr)
		{
			throw ClrException(SystemException::NullReference, token);
		}

		vtable = GetClassDeclaration(obj);

		// Assuming sizeof(void*) == sizeof(any pointer type)
		// and sizeof(void*) >= sizeof(int)
		// Our members start here
		offset = sizeof(void*);
	}

	// Early abort. Offsetting to a null ptr does not give a valid field
	if (o == nullptr)
	{
		throw ClrException(SystemException::NullReference, token);
	}

	vector<Variable*> allfields;
	CollectFields(vtable, allfields);
	for (auto handle1 = allfields.begin(); handle1 != allfields.end(); ++handle1)
	{
		// The type of handle1 is Variable**, because we must make sure not to use copy operations above
		Variable* handle = (*handle1);
		// Ignore static member here
		if ((handle->Type & VariableKind::StaticMember) != VariableKind::Void)
		{
			continue;
		}

		if (handle->Int32 == token)
		{
			// Found the member
			Variable ret;
			ret.Marker = VARIABLE_DEFAULT_MARKER;
			ret.Type = VariableKind::AddressOfVariable;
			ret.setSize(4);
			ret.Object = o + offset;
			return ret;
		}

		offset += handle->fieldSize();
	}

	Firmata.sendStringf(F("Class %lx has no member %lx"), 8, vtable->ClassToken, token);
	throw ClrException(SystemException::FieldAccess, token);
}

/// <summary>
/// Load a value from a static field
/// </summary>
/// <param name="token">Token of the static field</param>
Variable& FirmataIlExecutor::Ldsfld(int token)
{
	if (_statics.contains(token))
	{
		return _statics.at(token);
	}

	if (_largeStatics.contains(token))
	{
		return _largeStatics.at(token);
	}
	// We get here if reading an uninitialized static field

	// Loads from uninitialized static fields sometimes happen if the initialization sequence is bugprone.
	// Create an instance of the value type with the default zero value but the correct type
	ClassDeclaration& cls = ResolveClassFromFieldToken(token);

	vector<Variable*> allfields;
	CollectFields(&cls, allfields);
	for (auto handle1 = allfields.begin(); handle1 != allfields.end(); ++handle1)
	{
		Variable* handle = (*handle1);
		// Only static members checked
		if ((handle->Type & VariableKind::StaticMember) == VariableKind::Void)
		{
			continue;
		}
		if (handle->Int32 == token)
		{
			// Found the member, create an empty variable
			Variable ret;
			ret.Marker = VARIABLE_DEFAULT_MARKER;
			ret.Type = handle->Type & ~VariableKind::StaticMember;
			ret.setSize(handle->fieldSize());
			if (ret.fieldSize() > 8)
			{
				Variable& data = _largeStatics.insert(token, ret);
				return data;
			}
			ret.Int64 = 0;
			
			_statics.insert(token, ret);
			// Need to re-get the reference(!) as the above insert does a copy
			// TODO: the above insert could directly return the reference
			return _statics.at(token);
		}
	}
	
	throw ClrException("Could not resolve field token ", SystemException::FieldAccess, token);
}


/// <summary>
/// Load a value address of a static field
/// </summary>
/// <param name="token">Token of the static field</param>
Variable FirmataIlExecutor::Ldsflda(int token)
{
	Variable ret;
	if (_statics.contains(token))
	{
		Variable& temp = _statics.at(token);
		ret.Type = VariableKind::AddressOfVariable;
		ret.Marker = VARIABLE_DEFAULT_MARKER;
		ret.setSize(4);
		ret.Object = &temp.Int32;
		return ret;
	}

	if (_largeStatics.contains(token))
	{
		Variable& temp = _largeStatics.at(token);
		ret.Type = VariableKind::AddressOfVariable;
		ret.Marker = VARIABLE_DEFAULT_MARKER;
		ret.setSize(4);
		ret.Object = &temp.Int32;
		return ret;
	}

	// We get here if reading an uninitialized static field

	// Loads from uninitialized static fields sometimes happen if the initialization sequence is bugprone.
	// Create an instance of the value type with the default zero value but the correct type

	ClassDeclaration& cls = ResolveClassFromFieldToken(token);

	vector<Variable*> allfields;
	CollectFields(&cls, allfields);
	for (auto handle1 = allfields.begin(); handle1 != allfields.end(); ++handle1)
	{
		Variable* handle = (*handle1);
		// Only static members checked
		if ((handle->Type & VariableKind::StaticMember) == VariableKind::Void)
		{
			continue;
		}
		if (handle->Int32 == token)
		{
			// Found the member
			ret.Marker = VARIABLE_DEFAULT_MARKER;
			ret.Type = handle->Type & ~VariableKind::StaticMember;
			ret.setSize(handle->fieldSize());
			ret.Int64 = 0;
			void* addr;
			if (ret.fieldSize() > 8)
			{
				addr = &_largeStatics.insert(token, ret).Int32;
			}
			else
			{
				_statics.insert(token, ret);

				// Get the final address on the static heap (ret above is copied on insert)
				addr = &_statics.at(token).Int32;
			}
			
			ret.Marker = VARIABLE_DEFAULT_MARKER;
			ret.setSize(4);
			ret.Type = VariableKind::AddressOfVariable;
			ret.Object = addr;
			return ret;
		}
	}

	throw ClrException("Could not resolve field token ", SystemException::FieldAccess, token);
}


void FirmataIlExecutor::Stsfld(int token, Variable& value)
{
	if (_statics.contains(token))
	{
		_statics.at(token) = value;
		return;
	}

	if (value.Type == VariableKind::LargeValueType)
	{
		_largeStatics.insertOrUpdate(token, value);
		return;
	}

	_statics.insert(token, value);
}

/// <summary>
/// Store value "var" in field "token" of instance "obj". Obj may be an object, a reference (pointer to a location) or a value type (rare)
/// </summary>
/// <param name="currentMethod">Method currently executing</param>
/// <param name="obj">Instance to manipulate. May be of type Object; AddressOfVariable or an inline value type (anything else)</param>
/// <param name="token">The field token to use</param>
/// <param name="var">The value to store</param>
/// <returns>
/// The address where the value was stored, or null if a nullreference exception should be thrown
/// </returns>
void* FirmataIlExecutor::Stfld(MethodBody* currentMethod, Variable& obj, int32_t token, Variable& var)
{
	ClassDeclaration* cls;
	byte* o;
	int offset;
	if (obj.Type == VariableKind::AddressOfVariable)
	{
		cls = &ResolveClassFromFieldToken(token);
		offset = 0; // No extra header
		o = (byte*)obj.Object; // Data being pointed to
	}
	else if (obj.Type != VariableKind::Object)
	{
		// Stfld to a value type needs one less indirection, but we need to get the type first.
		// The value type does not carry the type information. Lets derive it from the field token.
		// TODO: This is slow, not what one expects from accessing a value type

		cls = &ResolveClassFromFieldToken(token);
		offset = 0; // No extra header
		o = (byte*)&obj.Int32; // Data is right there
	}
	else
	{
		// The vtable is actually a pointer to the class declaration and at the beginning of the object memory
		o = (byte*)obj.Object;
		// Get the first data element of where the object points to
		cls = ((ClassDeclaration*)(*(int32_t*)o));
		// Assuming sizeof(void*) == sizeof(any pointer type)
		// Our members start here
		offset = sizeof(void*);
	}

	// Early abort. Offsetting to a null ptr does not give a valid field
	if (o == nullptr)
	{
		return nullptr;
	}
	
	vector<Variable*> allfields;
	CollectFields(cls, allfields);
	for (auto handle1 = allfields.begin(); handle1 != allfields.end(); ++handle1)
	{
		Variable* handle = (*handle1);
		// Ignore static member here
		if ((handle->Type & VariableKind::StaticMember) != VariableKind::Void)
		{
			continue;
		}
		
		if (handle->Int32 == token)
		{
			// Found the member
			memcpy(o + offset, &var.Object, handle->fieldSize());
			return (o + offset);
		}

		if ((uint16_t)offset >= SizeOfClass(cls))
		{
			// Something is wrong.
			Firmata.sendString(F("Member offset exceeds class size"));
			break;
		}
		
		offset += handle->fieldSize();
	}

	throw ClrException("Could not resolve field token ", SystemException::FieldAccess, token);
}

void FirmataIlExecutor::ExceptionOccurred(ExecutionState* state, SystemException error, int32_t errorLocationToken)
{
	throw ClrException("", error, errorLocationToken);
	// state->_runtimeException = new RuntimeException(error, Variable(errorLocationToken, VariableKind::Int32));
}

#define BinaryOperation(op) \
switch (value1.Type)\
{\
case VariableKind::Int32:\
	intermediate.Int32 = value1.Int32 op value2.Int32;\
	intermediate.Type = value1.Type;\
	break;\
case VariableKind::Uint32:\
	intermediate.Uint32 = value1.Uint32 op value2.Uint32;\
	intermediate.Type = VariableKind::Uint32;\
	break;\
case VariableKind::Uint64:\
	intermediate.Uint64 = value1.Uint64 op value2.Uint64;\
	intermediate.Type = VariableKind::Uint64;\
	break;\
case VariableKind::Int64:\
	intermediate.Int64 = value1.Int64 op value2.Int64;\
	intermediate.Type = VariableKind::Int64;\
	break;\
case VariableKind::Float:\
	intermediate.Float = value1.Float op value2.Float;\
	intermediate.Type = VariableKind::Float;\
	break;\
case VariableKind::Double:\
	intermediate.Double = value1.Double op value2.Double;\
	intermediate.Type = VariableKind::Double;\
	break;\
default:\
	ExceptionOccurred(currentFrame, SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);\
	return MethodState::Aborted;\
}


#define ComparisonOperation(op) \
intermediate.Type = VariableKind::Boolean;\
switch (value1.Type)\
{\
case VariableKind::Int32:\
	intermediate.Boolean = value1.Int32 op value2.Int32;\
	break;\
case VariableKind::Object:\
case VariableKind::AddressOfVariable:\
case VariableKind::ReferenceArray:\
case VariableKind::ValueArray:\
	intermediate.Boolean = value1.Object op value2.Object; \
case VariableKind::RuntimeTypeHandle:\
case VariableKind::Boolean:\
case VariableKind::Uint32:\
	intermediate.Boolean = value1.Uint32 op value2.Uint32;\
	break;\
case VariableKind::Uint64:\
	intermediate.Boolean = value1.Uint64 op value2.Uint64;\
	break;\
case VariableKind::Int64:\
	intermediate.Boolean = value1.Int64 op value2.Int64;\
	break;\
case VariableKind::Float:\
	intermediate.Boolean = value1.Float op value2.Float;\
	break;\
case VariableKind::Double:\
	intermediate.Boolean = value1.Double op value2.Double;\
	break;\
default:\
	ExceptionOccurred(currentFrame, SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);\
	return MethodState::Aborted;\
}

// Implements binary operations when they're only defined on integral types (i.e AND, OR)
#define BinaryOperationIntOnly(op) \
switch (value1.Type)\
{\
case VariableKind::Int32:\
	intermediate.Int32 = value1.Int32 op value2.Int32;\
	intermediate.Type = value1.Type;\
	break;\
case VariableKind::Boolean:\
case VariableKind::Uint32:\
	intermediate.Uint32 = value1.Uint32 op value2.Uint32;\
	intermediate.Type = VariableKind::Uint32;\
	break;\
case VariableKind::Uint64:\
	intermediate.Uint64 = value1.Uint64 op value2.Uint64;\
	intermediate.Type = VariableKind::Uint64;\
	break;\
case VariableKind::Int64:\
	intermediate.Int64 = value1.Int64 op value2.Int64;\
	intermediate.Type = VariableKind::Int64;\
	break;\
default:\
	ExceptionOccurred(currentFrame, SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);\
	return MethodState::Aborted;\
}

#define MakeUnsigned() \
	if (value1.Type == VariableKind::Int64 || value1.Type == VariableKind::Uint64)\
	{\
		value1.Type = VariableKind::Uint64;\
	}\
	else\
	{\
		value1.Type = VariableKind::Uint32;\
	}

MethodState FirmataIlExecutor::BasicStackInstructions(ExecutionState* currentFrame, u16 PC, VariableDynamicStack* stack, VariableVector* locals, VariableVector* arguments,
	OPCODE instr, Variable& value1, Variable& value2, Variable& value3)
{
	Variable intermediate;
	switch (instr)
	{
	case CEE_THROW:
	{
		// Throw empties the execution stack
		while (!stack->empty())
		{
			stack->pop();
		}
		ClassDeclaration* exceptionType = GetClassDeclaration(value1);
		ExceptionOccurred(currentFrame, SystemException::CustomException, exceptionType->ClassToken);
		return MethodState::Aborted;
	}
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
		locals->at(0) = value1;
		break;
	case CEE_STLOC_1:
		locals->at(1) = value1;
		break;
	case CEE_STLOC_2:
		locals->at(2) = value1;
		break;
	case CEE_STLOC_3:
		locals->at(3) = value1;
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
		intermediate.Object = nullptr;
		intermediate.Type = VariableKind::Object;
		stack->push(intermediate);
		break;
	case CEE_CEQ:
		ComparisonOperation(== );
		stack->push(intermediate);
		break;
		
	case CEE_ADD:
		// For some of the operations, the result doesn't depend on the sign, due to correct overflow
		BinaryOperation(+);
		stack->push(intermediate);
		break;
	case CEE_SUB:
		BinaryOperation(-);
		stack->push(intermediate);
		break;
	case CEE_MUL:
		BinaryOperation(*);
		stack->push(intermediate);
		break;
	case CEE_DIV:
		if (value2.Uint64 == 0)
		{
			throw ClrException(SystemException::DivideByZero, currentFrame->_executingMethod->methodToken);
		}
		
		BinaryOperation(/ );
		stack->push(intermediate);
		break;
	case CEE_REM:
		if (value2.Uint64 == 0)
		{
			throw ClrException(SystemException::DivideByZero, currentFrame->_executingMethod->methodToken);
		}
		switch (value1.Type)
		{
		case VariableKind::Int32:
			intermediate.Int32 = value1.Int32 % value2.Int32;
			intermediate.Type = value1.Type;
				break;
		case VariableKind::Uint32:
			intermediate.Uint32 = value1.Uint32 % value2.Uint32;
			intermediate.Type = VariableKind::Uint32;
		break;
		case VariableKind::Uint64:
			intermediate.Uint64 = value1.Uint64 % value2.Uint64;
			intermediate.Type = VariableKind::Uint64;
			break;
		case VariableKind::Int64:
			intermediate.Int64 = value1.Int64 % value2.Int64;
			intermediate.Type = VariableKind::Int64;
			break;
		case VariableKind::Float:
			intermediate.Float = (float)fmod(value1.Float, value2.Float);
			intermediate.Type = VariableKind::Float;
			break;
		case VariableKind::Double:
			intermediate.Double = fmod(value1.Double, value2.Double);
			intermediate.Type = VariableKind::Double;
			break;
		default:
			ExceptionOccurred(currentFrame, SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);
			return MethodState::Aborted;
		}
		stack->push(intermediate);
		break;
	case CEE_DIV_UN:
		if (value2.Uint64 == 0)
		{
			throw ClrException(SystemException::DivideByZero, currentFrame->_executingMethod->methodToken);
		}
		if (value1.fieldSize() <= 4)
		{
			intermediate = { value1.Uint32 / value2.Uint32, VariableKind::Uint32 };
		}
		else
		{
			intermediate.Uint64 = value1.Uint64 / value2.Uint64;
			intermediate.Type = VariableKind::Uint64;
		}
		stack->push(intermediate);
		break;
	case CEE_REM_UN:
		if (value2.Uint64 == 0)
		{
			throw ClrException(SystemException::DivideByZero, currentFrame->_executingMethod->methodToken);
		}
		// Operation is not directly allowed on floating point variables by the CLR
		if (value1.Type == VariableKind::Int32 || value1.Type == VariableKind::Uint32)
		{
			intermediate = { value1.Uint32 % value2.Uint32, VariableKind::Uint32 };
		}
		else
		{
			intermediate.Uint64 = value1.Uint64 % value2.Uint64;
			intermediate.Type = VariableKind::Uint64;
		}
		
		stack->push(intermediate);
		break;
	case CEE_CGT_UN:
		if (value1.Type == VariableKind::Int64 || value1.Type == VariableKind::Uint64)
		{
			value1.Type = VariableKind::Uint64;
		}
		else
		{
			value1.Type = VariableKind::Uint32;
		}
		// fall trough
	case CEE_CGT:
		{
			ComparisonOperation(> );
			stack->push(intermediate);
			break;
		}
	case CEE_NOT:
		if (value1.fieldSize() == 4)
		{
			intermediate.Uint32 = ~value1.Uint32;
			intermediate.Type = value1.Type;
		}
		else
		{
			intermediate.Uint64 = ~value1.Uint64;
			intermediate.Type = value1.Type;
		}
		stack->push(intermediate);
		break;
	case CEE_NEG:
		if (value1.fieldSize() == 4)
		{
			intermediate.Int32 = -value1.Int32;
			intermediate.Type = value1.Type;
		}
		else
		{
			intermediate.Int64 = -value1.Int64;
			intermediate.Type = value1.Type;
		}
		stack->push(intermediate);
		break;
	case CEE_AND:
		BinaryOperationIntOnly(&);
		stack->push(intermediate);
		break;
	case CEE_OR:
		BinaryOperationIntOnly(| );
		stack->push(intermediate);
		break;
	case CEE_XOR:
		BinaryOperationIntOnly(^);
		stack->push(intermediate);
		break;
	case CEE_CLT:
		ComparisonOperation(< );
		stack->push(intermediate);
		break;
	case CEE_SHL:
		BinaryOperationIntOnly(<< );
		stack->push(intermediate);
		break;

	case CEE_SHR_UN:
		if (value1.Type == VariableKind::Int64 || value1.Type == VariableKind::Uint64)
		{
			value1.Type = VariableKind::Uint64;
		}
		else
		{
			value1.Type = VariableKind::Uint32;
		}
		// fall trough
	case CEE_SHR:
		// The right-hand-side of a shift operation always requires to be of type signed int
		BinaryOperationIntOnly(>> );
		stack->push(intermediate);
		break;
	case CEE_LDC_I4_0:
		intermediate.Int32 = 0;
		intermediate.Type = VariableKind::Int32;
		stack->push(intermediate);
		break;
	case CEE_LDC_I4_1:
		intermediate.Int32 = 1;
		intermediate.Type = VariableKind::Int32;
		stack->push(intermediate);
		break;
	case CEE_LDC_I4_2:
		intermediate.Int32 = 2;
		intermediate.Type = VariableKind::Int32;
		stack->push(intermediate);
		break;
	case CEE_LDC_I4_3:
		intermediate.Int32 = 3;
		intermediate.Type = VariableKind::Int32;
		stack->push(intermediate);
		break;
	case CEE_LDC_I4_4:
		intermediate.Int32 = 4;
		intermediate.Type = VariableKind::Int32;
		stack->push(intermediate);
		break;
	case CEE_LDC_I4_5:
		intermediate.Int32 = 5;
		intermediate.Type = VariableKind::Int32;
		stack->push(intermediate);
		break;
	case CEE_LDC_I4_6:
		intermediate.Int32 = 6;
		intermediate.Type = VariableKind::Int32;
		stack->push(intermediate);
		break;
	case CEE_LDC_I4_7:
		intermediate.Int32 = 7;
		intermediate.Type = VariableKind::Int32;
		stack->push(intermediate);
		break;
	case CEE_LDC_I4_8:
		intermediate.Int32 = 8;
		intermediate.Type = VariableKind::Int32;
		stack->push(intermediate);
		break;
	case CEE_LDC_I4_M1:
		intermediate.Int32 = -1;
		intermediate.Type = VariableKind::Int32;
		stack->push(intermediate);
		break;
	case CEE_DUP:
		stack->push(value1);
		stack->push(value1);
		break;
	case CEE_POP:
		// Nothing to do, already popped
		break;
	case CEE_LDIND_I1:
		{
			int8_t b = *((int8_t*)value1.Object);
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = b;
			stack->push(intermediate);
		}
		break;
	case CEE_LDIND_I2:
		{
			int16_t s = *((int16_t*)value1.Object);
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = s;
			stack->push(intermediate);
		}
		break;
	case CEE_LDIND_I4:
		{
			int32_t i = *((int32_t*)value1.Object);
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = i;
			stack->push(intermediate);
		}
		break;
	case CEE_LDIND_U1:
		{
			// Weird: The definition says that this loads as Int32 as well (and therefore does a sign-extension)
			byte b = *((byte*)value1.Object);
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = b;
			stack->push(intermediate);
		}
		break;
	case CEE_LDIND_U2:
		{
			uint16_t s = *((uint16_t*)value1.Object);
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = s;
			stack->push(intermediate);
		}
		break;
	case CEE_LDIND_U4:
		{
			uint32_t i = *((uint32_t*)value1.Object);
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = i;
			stack->push(intermediate);
		}
		break;
	case CEE_LDIND_REF:
		{
			uint32_t* pTarget = (uint32_t*)value1.Object;
			intermediate.Object = (void*)*pTarget;
			intermediate.Type = VariableKind::Object;
			stack->push(intermediate);
		}
		break;
	case CEE_STIND_I1:
		{
			// Store a byte (i.e. a bool) to the place where value1 points to
			byte* pTarget = (byte*)value1.Object;
			*pTarget = (byte)value2.Uint32;
		}
		break;
	case CEE_STIND_REF:
		{
			uint32_t* pTarget = (uint32_t*)value1.Object;
			if (pTarget == nullptr)
			{
				throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
			}
			*pTarget = value2.Uint32;
		}
		break;
	case CEE_LDLEN:
		{
			// Get the address of the array and push the array size (at index 0)
			if (value1.Object == nullptr)
			{
				throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
			}
			uint32_t* data = (uint32_t*)value1.Object;
			intermediate.Uint32 = *(data + 1);
			intermediate.Type = VariableKind::Uint32;
			stack->push(intermediate);
		}
		break;

	case CEE_LDELEM_U2:
	case CEE_LDELEM_I2:
	{
		if (value1.Object == nullptr)
		{
			throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
		}
		// The instruction suffix (here .i2) indicates the element size
		uint32_t* data = (uint32_t*)value1.Object;
		int32_t size = *(data + 1);
		int32_t index = value2.Int32;
		if (index < 0 || index >= size)
		{
			ExceptionOccurred(currentFrame, SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
			return MethodState::Aborted;
		}

		// This can only be a value type (of type short or ushort)
		u16* sPtr = (u16*)data;
		if (instr == CEE_LDELEM_I1)
		{
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = *(sPtr + 6 + index);
		}
		else
		{
			intermediate.Type = VariableKind::Uint32;
			intermediate.Uint32 = *(sPtr + 6 + index);
		}

		stack->push(intermediate);
	}
	break;
	case CEE_STELEM_I2:
	{
		if (value1.Object == nullptr)
		{
			throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
		}
		// The instruction suffix (here .i2) indicates the element size
		uint32_t* data = (uint32_t*)value1.Object;
		int32_t size = *(data + 1);
		int32_t index = value2.Int32;
		if (index < 0 || index >= size)
		{
			ExceptionOccurred(currentFrame, SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
			return MethodState::Aborted;
		}

		// This can only be a value type (of type short or ushort)
		u16* sPtr = (u16*)data;
		*(sPtr + 6 + index) = (short)value3.Int32;
	}
	break;

	case CEE_LDELEM_U1:
	case CEE_LDELEM_I1:
	{
		if (value1.Object == nullptr)
		{
			throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
		}
		// The instruction suffix (here .i1) indicates the element size
		uint32_t* data = (uint32_t*)value1.Object;
		int32_t size = *(data + 1);
		int32_t index = value2.Int32;
		if (index < 0 || index >= size)
		{
			ExceptionOccurred(currentFrame, SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
			return MethodState::Aborted;
		}

		// This can only be a value type (of type byte or sbyte)
		byte* bytePtr = (byte*)data;
		if (instr == CEE_LDELEM_I1)
		{
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = *(bytePtr + 12 + index);
		}
		else
		{
			intermediate.Type = VariableKind::Uint32;
			intermediate.Uint32 = *(bytePtr + 12 + index);
		}
			
		stack->push(intermediate);
	}
	break;
	case CEE_STELEM_I1:
	{
		if (value1.Object == nullptr)
		{
			throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
		}
		// The instruction suffix (here .i4) indicates the element size
		uint32_t* data = (uint32_t*)value1.Object;
		int32_t size = *(data + 1);
		int32_t index = value2.Int32;
		if (index < 0 || index >= size)
		{
			ExceptionOccurred(currentFrame, SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
			return MethodState::Aborted;
		}

		// This can only be a value type (of type byte or sbyte)
		byte* bytePtr = (byte*)data;
		*(bytePtr + 12 + index) = (byte)value3.Int32;
	}
	break;
	case CEE_LDELEM_REF:
	case CEE_LDELEM_U4:
	case CEE_LDELEM_I4:
		{
			if (value1.Object == nullptr)
			{
				throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
			}
			// The instruction suffix (here .i4) indicates the element size
			uint32_t* data = (uint32_t*)value1.Object;
			int32_t size = *(data + 1);
			int32_t index = value2.Int32;
			if (index < 0 || index >= size)
			{
				ExceptionOccurred(currentFrame, SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
				return MethodState::Aborted;
			}

			// Note: Here, size of Variable is equal size of pointer, but this doesn't hold for the other LDELEM variants
			if (value1.Type == VariableKind::ValueArray)
			{
				if (instr == CEE_LDELEM_I4)
				{
					intermediate.Type = VariableKind::Int32;
					intermediate.Int32 = *(data + 3 + index);
				}
				else
				{
					intermediate.Type = VariableKind::Uint32;
					intermediate.Uint32 = *(data + 3 + index);
				}

				stack->push(intermediate);
			}
			else
			{
				Variable r(*(data + 3 + index), VariableKind::Object);
				stack->push(r);
			}
		}
		break;
	case CEE_STELEM_REF:
	case CEE_STELEM_I4:
	{
		if (value1.Object == nullptr)
		{
			throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
		}
		// The instruction suffix (here .i4) indicates the element size
		uint32_t* data = (uint32_t*)value1.Object;
		int32_t size = *(data + 1);
		int32_t index = value2.Int32;
		if (index < 0 || index >= size)
		{
			ExceptionOccurred(currentFrame, SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
			return MethodState::Aborted;
		}

		if (value1.Type == VariableKind::ValueArray)
		{
			*(data + 3 + index) = value3.Int32;
		}
		else
		{
			if (instr == CEE_STELEM_REF && (value3.Type != VariableKind::Object && value3.Type != VariableKind::ValueArray))
			{
				// STELEM.ref shall throw if the value type doesn't match the array type. We don't test the dynamic type, but
				// at least it should be a reference
				ExceptionOccurred(currentFrame, SystemException::ArrayTypeMismatch, currentFrame->_executingMethod->methodToken);
				return MethodState::Aborted;
			}
			// can only be an object now
			*(data + 3 + index) = (uint32_t)value3.Object;
		}
	}
	break;
		// Luckily, the C++ compiler takes over the actual magic happening in these conversions
	case CEE_CONV_I:
	case CEE_CONV_I4:
	case CEE_CONV_I2:
		intermediate.Type = VariableKind::Int32;
		switch (value1.Type)
		{
		case VariableKind::Int32:
			intermediate.Int32 = value1.Int32;
			break;
		case VariableKind::Uint32:
			intermediate.Int32 = value1.Uint32;
			break;
		case VariableKind::Int64:
			intermediate.Int32 = (int32_t)value1.Int64;
			break;
		case VariableKind::Float:
			intermediate.Int32 = (int32_t)value1.Float;
			break;
		case VariableKind::Double:
			intermediate.Int32 = (int32_t)value1.Double;
			break;
		case VariableKind::AddressOfVariable:
			// If it was an address, keep that designation (this converts from Intptr to Uintptr, which is mostly a no-op)
			intermediate.Int32 = (int32_t)value1.Int32;
			intermediate.Type = VariableKind::AddressOfVariable;
			break;
		default: // The conv statement never throws
			intermediate.Int32 = (int32_t)value1.Uint64;
			break;
		}
		stack->push(intermediate);
		break;
		break;
	case CEE_CONV_U:
	case CEE_CONV_U4:
	case CEE_CONV_U2:
		intermediate.Type = VariableKind::Uint32;
		switch (value1.Type)
		{
		case VariableKind::Int32:
			intermediate.Uint32 = value1.Int32;
			break;
		case VariableKind::Uint32:
			intermediate.Uint32 = value1.Uint32;
			break;
		case VariableKind::Int64:
			intermediate.Uint32 = (uint32_t)value1.Int64;
			break;
		case VariableKind::Float:
			intermediate.Uint32 = (uint32_t)value1.Float;
			break;
		case VariableKind::Double:
			intermediate.Uint32 = (uint32_t)value1.Double;
			break;
		case VariableKind::AddressOfVariable:
			// If it was an address, keep that designation (this converts from Intptr to Uintptr, which is mostly a no-op)
			intermediate.Uint32 = (uint32_t)value1.Uint32;
			intermediate.Type = VariableKind::AddressOfVariable;
			break;
		default: // The conv statement never throws
			intermediate.Uint32 = (uint32_t)value1.Uint64;
			break;
		}
		stack->push(intermediate);
		break;
	case CEE_CONV_I8:
		intermediate.Type = VariableKind::Int64;
		switch(value1.Type)
		{
		case VariableKind::Int32:
			intermediate.Int64 = value1.Int32;
			break;
		case VariableKind::Uint32:
			intermediate.Int64 = value1.Uint32;
			break;
		case VariableKind::Uint64:
			intermediate.Uint64 = value1.Uint64;
			break;
		case VariableKind::Float:
			intermediate.Int64 = (int64_t)value1.Float;
			break;
		case VariableKind::Double:
			intermediate.Int64 = (int64_t)value1.Double;
			break;
		default: // The conv statement never throws
			intermediate.Int64 = value1.Int64;
			break;
		}
		stack->push(intermediate);
		break;
	case CEE_CONV_U8:
		intermediate.Type = VariableKind::Uint64;
		switch (value1.Type)
		{
		case VariableKind::Int32:
			intermediate.Uint64 = value1.Int32;
			break;
		case VariableKind::Uint32:
			intermediate.Uint64 = value1.Uint32;
			break;
		case VariableKind::Int64:
			intermediate.Uint64 = value1.Int64;
			break;
		case VariableKind::Float:
			intermediate.Uint64 = (uint64_t)value1.Float;
			break;
		case VariableKind::Double:
			intermediate.Uint64 = (uint64_t)value1.Double;
			break;
		default: // The conv statement never throws
			intermediate.Uint64 = value1.Uint64;
			break;
		}
		stack->push(intermediate);
		break;
	case CEE_CONV_R8:
		intermediate.Type = VariableKind::Double;
		switch (value1.Type)
		{
		case VariableKind::Int32:
			intermediate.Double = value1.Int32;
			break;
		case VariableKind::Uint32:
			intermediate.Double = value1.Uint32;
			break;
		case VariableKind::Int64:
			intermediate.Double = (double)value1.Int64;
			break;
		case VariableKind::Uint64:
			intermediate.Double = (double)value1.Uint64;
			break;
		case VariableKind::Float:
			intermediate.Double = (double)value1.Float;
			break;
		case VariableKind::Double:
			intermediate.Double = value1.Double;
			break;
		default: // The conv statement never throws
			intermediate.Double = value1.Double;
			break;
		}
		stack->push(intermediate);
		break;
	case CEE_CONV_R4:
		intermediate.Type = VariableKind::Float;
		switch (value1.Type)
		{
		case VariableKind::Int32:
			intermediate.Float = (float)value1.Int32;
			break;
		case VariableKind::Uint32:
			intermediate.Float = (float)value1.Uint32;
			break;
		case VariableKind::Int64:
			intermediate.Float = (float)value1.Int64;
			break;
		case VariableKind::Uint64:
			intermediate.Float = (float)value1.Uint64;
			break;
		case VariableKind::Float:
			intermediate.Float = value1.Float;
			break;
		case VariableKind::Double:
			intermediate.Double = (float)value1.Double;
			break;
		default: // The conv statement never throws
			intermediate.Float = value1.Float;
			break;
		}
		stack->push(intermediate);
		break;
	case CEE_VOLATILE_:
		// Nothing to do really, we're not optimizing anything
		break;
	default:
		InvalidOpCode(PC, instr);
		return MethodState::Aborted;
	}
	
	return MethodState::Running;
}

/// <summary>
/// Allocate an array of instances of the given type. If the type is a value type, the array space is inline, otherwise an array
/// of pointers is reserved.
/// </summary>
/// <param name="tokenOfArrayType">The type of object the array will contain</param>
/// <param name="numberOfElements">The number of elements the array will contain</param>
/// <param name="result">The array object, either a value array or a reference array. Returns type void if there's not enough memory to allocate
/// the array. </param>
/// <returns>
/// The size of the allocated array, in bytes, without the type header (12 bytes)
/// </returns>
int FirmataIlExecutor::AllocateArrayInstance(int tokenOfArrayType, int numberOfElements, Variable& result)
{
	ClassDeclaration& ty = _classes.at(tokenOfArrayType);
	uint32_t* data;
	uint64_t sizeToAllocate;
	if (ty.ValueType)
	{
		
		// Value types are stored directly in the array. Element 0 (of type int32) will contain the array type token (since arrays are also objects), index 1 the array length,
		// Element 1 is the array content type token
		// For value types, ClassDynamicSize may be smaller than a memory slot, because we don't want to store char[] or byte[] with 64 bits per element
		sizeToAllocate = (uint64_t)ty.ClassDynamicSize * numberOfElements;
		if (sizeToAllocate > INT32_MAX - 64 * 1024)
		{
			result = Variable();
			return 0;
		}
		data = (uint32_t*)AllocGcInstance((uint32_t)(sizeToAllocate + 12));
		result.Type = VariableKind::ValueArray;
	}
	else
	{
		// Otherwise we just store pointers
		sizeToAllocate = (uint64_t)sizeof(void*) * numberOfElements;
		if (sizeToAllocate > INT32_MAX - 64 * 1024)
		{
			result = Variable();
			return 0;
		}
		data = (uint32_t*)AllocGcInstance((uint32_t)(sizeToAllocate + 12));
		result.Type = VariableKind::ReferenceArray;
	}

	if (data == nullptr)
	{
		result = Variable();
		return 0;
	}

	ClassDeclaration& arrType = _classes.at((int)KnownTypeTokens::Array);
	
	*data = (uint32_t)&arrType;
	*(data + 1)= numberOfElements;
	*(data + 2) = tokenOfArrayType;
	result.Object = data;
	return (int)sizeToAllocate;
}

// This macro only works in the function below. It ensures the stack variable "tempVariable" has enough room to
// store variable of the given size (+ header)
#define EnsureStackVarSize(size) \
	if (tempVariable == nullptr)\
	{\
		tempVariable = (Variable*)alloca((size)  + sizeof(Variable)); /* this is a bit more than what we need, but doesn't matter (is stack memory only) */ \
		sizeOfTemp = size;\
	} else if (sizeOfTemp < (size_t)(size))\
	{\
		tempVariable = (Variable*)alloca((size)  + sizeof(Variable)); \
		sizeOfTemp = size;\
	}\
	tempVariable->Uint64 = 0;

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
	int constrainedTypeToken = 0; // Only used for the CONSTRAINED. prefix
	u16 PC = 0;
	u32* hlpCodePtr;
	VariableDynamicStack* stack;
	VariableVector* locals;
	VariableVector* arguments;

	// Temporary location for a stack variable of arbitrary size. The memory is allocated using alloca() when needed
	Variable* tempVariable = nullptr;
	size_t sizeOfTemp = 0;
	
	currentFrame->ActivateState(&PC, &stack, &locals, &arguments);

	MethodBody* currentMethod = currentFrame->_executingMethod;

	byte* pCode = currentMethod->methodIl;
	TRACE(u32 startTime = micros());
	try
	{
	// The compiler always inserts a return statement, so we can never run past the end of a method,
	// however we use this counter to interrupt code execution every now and then to go back to the main loop
	// and check for other tasks (i.e. serial input data)
    while (instructionsExecuted < NUM_INSTRUCTIONS_AT_ONCE)
    {
		instructionsExecuted++;
		
		u16   len;
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
			ExecuteSpecialMethod(currentFrame, specialMethod, *arguments, retVal);

    		// We're called into a "special" (built-in) method. 
			// Perform a method return
			ExecutionState* frame = rootState; // start at root
			while (frame->_next != currentFrame)
			{
				frame = frame->_next;
			}
			// Remove the last frame and set the PC for the new current frame
			frame->_next = nullptr;
			
			ExecutionState* exitingFrame = currentFrame; // Need to keep it until we have saved the return value
			currentFrame = frame;
			
			// If the method we just terminated is not of type void, we push the result to the 
			// stack of the calling method
    		if ((currentMethod->methodFlags & (byte)MethodFlags::Ctor) != 0)
    		{
    			// If the method was a ctor, we pick its first argument from the stack
    			// and push it back to the caller. This might be a value type
    			// Note: Rare case here - native ctors are special, but see below
				Variable& newInstance = arguments->at(0); // reference to the terminating method's arglist
				currentFrame->ActivateState(&PC, &stack, &locals, &arguments);
				stack->push(newInstance);
    		}
			else if ((currentMethod->methodFlags & (byte)MethodFlags::Void) == 0)
			{
				currentFrame->ActivateState(&PC, &stack, &locals, &arguments);
				stack->push(retVal);
			}
			else
			{
				currentFrame->ActivateState(&PC, &stack, &locals, &arguments);
			}

			delete exitingFrame;
    		
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
					// Remove current method from execution stack
					ExecutionState* frame = rootState;
					if (frame == currentFrame)
					{
						// We're at the outermost frame, for now only basic types can be returned here
						if (!stack->empty())
						{
							*returnValue = stack->top();
							stack->pop();
						}
						else
						{
							*returnValue = Variable();
						}
						return MethodState::Stopped;
					}

					Variable* var;
					if (!stack->empty())
					{
						var = &stack->top();
						stack->pop();
					}
					else
					{
						var = nullptr;
					}

					// Find the frame which has the current frame as next (should be the second-last on the stack now)
					while (frame->_next != currentFrame)
					{
						frame = frame->_next;
					}
					// Remove the last frame and set the PC for the new current frame

					frame->_next = nullptr;
					
					ExecutionState* exitingFrame = currentFrame; // Need to keep it until we have saved the return value
					currentFrame = frame;

					// If the method we just terminated is not of type void, we push the result to the 
					// stack of the calling method
					if ((currentMethod->methodFlags & (byte)MethodFlags::Void) == 0 && (currentMethod->methodFlags & (byte)MethodFlags::Ctor) == 0)
					{
						currentFrame->ActivateState(&PC, &stack, &locals, &arguments);
						stack->push(*var);
					}
					else
					{
						currentFrame->ActivateState(&PC, &stack, &locals, &arguments);
					}

					delete exitingFrame;

					currentMethod = currentFrame->_executingMethod;
					pCode = currentMethod->methodIl;
					
					break;
				}

				MethodState errorState = MethodState::Running;
				byte numArgumentsToPop = pgm_read_byte(OpcodePops + instr);
				if (numArgumentsToPop == 0)
				{
					Variable unused;
					errorState = BasicStackInstructions(currentFrame, PC, stack, locals, arguments, instr, unused, unused, unused);
				}
				else if (numArgumentsToPop == 1)
				{
					Variable& value1 = stack->top();
					stack->pop();
					// The last two args are unused in this case, so we can provide what we want
					errorState = BasicStackInstructions(currentFrame, PC, stack, locals, arguments, instr, value1, value1, value1);
				}
				else if (numArgumentsToPop == 2)
				{
					Variable& value2 = stack->top();
					stack->pop();
					Variable& value1 = stack->top();
					stack->pop();
					errorState = BasicStackInstructions(currentFrame, PC, stack, locals, arguments, instr, value1, value2, value2);
				}
				else if (numArgumentsToPop == 3)
				{
					Variable& value3 = stack->top();
					stack->pop();
					Variable& value2 = stack->top();
					stack->pop();
					Variable& value1 = stack->top();
					stack->pop();
					errorState = BasicStackInstructions(currentFrame, PC, stack, locals, arguments, instr, value1, value2, value3);
				}

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
						intermediate.Int32 = data;
						intermediate.Type = VariableKind::Int32;
						stack->push(intermediate);
						break;
					case CEE_LDLOC_S:
						stack->push(locals->at(data));
						break;
					case CEE_STLOC_S:
						locals->at(data) = stack->top();
						stack->pop();
						break;
					case CEE_LDLOCA_S:
						// Be sure not to make a copy of the local variable here
						intermediate.Object = &(locals->at(data).Int64);
						intermediate.Type = VariableKind::AddressOfVariable;
						stack->push(intermediate);
						break;
					case CEE_LDARG_S:
						stack->push(arguments->at(data));
						break;
					case CEE_LDARGA_S:
						// Get address of argument x. Do not copy the value.
						intermediate.Object = &(arguments->at(data).Int64);
						intermediate.Type = VariableKind::AddressOfVariable;
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
					intermediate.Int32 = v;
					intermediate.Type = VariableKind::Int32;
					stack->push(intermediate);
				}
				else
				{
					InvalidOpCode(PC, instr);
					return MethodState::Aborted;
				}
				break;
            }

			case InlineI8: // LDC.i8 instruction
			{
				uint64_t* hlpCodePtr8 = (uint64_t*)(pCode + PC);
				uint64_t v = *hlpCodePtr8;
				PC += 8;
				if (instr == CEE_LDC_I8)
				{
					intermediate.Type = VariableKind::Int64;
					intermediate.Int64 = v;
					stack->push(intermediate);
				}
				else
				{
					InvalidOpCode(PC, instr);
					return MethodState::Aborted;
				}
				break;
			}
			case InlineR: // LDC.r8 instruction
			{
				double* hlpCodePtr8 = (double*)(pCode + PC);
				double v = *hlpCodePtr8;
				PC += 8;
				if (instr == CEE_LDC_R8)
				{
					intermediate.Type = VariableKind::Double;
					intermediate.Double = v;
					stack->push(intermediate);
				}
				else
				{
					InvalidOpCode(PC, instr);
					return MethodState::Aborted;
				}
				break;
			}
			case ShortInlineR: // LDC.r4 instruction
			{
				float* hlpCodePtr4 = (float*)(pCode + PC);
				float v = *hlpCodePtr4;
				PC += 4;
				if (instr == CEE_LDC_R8)
				{
					intermediate.Type = VariableKind::Float;
					intermediate.Float = v;
					stack->push(intermediate);
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

				intermediate.Type = VariableKind::Boolean;
				intermediate.Boolean = false;
				switch (instr)
				{
					case CEE_BR:
					case CEE_BR_S:
						intermediate.Boolean = true;
						break;
					case CEE_BEQ:
					case CEE_BEQ_S:
						ComparisonOperation(== );
						break;
					case CEE_BGE_UN:
					case CEE_BGE_UN_S:
						MakeUnsigned();
						// fall trough
					case CEE_BGE:
					case CEE_BGE_S:
						ComparisonOperation(>= );
						break;
					case CEE_BLE_UN:
					case CEE_BLE_UN_S:
						MakeUnsigned();
						// fall trough
					case CEE_BLE:
					case CEE_BLE_S:
						ComparisonOperation(<= );
						break;
					case CEE_BGT_UN:
					case CEE_BGT_UN_S:
						MakeUnsigned();
						// fall trough
					case CEE_BGT:
					case CEE_BGT_S:
						ComparisonOperation(> );
						break;
					case CEE_BLT_UN:
					case CEE_BLT_UN_S:
						MakeUnsigned();
						// fall trough
					case CEE_BLT:
					case CEE_BLT_S:
						ComparisonOperation(< );
						break;
					case CEE_BNE_UN:
					case CEE_BNE_UN_S:
						MakeUnsigned();
						ComparisonOperation(!= );
						break;
					case CEE_BRFALSE:
					case CEE_BRFALSE_S:
						value2.Type = value1.Type;
						value2.Uint64 = 0;
						ComparisonOperation(== );
						break;
					case CEE_BRTRUE:
					case CEE_BRTRUE_S:
						value2.Type = value1.Type;
						value2.Uint64 = 0;
						ComparisonOperation(!= );
						break;
					default:
						InvalidOpCode(PC, instr);
						return MethodState::Aborted;
				}
				// the variable intermediate now contains true if we should take the branch, false otherwise
				if (opCodeType == ShortInlineBrTarget)
				{
					if (intermediate.Boolean)
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
				else if (intermediate.Boolean)
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
				int32_t token = static_cast<int32_t>(((u32)pCode[PC]) + (((u32)pCode[PC + 1]) << 8) + (((u32)pCode[PC + 2]) << 16) + (((u32)pCode[PC + 3]) << 24));
				PC += 4;
		            switch(instr)
		            {
		            	// The ldfld instruction loads a field value of an object to the stack
					case CEE_LDFLD:
						{
						Variable& obj = stack->top();
						stack->pop();
						
						VariableDescription desc;
						byte* dataPtr = Ldfld(currentMethod, obj, token, desc);

						// Combine the variable again with its metadata, so we can put it back to the stack
						EnsureStackVarSize(desc.fieldSize());
						tempVariable->setSize(desc.Size);
						tempVariable->Marker = 0x37;
						tempVariable->Type = desc.Type;
						memcpy(&(tempVariable->Int32), dataPtr, desc.Size);
						stack->push(*tempVariable);
						break;
						}
		            	// Store a value to a field
					case CEE_STFLD:
					{
						Variable& var = stack->top();
						stack->pop();
						Variable& obj = stack->top();
						stack->pop();
						void* ptr = Stfld(currentMethod, obj, token, var);
						if (ptr == nullptr)
						{
							ExceptionOccurred(currentFrame, SystemException::NullReference, currentFrame->_executingMethod->methodToken);
							return MethodState::Aborted;
						}
						break;
					}
		            	// Store a static field value on the stack
					case CEE_STSFLD:
						Stsfld(token, stack->top());
						stack->pop();
						break;
					case CEE_LDSFLD:
						stack->push(Ldsfld(token));
						break;
					case CEE_LDFLDA:
					// This one is tricky, because it can load both instance and static fields
					{
						Variable& obj = stack->top();
						stack->pop();
						ClassDeclaration& ty = ResolveClassFromFieldToken(token);
						Variable desc = GetVariableDescription(&ty, token);
						if (desc.Type == VariableKind::Void)
						{
							throw ClrException(SystemException::FieldAccess, token);
						}
						if ((int)(desc.Type & VariableKind::StaticMember) != 0)
						{
							// Obj can be ignored in this case (but needs popping nevertheless)
							stack->push(Ldsflda(token));
						}
						else
						{
							stack->push(Ldflda(obj, token));
						}
					}
					break;
					case CEE_LDSFLDA:
						stack->push(Ldsflda(token));
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

				MethodBody* newMethod = ResolveToken(currentMethod, tk);
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
					Variable& instance = stack->nth(newMethod->numArgs - 1); // numArgs includes the this pointer

					// The constrained prefix just means that it may be a reference. We'll have to check its type
					// TODO: Add test that checks the different cases
					if (constrainedTypeToken != 0)
					{
						ASSERT(instance.Type == VariableKind::AddressOfVariable);
						// The reference points to an instance of type constrainedTypeToken
						cls = &_classes.at(constrainedTypeToken);
						if (cls->ValueType)
						{
							// This will be the this pointer for a method call on a value type (we'll have to do a real boxing if the
							// callee is virtual (can only be the methods ToString(), GetHashCode() or Equals() - that is, one of the virtual methods of
							// System.Object, System.Enum or System.ValueType)
							if (newMethod->methodFlags & (int)MethodFlags::Virtual)
							{
								size_t sizeOfClass = SizeOfClass(cls);
								void* ret = AllocGcInstance(sizeOfClass);
								if (ret == nullptr)
								{
									ExceptionOccurred(currentFrame, SystemException::OutOfMemory, constrainedTypeToken);
									return MethodState::Aborted;
								}

								// Save a reference to the class declaration in the first entry of the newly created instance.
								// this will serve as vtable.
								ClassDeclaration** vtable = (ClassDeclaration**)ret;
								*vtable = cls;
								Variable* value1 = (Variable*)instance.Object;
								instance.Object = ret;
								instance.Type = VariableKind::Object;
								// Copy the value to the newly allocated boxed instance (just the first member for now)
								SetField4(*cls, *value1, instance, 0); // Since fieldNo is 0, we can use the size-independent version here
							}
							else
							{
								// TODO: Untested case - test!!
								ExceptionOccurred(currentFrame, SystemException::InvalidOperation, constrainedTypeToken);
								return MethodState::Aborted;
							}
						}
						else
						{
							// Dereference the instance
							instance.Object = reinterpret_cast<void*>(*(int*)(instance.Object));
							instance.Type = VariableKind::Object;
						}
					}
					
					if (instance.Type != VariableKind::Object && instance.Type != VariableKind::ValueArray && instance.Type != VariableKind::ReferenceArray)
					{
						Firmata.sendString(F("Virtual function call on something that is not an object"));
						ExceptionOccurred(currentFrame, SystemException::InvalidCast, newMethod->methodToken);
						return MethodState::Aborted;
					}
					if (instance.Object == nullptr)
					{
						if (newMethod->nativeMethod != NativeMethod::None)
						{
							// For native methods, the this pointer may be null, that is ok (we're calling on a dummy interface)
							goto outer;
						}
						Firmata.sendString(F("NullReferenceException calling virtual method"));
						ExceptionOccurred(currentFrame, SystemException::NullReference, newMethod->methodToken);
						return MethodState::Aborted;
					}

					if (cls == nullptr)
					{
						// The vtable is actually a pointer to the class declaration and at the beginning of the object memory
						byte* o = (byte*)instance.Object;
						// Get the first data element of where the object points to
						cls = ((ClassDeclaration*)(*(int*)o));
					}
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
					cls = &ResolveClassFromCtorToken(newMethod->methodToken);

					if (cls->ValueType)
					{
						// If a value type is being created using a newobj instruction, an unboxed value is created and pushed as #0 on the stack
						// we have to be careful to get it back on the ret.
						size_t size = cls->ClassDynamicSize;
						// Reserve enough memory on the stack, so we can temporarily hold the whole variable
						EnsureStackVarSize(size);
						memset(tempVariable, 0, size + sizeof(Variable));
						tempVariable->setSize((uint16_t)size);
						tempVariable->Marker = 0x37;
						tempVariable->Type = size > 8 ? VariableKind::LargeValueType : VariableKind::Int64;
						newObjInstance = tempVariable;
					}
					else
					{
						newObjInstance = CreateInstance(*cls);
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
				
				u16 argumentCount = newMethod->numArgs;
				// While generating locals, assign their types (or a value used as out parameter will never be correctly typed, causing attempts
				// to calculate on void types)
				ExecutionState* newState = new ExecutionState(newMethod->codeReference, newMethod->maxStack, newMethod);
				currentFrame->_next = newState;
				
				VariableDynamicStack* oldStack = stack;
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

					// The first argument of the ctor being invoked is the new object
					if (cls->ValueType)
					{
						// See above, newObjInstance actually points to the new unboxed Variable instance.
						// Push it to the old method's stack, and pass a reference to it as argument.
						// By definition, the "this" pointer of a non-static non-virtual method on a value type is passed by reference
						oldStack->push(*(Variable*)newObjInstance);
						Variable v2(VariableKind::AddressOfVariable);
						v2.Object = &(oldStack->top().Int32);
						arguments->at(0) = v2;
					}
					else
					{
						// This will be pushed to the stack of the calling method on return
						Variable v;
						v.Type = VariableKind::Object;
						v.Object = newObjInstance;
						arguments->at(0) = v;

						// Also push it to the stack of the calling method - this will be the implicit return value of
						// the newobj call.
						oldStack->push(v);
					}
            	}
				else
				{
					while (argumentCount > 0)
					{
						argumentCount--;
						Variable& v = oldStack->top();
						////if (argumentCount == 0 && v.Type == VariableKind::AddressOfVariable)
						////{
						////	// TODO: The "this" pointer of a value type is passed by reference (it is loaded using a ldarga instruction)
						////	// But for us, it nevertheless points to an object variable slot. (Why?) Therefore, unbox the reference.
						////	// There are a few more special cases to consider it seems, especially when the called method is virtual, see §8.6.1.5
						////	Variable v2;
						////	v2.Object = (void*)(*((uint32_t*)v.Object));
						////	v2.Type = VariableKind::Object;
						////	arguments->at(0) = v2;
						////}
						////else
						{
							// This calls operator =, potentially copying more than sizeof(Variable)
							arguments->at(argumentCount) = v;
						}
						oldStack->pop();
					}
				}

				constrainedTypeToken = 0;
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
					if (!_classes.contains(token))
					{
						Firmata.sendStringf(F("Unknown class token in NEWARR instruction: %lx"), 4, token);
						ExceptionOccurred(currentFrame, SystemException::ClassNotFound, token);
						return MethodState::Aborted;
					}
					else
					{
						Variable v1;
						AllocateArrayInstance(token, size, v1);
						if (v1.Type == VariableKind::Void)
						{
							ExceptionOccurred(currentFrame, SystemException::OutOfMemory, token);
							return MethodState::Aborted;
						}
						stack->push(v1);
					}
					
					break;
				case CEE_STELEM:
					{
						Variable& value3 = stack->top();
						stack->pop();
						Variable& value2 = stack->top();
						stack->pop();
						Variable& value1 = stack->top();
						stack->pop();
						if (value1.Object == nullptr)
						{
							throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
						}
						
						uint32_t* data = (uint32_t*)value1.Object;
						int32_t arraysize = *(data + 1);
						int32_t targetType = *(data + 2);
						if (token != targetType)
						{
							ExceptionOccurred(currentFrame, SystemException::ArrayTypeMismatch, currentFrame->_executingMethod->methodToken);
							return MethodState::Aborted;
						}
						ClassDeclaration& elemTy = _classes.at(token);
						int32_t index = value2.Int32;
						if (index < 0 || index >= arraysize)
						{
							ExceptionOccurred(currentFrame, SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
							return MethodState::Aborted;
						}

						switch(elemTy.ClassDynamicSize)
						{
						case 1:
						{
							byte* dataptr = (byte*)data;
							*(dataptr + 12 + index) = (byte)value3.Int32;
							break;
						}
						case 2:
						{
							short* dataptr = (short*)data;
							*(dataptr + 6 + index) = (short)value3.Int32;
							break;
						}
						case 4:
						{
							*(data + 3 + index) = value3.Int32;
							break;
						}
						case 8:
						{
							uint64_t* dataptr = (uint64_t*)data;
							*(AddBytes(dataptr, 12) + index) = value3.Int64;
							break;
						}
						default: // Arbitrary size of the elements in the array
						{
							byte* dataptr = (byte*)data;
							byte* targetPtr = AddBytes(dataptr, 12 + (elemTy.ClassDynamicSize * index));
							memcpy(targetPtr, &value3.Int32, elemTy.ClassDynamicSize);
							break;
						}
						case 0: // That's fishy
							ExceptionOccurred(currentFrame, SystemException::ArrayTypeMismatch, token);
							return MethodState::Aborted;
						}
						
					}
					break;
				case CEE_LDELEM:
				{
					Variable& value2 = stack->top();
					stack->pop();
					Variable& value1 = stack->top();
					stack->pop();
					if (value1.Object == nullptr)
					{
						throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
					}
					// The instruction suffix (here .i4) indicates the element size
					uint32_t* data = (uint32_t*)value1.Object;
					int32_t arraysize = *(data + 1);
					int32_t targetType = *(data + 2);
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
						// This should always exist
						ClassDeclaration& elemTy = _classes.at(token);
						EnsureStackVarSize(elemTy.ClassDynamicSize);
						tempVariable->Marker = VARIABLE_DEFAULT_MARKER;
						switch (elemTy.ClassDynamicSize)
						{
						case 1:
						{
							byte* dataptr = (byte*)data;
							tempVariable->Type = VariableKind::Int32;
							tempVariable->Int32 = *(dataptr + 12 + index);
							tempVariable->setSize(4);
							break;
						}
						case 2:
						{
							short* dataptr = (short*)data;
							tempVariable->Type = VariableKind::Int32;
							tempVariable->Int32 = *(dataptr + 6 + index);
							tempVariable->setSize(4);
							break;
						}
						case 4:
						{
							tempVariable->Type = VariableKind::Int32;
							tempVariable->Int32 = *(data + 3 + index);
							tempVariable->setSize(4);
							break;
						}
						case 8:
						{
							tempVariable->Type = VariableKind::Int64;
							uint64_t* dataptr = (uint64_t*)data;
							tempVariable->Int64 = *(AddBytes(dataptr, 12) + index);
							tempVariable->setSize(8);
							break;
						}
						default:
							byte* dataptr = (byte*)data;
							byte* srcPtr = AddBytes(dataptr, 12 + (elemTy.ClassDynamicSize * index));
							tempVariable->setSize(elemTy.ClassDynamicSize);
							tempVariable->Type = elemTy.ClassDynamicSize <= 8 ? VariableKind::Int64 : VariableKind::LargeValueType;
							memcpy(&tempVariable->Int32, srcPtr, elemTy.ClassDynamicSize);
							break;
						}
						stack->push(*tempVariable);
						
					}
					else
					{
						// can only be an object now
						Variable v1;
						v1.Marker = VARIABLE_DEFAULT_MARKER;
						v1.Object = (void*)*(data + 3 + index);
						v1.Type = VariableKind::Object;
						v1.setSize(4);
						stack->push(v1);
					}
					break;
				}
				case CEE_LDELEMA:
				{
					Variable& value2 = stack->top();
					stack->pop();
					Variable& value1 = stack->top();
					stack->pop();
					if (value1.Object == nullptr)
					{
						throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
					}

					uint32_t* data = (uint32_t*)value1.Object;
					int32_t arraysize = *(data + 1);
					int32_t targetType = *(data + 2);
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
						ClassDeclaration& elemTy = _classes.at(token);
						switch (elemTy.ClassDynamicSize)
						{
						case 1:
						{
							byte* dataptr = (byte*)data;
							v1.Object = (dataptr + 12 + index);
							break;
						}
						case 2:
						{
							short* dataptr = (short*)data;
							v1.Object = (dataptr + 6 + index);
							break;
						}
						case 4:
						{
							v1.Object = (data + 3 + index);
							break;
						}
						case 8:
						{
							uint64_t* dataptr = (uint64_t*)data;
							v1.Object = (AddBytes(dataptr, 12) + index);
							break;
						}
						default:
							byte* dataptr = (byte*)data;
							v1.Object = AddBytes(dataptr, 12 + (elemTy.ClassDynamicSize * index));
							break;
						}
						
						v1.Type = VariableKind::AddressOfVariable;
					}
					else
					{
						// can only be an object now
						v1.Object = (data + 3 + index);
						v1.Type = VariableKind::Object;
					}
					stack->push(v1);
					break;
				}
				case CEE_BOX:
				{
					Variable& value1 = stack->top();
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
							memcpy(AddBytes(ret,sizeof(void*)), &value1.Int32, value1.fieldSize());
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
					Variable& value1 = stack->top();
					stack->pop();
					if (_classes.contains(token))
					{
						ClassDeclaration& ty = _classes.at(token);
						if (ty.ValueType)
						{
							// TODO: This requires special handling for types derived from Nullable<T>

							uint32_t offset = sizeof(void*);
							// Get the beginning of the data part of the object
							byte* o = (byte*)value1.Object;
							size = ty.ClassDynamicSize;
							// Reserve enough memory on the stack, so we can temporarily hold the whole variable
							EnsureStackVarSize(size);
							tempVariable->setSize((uint16_t)size);
							tempVariable->Marker = 0x37;
							tempVariable->Type = size > 8 ? VariableKind::LargeValueType : VariableKind::Uint64;
							memcpy(&(tempVariable->Int32), o + offset, size);
							stack->push(*tempVariable);
						}
						else
						{
							// If ty is a reference type, unbox.any does nothing fancy, except a type test
							MethodState result = IsAssignableFrom(ty, value1);
							if (result != MethodState::Running)
							{
								return result;
							}
							Variable r;
							r.Object = value1.Object;
							r.Type = value1.Type;
							stack->push(r);
						}
						
					}
					else
					{
						Firmata.sendStringf(F("Unknown type token in BOX instruction: %lx"), 4, token);
						ExceptionOccurred(currentFrame, SystemException::ClassNotFound, token);
						return MethodState::Aborted;
					}
					break;
				}
				case CEE_INITOBJ:
				{
					Variable value1 = stack->top();
					stack->pop();
						// According to docs, this shouldn't happen, but better be safe
					if (value1.Object == nullptr)
					{
						throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
					}
					ClassDeclaration& ty = _classes.at(token);
					if (ty.ValueType)
					{
						size = ty.ClassDynamicSize;
						memset(value1.Object, 0, size);
					}
					else
					{
						// per definition, INITOBJ with a reference type token assumes the target is a pointer, so null it.
						memset(value1.Object, 0, sizeof(void*));
					}
				}
				break;
				case CEE_ISINST:
				{
					Variable value1 = stack->top();
					stack->pop();
					if (value1.Object == nullptr)
					{
						// ISINST on a null pointer just returns the same (which is considered false)
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
					// The cast fails, return null
					intermediate.Object = nullptr;
					intermediate.Type = VariableKind::Object;
					stack->push(intermediate);
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
					// Our metadata list does not contain array[] types, therefore we assume this matches for now
					if (value1.Type == VariableKind::ReferenceArray || value1.Type == VariableKind::ReferenceArray)
					{
						// TODO: Verification possible when looking inside?
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
				case CEE_CONSTRAINED_:
					constrainedTypeToken = token; // This is always immediately followed by a callvirt
					break;

				case CEE_LDOBJ:
				{
					Variable value1 = stack->top();
					stack->pop();
					if (value1.Object == nullptr)
					{
						throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
					}
					if (value1.Type != VariableKind::AddressOfVariable)
					{
						ExceptionOccurred(currentFrame, SystemException::InvalidOperation, token);
						return MethodState::Aborted;
					}
					
					ClassDeclaration& ty = _classes.at(token);
					size = ty.ClassDynamicSize;
					EnsureStackVarSize(size);
					if (ty.ValueType)
					{
						tempVariable->Type = (size > 8 ? VariableKind::LargeValueType : VariableKind::Int64);
					}
					else
					{
						tempVariable->Type = VariableKind::Object;
					}
					tempVariable->setSize((u16)size);
					tempVariable->Marker = 0x37;
					memcpy(&tempVariable->Int32, value1.Object, size);
					stack->push(*tempVariable);
				}
				break;
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
							// constants above 0x10000 are string tokens, but they're not used with LDTOKEN, but with LDSTR
							if (token < 0x10000 && _constants.contains(token))
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
			case InlineString:
				{
					// opcode must be CEE_LDSTR
					int token = static_cast<int32_t>(((u32)pCode[PC]) + (((u32)pCode[PC + 1]) << 8) + (((u32)pCode[PC + 2]) << 16) + (((u32)pCode[PC + 3]) << 24));
					PC += 4;
					bool emptyString = (token & 0xFFFF) == 0;
					if (_constants.contains(token) || emptyString)
					{
						byte* data = nullptr;
						uint32_t length = (uint32_t)(token & 0xFFFF);
						if (!emptyString)
						{
							data = _constants.at(token);
						}
						
						byte* classInstance = (byte*)CreateInstanceOfClass((int)KnownTypeTokens::String, length * 2); // *2, because length is in chars here
						// The string data is stored inline in the class data junk
						memcpy(classInstance + 8, data, length * 2);
						ClassDeclaration& string = _classes.at((int)KnownTypeTokens::String);
						
						// Length
						Variable v(length, VariableKind::Int32);
						intermediate.Type = VariableKind::Object;
						intermediate.Object = classInstance;
						SetField4(string, v, intermediate, 0);
						stack->push(intermediate);
					}
					else
					{
						ExceptionOccurred(currentFrame, SystemException::NotSupported, token);
						return MethodState::Aborted;
					}
				}
				break;
			default:
				InvalidOpCode(PC, instr);
				return MethodState::Aborted;
        }
	}

	}
	catch(OutOfMemoryException& ox)
	{
		currentFrame->UpdatePc(PC);
		Firmata.sendString(STRING_DATA, ox.Message());
		currentFrame->_runtimeException = nullptr; // Can't allocate memory right now
		return MethodState::Aborted;
	}
	catch(ClrException& ex)
	{
		currentFrame->UpdatePc(PC);
		Firmata.sendString(STRING_DATA, ex.Message());
		// TODO: Replace with correct exception handling/stack unwinding later
		currentFrame->_runtimeException = new RuntimeException(ex.ExceptionType(), Variable((int32_t)ex.ExceptionToken(), VariableKind::Int32));
		return MethodState::Aborted;
	}
	catch(ExecutionEngineException& ee)
	{
		currentFrame->UpdatePc(PC);
		Firmata.sendString(STRING_DATA, ee.Message());
		return MethodState::Aborted;
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

	// If the assignment target implements the source interface, that's fine
	if (typeToAssignTo.interfaceTokens.contains(sourceType->ClassToken))
	{
		return MethodState::Running;
	}

	// if the assignment target is an interface implemented by source, that's fine
	if (sourceType->interfaceTokens.contains(typeToAssignTo.ClassToken))
	{
		return MethodState::Running;
	}

	return MethodState::Stopped;
}

/// <summary>
/// Creates a class directly by its type (used i.e. to create instances of System::Type)
/// </summary>
void* FirmataIlExecutor::CreateInstanceOfClass(int32_t typeToken, u32 length /* for string */)
{
	ClassDeclaration& cls = _classes.at(typeToken);
	TRACE(Firmata.sendString(F("Class to create is 0x"), cls.ClassToken));
	// Compute sizeof(class)
	size_t sizeOfClass = SizeOfClass(&cls);
	if (cls.ClassToken == (int)KnownTypeTokens::String)
	{
		sizeOfClass = sizeof(void*) + 4 + length + 2;
	}

	TRACE(Firmata.sendString(F("Class size is 0x"), sizeOfClass));
	void* ret = AllocGcInstance(sizeOfClass);
	if (ret == nullptr)
	{
		OutOfMemoryException::Throw();
	}

	// Save a reference to the class declaration in the first entry of the newly created instance.
	// this will serve as vtable.
	ClassDeclaration** vtable = (ClassDeclaration**)ret;
	*vtable = &cls;
	return ret;
}

ClassDeclaration& FirmataIlExecutor::ResolveClassFromCtorToken(int32_t ctorToken)
{
	TRACE(Firmata.sendString(F("Creating instance via .ctor 0x"), ctorToken));
	for (auto iterator = _classes.begin(); iterator != _classes.end(); ++iterator)
	{
		ClassDeclaration& cls = iterator.second();
		// TRACE(Firmata.sendString(F("Class "), cls.ClassToken));
		for (size_t j = 0; j < cls.methodTypes.size(); j++)
		{
			Method& member = cls.methodTypes.at(j);
			// TRACE(Firmata.sendString(F("Member "), member.Uint32));
			if (member.token == ctorToken)
			{
				return cls;
			}
		}
	}

	throw ClrException(SystemException::MissingMethod, ctorToken);
}

ClassDeclaration& FirmataIlExecutor::ResolveClassFromFieldToken(int32_t fieldToken)
{
	TRACE(Firmata.sendString(F("Creating instance via .ctor 0x"), ctorToken));
	for (auto iterator = _classes.begin(); iterator != _classes.end(); ++iterator)
	{
		ClassDeclaration& cls = iterator.second();
		for (size_t j = 0; j < cls.fieldTypes.size(); j++)
		{
			Variable& member = cls.fieldTypes.at(j);
			if (member.Int32 == fieldToken)
			{
				return cls;
			}
		}
	}

	throw ClrException(SystemException::FieldAccess, fieldToken);
}

/// <summary>
/// Creates an instance of the given type.
/// TODO: System.String needs special handling here, since its instances have a dynamic length (the string is coded inline)
/// </summary>
void* FirmataIlExecutor::CreateInstance(ClassDeclaration& cls)
{
	TRACE(Firmata.sendString(F("Class to create is 0x"), cls.ClassToken));
	// The constructor that was called belongs to this class
	// Compute sizeof(class)
	size_t sizeOfClass = SizeOfClass(&cls);

	TRACE(Firmata.sendString(F("Class size is 0x"), sizeOfClass));
	void* ret = AllocGcInstance(sizeOfClass);
	if (ret == nullptr)
	{
		OutOfMemoryException::Throw();
	}

	// Save a reference to the class declaration in the first entry of the newly created instance.
	// this will serve as vtable.
	ClassDeclaration** vtable = (ClassDeclaration**)ret;
	*vtable = &cls;
	return ret;
}

/// <summary>
/// Returns the size of the memory that needs to be allocated for a dynamic class instance.
/// For value types, this returns the boxed size (which is at least the size of a variable slot + vtable)
/// </summary>
uint16_t FirmataIlExecutor::SizeOfClass(ClassDeclaration* cls)
{
	// + (platform specific) vtable* size
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
	v.Marker = VARIABLE_DECLARATION_MARKER;
	v.Type = (VariableKind)argv[i];
	v.Int32 = DecodePackedUint32(argv + i + 1); // this is the token. It uses 5 bytes
	i += 6;
	if (v.Type != VariableKind::Method)
	{
		v.setSize(DecodePackedUint14(argv + i));
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

MethodBody* FirmataIlExecutor::ResolveToken(MethodBody* code, int32_t token)
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


void FirmataIlExecutor::AttachToMethodList(MethodBody* newCode)
{
	if (_firstMethod == nullptr)
	{
		_firstMethod = newCode;
		return;
	}

	MethodBody* parent = _firstMethod;
	while (parent->next != nullptr)
	{
		parent = parent->next;
	}

	parent->next = newCode;
}

MethodBody* FirmataIlExecutor::GetMethodByCodeReference(u16 codeReference)
{
	MethodBody* current = _firstMethod;
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

MethodBody* FirmataIlExecutor::GetMethodByToken(MethodBody* code, int32_t token)
{
	// Methods in the method list have their top nibble patched with the module ID.
	// if the token to be searched has module 0, we need to add the current module Id (from the
	// token of the currently executing method)

	MethodBody* current = _firstMethod;
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

OPCODE DecodeOpcode(const BYTE *pCode, u16 *pdwLen)
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

	_statics.clear();

	_largeStatics.clear();

	Firmata.sendString(F("Execution memory cleared. Free bytes: 0x"), freeMemory());
}
