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

typedef byte BYTE;
typedef uint32_t DWORD;

#define TRACE(x) x
// #define TRACE(x)

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
		case ExecutorCommand::SetMethodTokens:
			if (argc < 6)
			{
				Firmata.sendString(F("Not enough IL data parameters"));
				SendAckOrNack(subCommand, ExecutionError::InvalidArguments);
				return true;
			}
			SendAckOrNack(subCommand, LoadMetadataTokenMapping(DecodePackedUint14(argv + 2), DecodePackedUint14(argv + 4), 
				DecodePackedUint14(argv + 6), argc - 8, argv + 8));
			break;
		case ExecutorCommand::ClassDeclaration:
			if (argc < 19)
			{
				Firmata.sendString(F("Not enough IL data parameters"));
				SendAckOrNack(subCommand, ExecutionError::InvalidArguments);
			}

			SendAckOrNack(subCommand, LoadClassSignature(DecodePackedUint32(argv + 2),
				DecodePackedUint32(argv + 2 + 5), DecodePackedUint14(argv + 2 + 5 + 5),
				DecodePackedUint14(argv + 2 + 5 + 5 + 2), DecodePackedUint14(argv + 2 + 5 + 5 + 2 + 2), argc - 18, argv + 18));
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

void FirmataIlExecutor::KillCurrentTask()
{
	if (_methodCurrentlyExecuting == nullptr)
	{
		return;
	}

	byte topLevelMethod = _methodCurrentlyExecuting->MethodIndex();

	ExecutionState** currentFrameVar = &_methodCurrentlyExecuting;
	ExecutionState* currentFrame = _methodCurrentlyExecuting;
	while (currentFrame != nullptr)
	{
		// destruct the stack top to bottom (to ensure we regain the complete memory chain)
		while (currentFrame->_next != nullptr)
		{
			currentFrameVar = &currentFrame->_next;
			currentFrame = currentFrame->_next;
		}

		delete currentFrame;
		*currentFrameVar = nullptr; // sets the parent's _next pointer to null
		
		currentFrame = _methodCurrentlyExecuting;
		currentFrameVar = &_methodCurrentlyExecuting;
	}

	// Send a status report, to end any process waiting for this method to return.
	SendExecutionResult(topLevelMethod, Variable(), MethodState::Killed);
	Firmata.sendString(F("Code execution aborted"));
}

void FirmataIlExecutor::runStep()
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
	
	SendExecutionResult(_methodCurrentlyExecuting->MethodIndex(), retVal, execResult);

	// The method ended
	delete _methodCurrentlyExecuting;
	_methodCurrentlyExecuting = nullptr;
}

ExecutionError FirmataIlExecutor::LoadIlDeclaration(u16 codeReference, int flags, byte maxLocals, byte argCount,
	NativeMethod nativeMethod, int token)
{
	Firmata.sendStringf(F("Loading declaration for codeReference %d, Flags 0x%x"), 6, (int)codeReference, (int)flags);
	IlCode* method = GetMethodByCodeReference(codeReference);
	if (method != nullptr)
	{
		method->Clear();
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
	Firmata.sendStringf(F("Loading Declaration."), 0);
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

ExecutionError FirmataIlExecutor::LoadMetadataTokenMapping(u16 codeReference, u16 totalTokens, u16 offset, byte argc, byte* argv)
{
	Firmata.sendStringf(F("Loading %d tokens from offset %d."), 4, (int)totalTokens, (int)offset);
	IlCode* method = GetMethodByCodeReference(codeReference);
	if (method == nullptr)
	{
		// This operation is illegal if the method is unknown
		Firmata.sendString(F("LoadMetadataTokenMapping for unknown codeReference"));
		return ExecutionError::InvalidArguments;
	}

	u32* tokens = nullptr;
	if (offset == 0)
	{
		if (method->tokenMap != nullptr)
		{
			free(method->tokenMap);
		}

		tokens = (u32*)malloc(totalTokens * 4);
		
		memset(tokens, 0, totalTokens * 4);
		method->tokenMapEntries = totalTokens;
		method->tokenMap = tokens;
	}
	else
	{
		if (method->tokenMap == nullptr || method->tokenMapEntries != totalTokens)
		{
			return ExecutionError::InvalidArguments;
		}
		
		tokens = method->tokenMap;
	}
	
	// No need to care about signed/unsigned here, because the top bit of metadata tokens is never used
	byte numTokens = argc / 16;
	for (int i = 0; i < numTokens * 2; i++)
	{
		tokens[i + offset] = DecodeUint32(argv + (8 * i));
	}

	Firmata.sendStringf(F("%d metadata tokens loaded for method %d"), 4, (int)numTokens, (int)codeReference);
	
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

	Firmata.sendStringf(F("Loaded IL Data for method %d, offset %x"), 4, codeReference, offset);
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

void FirmataIlExecutor::SendExecutionResult(u16 codeReference, Variable returnValue, MethodState execResult)
{
	byte replyData[4];
	// Reply format:
	// byte 0: 1 on success, 0 on (technical) failure, such as unsupported opcode
	// byte 1: Number of integer values returned
	// bytes 2+: Integer return values

	// Todo: Fix
	u32 result = returnValue.Uint32;
	
	replyData[0] = result & 0xFF;
	replyData[1] = (result >> 8) & 0xFF;
	replyData[2] = (result >> 16) & 0xFF;
	replyData[3] = (result >> 24) & 0xFF;

	Firmata.startSysex();
	Firmata.write(SCHEDULER_DATA);
	Firmata.write(codeReference & 0x7F);
	Firmata.write((codeReference >> 7) & 0x7F);
	
	// 0: Code execution completed, called method ended
	// 1: Code execution aborted due to exception (i.e. unsupported opcode, method not found)
	// 2: Intermediate data from method (not used here)
	Firmata.write((byte)execResult);
	Firmata.write(1); // Number of arguments that follow
	for (int i = 0; i < 4; i++)
	{
		Firmata.sendValueAsTwo7bitBytes(replyData[i]);
	}
	Firmata.endSysex();
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

	// The method ended very quickly
	_methodCurrentlyExecuting = nullptr;
	delete rootState;
	
	SendExecutionResult(codeReference, result, execResult);
}

void FirmataIlExecutor::InvalidOpCode(u16 pc, u16 opCode)
{
	Firmata.sendStringf(F("Invalid/Unsupported opcode 0x%x at method offset 0x%x"), 4, opCode, pc);
}

// Executes the given OS function. Note that args[0] is the this pointer
Variable FirmataIlExecutor::ExecuteSpecialMethod(NativeMethod method, const vector<Variable>& args)
{
	u32 mil = 0;
	switch(method)
	{
	case NativeMethod::SetPinMode: // PinMode(int pin, PinMode mode)
		{
			int mode = INPUT;
			if (args[2].Int32 == 1) // Must match PullMode enum on C# side
			{
				mode = OUTPUT;
			}
			if (args[2].Int32 == 3)
			{
				mode = INPUT_PULLUP;
			}
			pinMode(args[1].Int32, mode);
			// Firmata.sendStringf(F("Setting pin %ld to mode %ld"), 8, args->Get(1), mode);

			break;
		}
	case NativeMethod::WritePin: // Write(int pin, int value)
			// Firmata.sendStringf(F("Write pin %ld value %ld"), 8, args->Get(1), args->Get(2));
			digitalWrite(args[1].Int32, args[2].Int32 != 0);
			break;
	case NativeMethod::ReadPin:
			return { (int32_t)digitalRead(args[1].Int32), VariableKind::Int32 };
	case NativeMethod::GetTickCount: // TickCount
			mil = millis();
			// Firmata.sendString(F("TickCount "), mil);
			return { (int32_t)mil, VariableKind::Int32 };
	case NativeMethod::SleepMicroseconds:
			delayMicroseconds(args[1].Int32);
			return {};
	case NativeMethod::GetMicroseconds:
			return { (int32_t)micros(), VariableKind::Int32 };
	case NativeMethod::Debug:
			Firmata.sendString(F("Debug "), args[1].Uint32);
			return {};
		default:
			Firmata.sendString(F("Unknown internal method: "), (int)method);
			break;
	}
	return {};
}


Variable Ldfld(Variable& obj, int token)
{
	byte* o = (byte*)obj.Object;
	ClassDeclaration* vtable = (ClassDeclaration*)(*(int*)o);

	// Assuming sizeof(void*) == sizeof(any pointer type)
	// and sizeof(void*) >= sizeof(int)
	// Our members start here
	int offset = sizeof(void*);
	// Todo: Check base classes
	for (auto handle = vtable->memberTypes.begin(); handle != vtable->memberTypes.end(); ++handle)
	{
		if (handle->Int32 == token)
		{
			// Found the member
			Variable v;
			memcpy(&v.Object, (o + offset), sizeof(void*));
			v.Type = handle->Type;
			return v;
		}

		offset += Variable::datasize();
		if (offset >= vtable->ClassSize)
		{
			// Something is wrong.
			Firmata.sendString(F("Member offset exceeds class size"));
		}
	}

	Firmata.sendStringf(F("Class %lx has no member %lx"), 8, vtable->ClassToken, token);
	return Variable();
}


void Stfld(Variable& obj, int token, Variable& var)
{
	// The vtable is actually a pointer to the class declaration and at the beginning of the object memory
	byte* o = (byte*)obj.Object;
	// Get the first data element of where the object points to
	ClassDeclaration* cls = ((ClassDeclaration*)(*(int*)o));

	// Assuming sizeof(void*) == sizeof(any pointer type)
	// Our members start here
	int offset = sizeof(void*);
	// Todo: Check base classes
	for (auto handle = cls->memberTypes.begin(); handle != cls->memberTypes.end(); ++handle)
	{
		if (handle->Int32 == token)
		{
			// Found the member
			memcpy(o + offset, &var.Object, sizeof(void*));
			return;
		}

		offset += Variable::datasize();
		if (offset >= cls->ClassSize)
		{
			// Something is wrong.
			Firmata.sendString(F("Member offset exceeds class size"));
		}
	}

	Firmata.sendStringf(F("Class %lx has no member %lx"), 8, cls->ClassToken, token);
}

MethodState FirmataIlExecutor::BasicStackInstructions(u16 PC, stack<Variable>* stack, vector<Variable>* locals, vector<Variable>* arguments, 
	OPCODE instr, Variable value1, Variable value2)
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
		stack->push(Variable(0, VariableKind::Object));
		break;
	case CEE_CEQ:
		stack->push({ (uint32_t)(value1.Uint32 == value2.Uint32), VariableKind::Boolean });
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
			return MethodState::Aborted;
		}
		intermediate = { value1.Uint32 / value2.Uint32, VariableKind::Uint32 };
		stack->push(intermediate);
		break;
	case CEE_REM_UN:
		if (value2.Uint32 == 0)
		{
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
		
		TRACE(Firmata.sendStringf(F("PC: 0x%x in Method %d (token %lx)"), 8, PC, currentMethod->codeReference, currentMethod->methodToken));
    	/*if (!stack->empty())
    	{
			Firmata.sendStringf(F("Top of stack %lx"), 4, stack->peek());
    	}*/
    	
    	if (PC == 0 && (currentMethod->methodFlags & (byte)MethodFlags::Special))
		{
			NativeMethod specialMethod = currentMethod->nativeMethod;

			TRACE(Firmata.sendString(F("Executing special method "), (int)specialMethod));
			Variable retVal = ExecuteSpecialMethod(specialMethod, *arguments);
			
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

				MethodState errorState = BasicStackInstructions(PC, stack, locals, arguments, instr, value1, value2);
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
					case CEE_LDLOCA_S:
						// Warn: Pointer to data conversion!
						intermediate.Object = &locals->at(data);
						intermediate.Type = VariableKind::Object;
						stack->push(intermediate);
						break;
					case CEE_LDARG_S:
						stack->push(arguments->at(data));
						break;
					case CEE_LDARGA_S:
						// Get address of argument x. 
						// TODO: Byref parameter handling is not supported at the moment by the call implementation. 
						intermediate.Object = &arguments->at(data);
						intermediate.Type = VariableKind::Object;
						stack->push(intermediate);
						break;
					case CEE_STARG_S:
						arguments->at(data) = stack->top();
						stack->pop();
						break;
					default:
						InvalidOpCode(PC, instr);
						break;
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
						stack->push(Ldfld(obj, token));
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
						Stfld(obj, token, var);
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

                IlCode* newMethod = ResolveToken(currentMethod->codeReference, tk);
            	if (newMethod == nullptr)
				{
					Firmata.sendString(F("Unknown token 0x"), tk);
					return MethodState::Aborted;
				}

				if (instr == CEE_NEWOBJ)
				{
					newObjInstance = CreateInstance(newMethod->methodToken);
					if (newObjInstance == nullptr)
					{
						return MethodState::Aborted;
					}
				}
            	
				u32 method = (u32)newMethod;
				method &= ~0xFF000000;
            	            	
				// Patch the code to use the method pointer, that's faster for next time we see this piece of code.
            	// But remove the top byte, this is the memory bank address, which is not 0 for some of the ARM boards
				*hlpCodePtr = method;

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
						arguments->at(argumentCount) = oldStack->top();
						oldStack->pop();
					}
				}
            		
				TRACE(Firmata.sendStringf(F("Pushed stack to method %d"), 2, currentMethod->codeReference));
				break;
            }
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

void* FirmataIlExecutor::CreateInstance(u32 ctorToken)
{
	TRACE(Firmata.sendString(F("Creating instance via .ctor 0x"), ctorToken));
	for (auto iterator = _classes.begin(); iterator != _classes.end(); ++iterator)
	{
		ClassDeclaration& cls = iterator.second();
		// TRACE(Firmata.sendString(F("Class "), cls.ClassToken));
		for(size_t j = 0; j < cls.memberTypes.size(); j++)
		{
			Variable& member = cls.memberTypes.at(j);
			// TRACE(Firmata.sendString(F("Member "), member.Uint32));
			if (member.Uint32 == ctorToken)
			{
				TRACE(Firmata.sendString(F("Class to create is 0x"), cls.ClassToken));
				// The constructor that was called belongs to this class
				// Compute sizeof(class)
				size_t sizeOfClass = SizeOfClass(cls);

				TRACE(Firmata.sendString(F("Class size is 0x"), sizeOfClass));
				// Todo: Alloc and never free is not really a GC thing.
				void* ret = malloc(sizeOfClass);
				if (ret == nullptr)
				{
					Firmata.sendString(F("Not enough memory allocating an instance of 0x"), cls.ClassToken);
					return nullptr;
				}
				memset(ret, 0, sizeOfClass); // the standard explicitly requires the memory to be initialized
				// Save a reference to the class declaration in the first entry of the newly created instance.
				// this will serve as vtable.
				ClassDeclaration** vtable = (ClassDeclaration**)ret;
				*vtable = &cls;
				return ret;
			}
		}
	}

	Firmata.sendString(F("No class found with that .ctor"));
	return nullptr;
}

int16_t FirmataIlExecutor::SizeOfClass(ClassDeclaration &cls)
{
	// + (platform specific) vtable* size
	return cls.ClassSize + sizeof(void*);
}

ExecutionError FirmataIlExecutor::LoadClassSignature(u32 classToken, u32 parent, u16 size, u16 numberOfMembers, u16 offset, byte argc, byte* argv)
{
	bool alreadyExists = _classes.contains(classToken);
	ClassDeclaration* decl;
	if (alreadyExists)
	{
		decl = &_classes.at(classToken);
	}
	else
	{
		_classes.insert(classToken, ClassDeclaration(classToken, parent, size));
		decl = &_classes.at(classToken);
	}

	// Reinit
	if (offset == 0)
	{
		decl->memberTypes.clear();
	}

	// Firmata.sendStringf(F("Class %lx has parent %lx and size %d. (%d of %d members)"), 14, classToken, parent, size, offset, numberOfMembers);
	
	for (int i = 0; i + 5 < argc;)
	{
		Variable v;
		v.Type = (VariableKind)argv[i];
		v.Uint32 = DecodePackedUint32(argv + i + 1); // uses 5 bytes
		i += 6;
		Firmata.sendStringf(F("Received member %lx of class %lx"), 8, v.Uint32, classToken);
		decl->memberTypes.push_back(v);
	}

	// This is test code - perform an integrity test on the data structure
	
	ClassDeclaration& cls = _classes.at(classToken);
	if (cls.ClassToken != classToken)
	{
		Firmata.sendStringf(F("Current class is not in map: &lx"), 4, cls.ClassToken);
	}

	if (cls.memberTypes.size() != offset + 1)
	{
		Firmata.sendStringf(F("Expected member count doesn't fit: %d"), 2, cls.memberTypes.size());
	}
	for (int j = 0; j < cls.memberTypes.size(); j++)
	{
		Variable& v = cls.memberTypes.at(j);
		if ((v.Uint32 & 0xFF000000) == 0)
		{
			Firmata.sendStringf(F("Invalid member entry at %d"), 2, j);
		}
	}
	
	
	return ExecutionError::None;
}

IlCode* FirmataIlExecutor::ResolveToken(u16 codeReference, uint32_t token)
{
	IlCode* method;
	if ((token >> 24) == 0x0)
	{
		// We've previously patched the code directly with the lower 3 bytes of the method pointer
		// Now we extend that again with the RAM base address (0x20000000 on a Due, 0x0 on an Uno)
		token = token | ((u32)_firstMethod & 0xFF000000);
		return (IlCode*)token;
	}
	if ((token >> 24) == 0x0A)
	{
		// Use the token map first
		int mapEntry = 0;

		method = GetMethodByCodeReference(codeReference);
		uint32_t* entries = method -> tokenMap;
		while (mapEntry < method->tokenMapEntries * 2)
		{
			uint32_t memberRef = entries[mapEntry + 1];
			TRACE(Firmata.sendString(F("MemberRef token 0x"), entries[mapEntry + 1]));
			TRACE(Firmata.sendString(F("MethodDef token 0x"), entries[mapEntry]));
			if (memberRef == token)
			{
				token = entries[mapEntry];
				break;
			}
			mapEntry += 2;
		}
	}

	return GetMethodByToken(token);
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

	Firmata.sendString(F("Reference not found: "), codeReference);
	return nullptr;
}

IlCode* FirmataIlExecutor::GetMethodByToken(uint32_t token)
{
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

	Firmata.sendString(F("Execution memory cleared. Free bytes: 0x"), freeMemory());
}
