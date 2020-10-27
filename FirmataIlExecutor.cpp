/*
  FirmataIlExecutor

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  See file LICENSE.txt for further informations on licensing terms.

*/


#include <ConfigurableFirmata.h>
#include <FreeMemory.h>
#include "FirmataIlExecutor.h"
#include "openum.h"
#include "ObjectStack.h"
typedef byte BYTE;
typedef uint32_t DWORD;

const byte OpcodeInfo[] PROGMEM =
{
#define OPDEF(c,s,pop,push,type,args,l,s1,s2,ctrl) type,
//OPDEF(CEE_NOP, "nop", Pop0, Push0, InlineNone, 0, 1, 0xFF, 0x00, NEXT)
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
	switch (command) {
	case SCHEDULER_DATA:
		if (argc < 3)
		{
			Firmata.sendString(F("Error in Scheduler command: Not enough parameters"));
			return false;
		}
		if (argv[0] != 0xFF)
		{
			// Scheduler message type must be 0xFF, specific meaning follows
			return false;
		}
		subCommand = (ExecutorCommand)argv[1];
		if (IsExecutingCode() && subCommand != ExecutorCommand::ResetExecutor && subCommand != ExecutorCommand::KillTask)
		{
			Firmata.sendString(F("Execution engine busy. Ignoring command."));
			SendNack(subCommand, ExecutionError::EngineBusy);
			return true;
		}

		// Firmata.sendString(F("Executing Scheduler command 0x"), subCommand);
		switch (subCommand)
		{
		case ExecutorCommand::LoadIl:
			if (argc < 6)
			{
				Firmata.sendString(F("Not enough IL data parameters"));
				SendNack(subCommand, ExecutionError::InvalidArguments);
				return true;
			}
			LoadIlDataStream(argv[2], argv[3], argv[4], argc - 5, argv + 5);
			SendAck(subCommand);
			break;
		case ExecutorCommand::StartTask:
			DecodeParametersAndExecute(argv[2], argc - 3, argv + 3);
			SendAck(subCommand);
			break;
		case ExecutorCommand::DeclareMethod:
			if (argc < 6)
			{
				Firmata.sendString(F("Not enough IL data parameters"));
				SendNack(subCommand, ExecutionError::InvalidArguments);
				return true;
			}
			LoadIlDeclaration(argv[2], argv[3], argv[4], argc - 5, argv + 5);
			SendAck(subCommand);
			break;
		case ExecutorCommand::SetMethodTokens:
			if (argc < 6)
			{
				Firmata.sendString(F("Not enough IL data parameters"));
				SendNack(subCommand, ExecutionError::InvalidArguments);
				return true;
			}
			LoadMetadataTokenMapping(argv[2], argc - 3, argv + 3);
			SendAck(subCommand);
			break;
		case ExecutorCommand::ResetExecutor:
			if (argv[2] == 1)
			{
				KillCurrentTask();
				reset();
				SendAck(subCommand);
			}
			else
			{
				SendNack(subCommand, ExecutionError::InvalidArguments);
			}
			break;
		case ExecutorCommand::KillTask:
		{
			KillCurrentTask();
			SendAck(subCommand);
			break;
		}
		break;

		}

		return true;
	}
	return false;
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
	SendExecutionResult(topLevelMethod, 0, MethodState::Killed);
	Firmata.sendString(F("Code execution aborted"));
}

void FirmataIlExecutor::runStep()
{
	// Check that we have an existing execution context, and if so continue there.
	if (!IsExecutingCode())
	{
		return;
	}

	uint32_t retVal = 0;
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

void FirmataIlExecutor::LoadIlDeclaration(byte codeReference, int flags, byte maxLocals, byte argc, byte* argv)
{
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
	method->numArgs = argv[0];
	uint32_t token = DecodeUint32(argv + 1);
	method->methodToken = token;
	// Firmata.sendStringf(F("Loaded metadata for token 0x%lx, Flags 0x%x"), 6, token, (int)flags);
}

void FirmataIlExecutor::LoadMetadataTokenMapping(byte codeReference, byte argc, byte* argv)
{
	IlCode* method = GetMethodByCodeReference(codeReference);
	if (method == nullptr)
	{
		// This operation is illegal if the method is unknown
		Firmata.sendString(F("LoadMetadataTokenMapping for unknown codeReference"));
		return;
	}
	
	if (method->tokenMap != nullptr)
	{
		free(method->tokenMap);
		method->tokenMap = nullptr;
	}

	// No need to care about signed/unsigned here, because the top bit of metadata tokens is never used
	byte numTokens = argc / 16;
	uint32_t* tokens = (uint32_t*)malloc(numTokens * 8);
	for (int i = 0; i < numTokens * 2; i++)
	{
		tokens[i] = DecodeUint32(argv + (8 * i));
		//Firmata.sendStringf(F("Metadata token loaded: 0x%lx (%x, %x, %x, %x, %x, %x, %x, %x)"), 20, token, 
		//	(int)argv[(8 * i)], (int)argv[(8 * i) + 1], (int)argv[(8 * i) + 2], (int)argv[(8 * i) + 3], 
		//	(int)argv[(8 * i) + 4], (int)argv[(8 * i) + 5], (int)argv[(8 * i) + 6], (int)argv[(8 * i) + 7]);
	}

	method->tokenMapEntries = numTokens;
	method->tokenMap = tokens;
	Firmata.sendStringf(F("%d metadata tokens loaded for method %d"), 4, (int)numTokens, (int)codeReference);
}

void FirmataIlExecutor::LoadIlDataStream(byte codeReference, byte codeLength, byte offset, byte argc, byte* argv)
{
	IlCode* method = GetMethodByCodeReference(codeReference);
	if (method == nullptr)
	{
		// This operation is illegal if the method is unknown
		Firmata.sendString(F("LoadIlDataStream for unknown codeReference 0x"), codeReference);
		return;
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
			return;
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

void FirmataIlExecutor::SendExecutionResult(byte codeReference, uint32_t result, MethodState execResult)
{
	byte replyData[4];
	// Reply format:
	// byte 0: 1 on success, 0 on (technical) failure, such as unsupported opcode
	// byte 1: Number of integer values returned
	// bytes 2+: Integer return values
	
	replyData[0] = result & 0xFF;
	replyData[1] = (result >> 8) & 0xFF;
	replyData[2] = (result >> 16) & 0xFF;
	replyData[3] = (result >> 24) & 0xFF;

	Firmata.startSysex();
	Firmata.write(SCHEDULER_DATA);
	Firmata.write(codeReference);
	
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

void FirmataIlExecutor::DecodeParametersAndExecute(byte codeReference, byte argc, byte* argv)
{
	uint32_t result = 0;
	IlCode* method = GetMethodByCodeReference(codeReference);
	// Firmata.sendStringf(F("Code execution for %d starts. Stack Size is %d."), 4, codeReference, _methods[codeReference].maxLocals);
	ExecutionState* rootState = new ExecutionState(codeReference, method->maxLocals, method->numArgs);
	_methodCurrentlyExecuting = rootState;
	for (int i = 0; i < method->numArgs; i++)
	{
		rootState->UpdateArg(i, DecodeUint32(argv + (8 * i)));
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

void InvalidOpCode(int opCode)
{
	Firmata.sendString(F("Invalid/Unsupported opcode "), opCode);
}

// Executes the given OS function. Note that args[0] is the this pointer
uint32_t ExecuteSpecialMethod(byte method, ObjectList* args)
{
	switch(method)
	{
		case 0: // Sleep(int delay)
			delay(args->Get(1));
			break;
		case 1: // PinMode(int pin, PinMode mode)
		{
			int mode = INPUT;
			if (args->Get(2) == 1)
			{
				mode = OUTPUT;
			}
			if (args->Get(2) == 2)
			{
				mode = INPUT_PULLUP;
			}
			pinMode(args->Get(1), mode);
			break;
		}
		case 2: // Write(int pin, int value)
			digitalWrite(args->Get(1), args->Get(2));
			break;
		case 5: // TickCount
			return millis();
		default:
			Firmata.sendString(F("Unknown internal method: "), method);
			break;
	}
	return 0;
}

// Preconditions for save execution: 
// - codeLength is correct
// - argc matches argList
// - It was validated that the method has exactly argc arguments
MethodState FirmataIlExecutor::ExecuteIlCode(ExecutionState *rootState, uint32_t* returnValue)
{
	const int NUM_INSTRUCTIONS_AT_ONCE = 5;
	
	ExecutionState* currentFrame = rootState;
	while (currentFrame->_next != NULL)
	{
		currentFrame = currentFrame->_next;
	}

	int instructionsExecuted = 0;
	short PC = 0;
	ObjectStack* stack;
	ObjectList* locals;
	ObjectList* arguments;
	
	currentFrame->ActivateState(&PC, &stack, &locals, &arguments);

	IlCode* currentMethod = GetMethodByCodeReference(currentFrame->MethodIndex());
	int currentMethodReference = currentMethod->codeReference;

	Firmata.sendStringf(F("Continuation at: 0x%x in Method %d"), 4, PC, currentMethodReference);
	byte* pCode = currentMethod->methodIl;
	// The compiler always inserts a return statement, so we can never run past the end of a method,
	// however we use this counter to interrupt code execution every now and then to go back to the main loop
	// and check for other tasks (i.e. serial input data)
    while (instructionsExecuted < NUM_INSTRUCTIONS_AT_ONCE)
    {
		instructionsExecuted++;
    	
        DWORD   len;
        OPCODE  instr;
		
		Firmata.sendStringf(F("PC: 0x%x in Method %d"), 4, PC, currentMethodReference);
        
		if (PC == 0 && (currentMethod->methodFlags & (byte)MethodFlags::Special))
		{
			int specialMethod = currentMethod->maxLocals;
			
			Firmata.sendString(F("Executing special method "), specialMethod);
			uint32_t retVal = ExecuteSpecialMethod(specialMethod, arguments);
			
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
			if ((currentMethod->methodFlags & (byte)MethodFlags::Void) == 0)
			{
				stack->push(retVal);
			}

			currentMethod = GetMethodByCodeReference(currentFrame->MethodIndex());

			pCode = currentMethod->methodIl;
			continue;
		}

		instr = DecodeOpcode(&pCode[PC], &len);
        if (instr == CEE_COUNT)
        {
			InvalidOpCode(instr);
            return MethodState::Aborted;
        }
		
		PC += len;
		
		uint32_t intermediate;
		
		byte opCodeType = pgm_read_byte(OpcodeInfo + instr);
            
		switch (opCodeType)
        {
            case InlineNone:
            {
                switch (instr)
                {
                    case CEE_RET:
					{
						if (!stack->empty())
						{
							*returnValue = stack->pop();
						}
						else 
						{
							*returnValue = 0;
						}
                    		
						bool oldMethodIsVoid = currentMethod->methodFlags & (byte)MethodFlags::Void;
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

						currentMethod = GetMethodByCodeReference(currentFrame->MethodIndex());
                    	
						pCode = currentMethod->methodIl;
						// If the method we just terminated is not of type void, we push the result to the 
						// stack of the calling method (methodIndex still points to the old frame)
						if (!stack->empty())
						{
							Firmata.sendString(F("For debug apps, stack should be empty now"));
						}
						else
						{
							Firmata.sendString(F("Stack is empty - returning now."));
						}
                    		
						if (!oldMethodIsVoid)
						{
							stack->push(*returnValue);

							Firmata.sendString(F("Pushing return value: "), *returnValue);
						}
					}
					break;
                    case CEE_THROW:
						InvalidOpCode(instr);
                        return MethodState::Aborted;
					case CEE_NOP:
						break;
					case CEE_BREAK:
						// This should not normally occur in code
						InvalidOpCode(instr);
						return MethodState::Aborted;
						break;
					case CEE_LDARG_0:
						stack->push(arguments->Get(0));
						break;
					case CEE_LDARG_1:
						stack->push(arguments->Get(1));
						break;
					case CEE_LDARG_2:
						stack->push(arguments->Get(2));
						break;
					case CEE_LDARG_3:
						stack->push(arguments->Get(3));
						break;
					case CEE_STLOC_0:
						intermediate = stack->pop();
						locals->Set(0, intermediate);
						break;
					case CEE_STLOC_1:
						intermediate = stack->pop();
						locals->Set(1, intermediate);
						break;
					case CEE_STLOC_2:
						intermediate = stack->pop();
						locals->Set(2, intermediate);
						break;
					case CEE_STLOC_3:
						intermediate = stack->pop();
						locals->Set(3, intermediate);
						break;
					case CEE_LDLOC_0:
						stack->push(locals->Get(0));
						break;
					case CEE_LDLOC_1:
						stack->push(locals->Get(1));
						break;
					case CEE_LDLOC_2:
						stack->push(locals->Get(2));
						break;
					case CEE_LDLOC_3:
						stack->push(locals->Get(3));
						break;
					case CEE_ADD:
						intermediate = stack->pop() + stack->pop();
						stack->push(intermediate);
						break;
					case CEE_SUB:
						intermediate = stack->pop() - stack->pop();
						stack->push(intermediate);
						break;
					case CEE_MUL:
						intermediate = stack->pop() * stack->pop();
						stack->push(intermediate);
						break;
					case CEE_DIV:
						// TODO: Proper typing required for this and the next
						intermediate = stack->pop();
						if (intermediate == 0)
						{
							return MethodState::Aborted;
						}
						intermediate = stack->pop() / intermediate;
						stack->push(intermediate);
						break;
					case CEE_REM:
						intermediate = stack->pop();
						if (intermediate == 0)
						{
							return MethodState::Aborted;
						}
						intermediate = stack->pop() % intermediate;
						stack->push(intermediate);
						break;
					case CEE_DIV_UN:
						intermediate = stack->pop();
						if (intermediate == 0)
						{
							return MethodState::Aborted;
						}
						intermediate = stack->pop() / intermediate;
						stack->push(intermediate);
						break;
					case CEE_REM_UN:
						intermediate = stack->pop();
						if (intermediate == 0)
						{
							return MethodState::Aborted;
						}
						intermediate = stack->pop() % intermediate;
						stack->push(intermediate);
						break;
					case CEE_CEQ:
						stack->push(stack->pop() == stack->pop());
						break;
					case CEE_CGT:
						stack->push(stack->pop() > stack->pop());
						break;
					case CEE_NOT:
						stack->push(~stack->pop());
						break;
					case CEE_NEG:
						stack->push(-stack->pop());
						break;
					case CEE_AND:
						stack->push(stack->pop() & stack->pop());
						break;
					case CEE_OR:
						stack->push(stack->pop() | stack->pop());
						break;
					case CEE_XOR:
						stack->push(stack->pop() ^ stack->pop());
						break;
					case CEE_CLT:
						stack->push(stack->pop() < stack->pop());
						break;
					case CEE_SHL:
						stack->push(stack->pop() << stack->pop());
						break;
					case CEE_SHR:
						stack->push((int)stack->pop() >> stack->pop());
						break;
					case CEE_SHR_UN:
						stack->push((uint32_t)stack->pop() >> stack->pop());
						break;
					case CEE_LDC_I4_0:
						stack->push(0);
						break;
					case CEE_LDC_I4_1:
						stack->push(1);
						break;
					case CEE_LDC_I4_2:
						stack->push(2);
						break;
					case CEE_LDC_I4_3:
						stack->push(3);
						break;
					case CEE_LDC_I4_4:
						stack->push(4);
						break;
					case CEE_LDC_I4_5:
						stack->push(5);
						break;
					case CEE_LDC_I4_6:
						stack->push(6);
						break;
					case CEE_LDC_I4_7:
						stack->push(7);
						break;
					case CEE_LDC_I4_8:
						stack->push(8);
						break;
					case CEE_LDC_I4_M1:
						stack->push(-1);
						break;
					case CEE_DUP:
						intermediate = stack->peek();
						stack->push(intermediate);
						break;
					case CEE_POP:
						stack->pop();
						break;
					case CEE_LDIND_I1:
						// TODO: Fix type of stack (must support dynamic typing)
						intermediate = stack->pop();
                    {
							int8_t b = *((int8_t*)intermediate);
							stack->push(b);
                    }
					break;
					case CEE_LDIND_I2:
						intermediate = stack->pop();
						{
							int16_t s = *((int16_t*)intermediate);
							stack->push(s);
						}
						break;
					case CEE_LDIND_I4:
						intermediate = stack->pop();
						{
							int32_t i = *((int32_t*)intermediate);
							stack->push(i);
						}
						break;
					case CEE_LDIND_U1:
						intermediate = stack->pop();
						{
							byte b = *((byte*)intermediate);
							stack->push(b);
						}
						break;
					case CEE_LDIND_U2:
						intermediate = stack->pop();
						{
							uint16_t s = *((uint16_t*)intermediate);
							stack->push(s);
						}
						break;
					case CEE_LDIND_U4:
						intermediate = stack->pop();
						{
							uint32_t i = *((uint32_t*)intermediate);
							stack->push(i);
						}
						break;
                    default:
						InvalidOpCode(instr);
                        return MethodState::Aborted;
                }
				break;
            }
			case ShortInlineI:
			case ShortInlineVar:
	            {
					char data = (char)pCode[PC];

					PC++;
		            switch(instr)
		            {
					case CEE_UNALIGNED_: /*Ignore prefix, we don't need alignment. Just execute the actual instruction*/
						PC--;
						continue;
					case CEE_LDC_I4_S:
						stack->push(data);
						break;
					case CEE_LDLOC_S:
						stack->push(locals->Get(data));
						break;
					case CEE_STLOC_S:
						locals->Set(data, stack->pop());
						break;
					case CEE_LDLOCA_S:
						// Warn: Pointer to data conversion!
						stack->push((uint32_t)locals->AddressOf(data));
						break;
					case CEE_LDARG_S:
						stack->push(arguments->Get(data));
						break;
					case CEE_LDARGA_S:
						// Get address of argument x. 
						// TOOD: Byref parameter handling is not supported at the moment by the call implementation. 
						stack->push((uint32_t)arguments->AddressOf(data));
						break;
					case CEE_STARG_S:
						arguments->Set(data, stack->pop());
						break;
					default:
						InvalidOpCode(instr);
						break;
		            }
	            }
                break;
			/*
            case ShortInlineI:
            case ShortInlineVar:
            {
                unsigned char  ch= pCode[PC];
                short sh = OpcodeInfo[instr].Type==ShortInlineVar ? ch : (ch > 127 ? -(256-ch) : ch);
                if(g_fShowBytes)
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%2.2X ", ch);
                    Len += 3;
                    PadTheString;
                }
                switch(instr)
                {
                    case CEE_LDARG_S:
                    case CEE_LDARGA_S:
                    case CEE_STARG_S:
                        if(g_fThisIsInstanceMethod &&(ch==0))
                        { // instance methods have arg0="this", do not label it!
                            szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s %d", pszInstrName, ch);
                        }
                        else
                        {
                            if(pszArgname)
                            {
                                unsigned char ch1 = g_fThisIsInstanceMethod ? ch-1 : ch;
                                if(ch1 < ulArgs)
                                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),"%-10s %s",pszInstrName,
                                                    ProperLocalName(pszArgname[ch1].name));
                                else
                                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),ERRORMSG(RstrUTF(IDS_E_ARGINDEX)),pszInstrName, ch,ulArgs);
                            }
                            else szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s A_%d",pszInstrName, ch);
                        }
                        break;

                    case CEE_LDLOC_S:
                    case CEE_LDLOCA_S:
                    case CEE_STLOC_S:
                        if(pszLVname)
                        {
                            if(ch < ulVars) szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s %s", pszInstrName,
                                ProperLocalName(pszLVname[ch].name));
                            else
                                szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),ERRORMSG(RstrUTF(IDS_E_LVINDEX)),pszInstrName, ch, ulVars);
                        }
                        else szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s V_%d",pszInstrName, ch);
                        break;

                    default:
                        szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s %d", pszInstrName,sh);
                }

                PC++;
                break;
            }

            case InlineVar:
            {
                if(g_fShowBytes)
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%2.2X%2.2X ", pCode[PC], pCode[PC+1]);
                    Len += 5;
                    PadTheString;
                }

                USHORT v = pCode[PC] + (pCode[PC+1] << 8);
                long l = OpcodeInfo[instr].Type==InlineVar ? v : (v > 0x7FFF ? -(0x10000 - v) : v);

                switch(instr)
                {
                    case CEE_LDARGA:
                    case CEE_LDARG:
                    case CEE_STARG:
                        if(g_fThisIsInstanceMethod &&(v==0))
                        { // instance methods have arg0="this", do not label it!
                            szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s %d", pszInstrName, v);
                        }
                        else
                        {
                            if(pszArgname)
                            {
                                USHORT v1 = g_fThisIsInstanceMethod ? v-1 : v;
                                if(v1 < ulArgs)
                                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),"%-10s %s",pszInstrName,
                                                    ProperLocalName(pszArgname[v1].name));
                                else
                                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),ERRORMSG(RstrUTF(IDS_E_ARGINDEX)),pszInstrName, v,ulArgs);
                            }
                            else szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s A_%d",pszInstrName, v);
                        }
                        break;

                    case CEE_LDLOCA:
                    case CEE_LDLOC:
                    case CEE_STLOC:
                        if(pszLVname)
                        {
                            if(v < ulVars)  szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s %s", pszInstrName,
                                ProperLocalName(pszLVname[v].name));
                            else
                                szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),ERRORMSG(RstrUTF(IDS_E_LVINDEX)),pszInstrName, v,ulVars);
                        }
                        else szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s V_%d",pszInstrName, v);
                        break;

                    default:
                        szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s %d", pszInstrName, l);
                        break;
                }
                PC += 2;
                break;
            }

            case InlineI:
            case InlineRVA:
            {
                DWORD v = pCode[PC] + (pCode[PC+1] << 8) + (pCode[PC+2] << 16) + (pCode[PC+3] << 24);
                if(g_fShowBytes)
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%2.2X%2.2X%2.2X%2.2X ", pCode[PC], pCode[PC+1], pCode[PC+2], pCode[PC+3]);
                    Len += 9;
                    PadTheString;
                }
                szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s 0x%x", pszInstrName, v);
                PC += 4;
                break;
            }

            case InlineI8:
            {
                __int64 v = (__int64) pCode[PC] +
                            (((__int64) pCode[PC+1]) << 8) +
                            (((__int64) pCode[PC+2]) << 16) +
                            (((__int64) pCode[PC+3]) << 24) +
                            (((__int64) pCode[PC+4]) << 32) +
                            (((__int64) pCode[PC+5]) << 40) +
                            (((__int64) pCode[PC+6]) << 48) +
                            (((__int64) pCode[PC+7]) << 56);

                if(g_fShowBytes)
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),
                        "%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X",
                        pCode[PC], pCode[PC+1], pCode[PC+2], pCode[PC+3],
                        pCode[PC+4], pCode[PC+5], pCode[PC+6], pCode[PC+7]);
                    Len += 8*2;
                    PadTheString;
                }

                szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s 0x%I64x", pszInstrName, v);
                PC += 8;
                break;
            }

            case ShortInlineR:
            {
                __int32 v = (__int32) pCode[PC] +
                            (((__int32) pCode[PC+1]) << 8) +
                            (((__int32) pCode[PC+2]) << 16) +
                            (((__int32) pCode[PC+3]) << 24);

                float f = (float&)v;

                if(g_fShowBytes)
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%2.2X%2.2X%2.2X%2.2X ", pCode[PC], pCode[PC+1], pCode[PC+2], pCode[PC+3]);
                    Len += 9;
                    PadTheString;
                }

                char szf[32];
                if(f==0.0)
                    strcpy_s(szf,32,((v>>24)==0)? "0.0" : "-0.0");
                else
                    _gcvt_s(szf,32,(double)f, 8);
                float fd = (float)atof(szf);
                // Must compare as underlying bytes, not floating point otherwise optmizier will
                // try to enregister and comapre 80-bit precision number with 32-bit precision number!!!!
                if(((__int32&)fd == v)&&!IsSpecialNumber(szf))
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s %s", pszInstrName, szf);
                else
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s (%2.2X %2.2X %2.2X %2.2X)",
                        pszInstrName, pCode[PC], pCode[PC+1], pCode[PC+2], pCode[PC+3]);
                PC += 4;
                break;
            }

            case InlineR:
            {
                __int64 v = (__int64) pCode[PC] +
                            (((__int64) pCode[PC+1]) << 8) +
                            (((__int64) pCode[PC+2]) << 16) +
                            (((__int64) pCode[PC+3]) << 24) +
                            (((__int64) pCode[PC+4]) << 32) +
                            (((__int64) pCode[PC+5]) << 40) +
                            (((__int64) pCode[PC+6]) << 48) +
                            (((__int64) pCode[PC+7]) << 56);

                double d = (double&)v;

                if(g_fShowBytes)
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),
                        "%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X",
                        pCode[PC], pCode[PC+1], pCode[PC+2], pCode[PC+3],
                        pCode[PC+4], pCode[PC+5], pCode[PC+6], pCode[PC+7]);
                    Len += 8*2;
                    PadTheString;
                }
                char szf[32],*pch;
                if(d==0.0)
                    strcpy_s(szf,32,((v>>56)==0)? "0.0" : "-0.0");
                else
                    _gcvt_s(szf,32,d, 17);
                double df = strtod(szf, &pch); //atof(szf);
                // Must compare as underlying bytes, not floating point otherwise optmizier will
                // try to enregister and comapre 80-bit precision number with 64-bit precision number!!!!
                if (((__int64&)df == v)&&!IsSpecialNumber(szf))
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s %s", pszInstrName, szf);
                else
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s (%2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X)",
                        pszInstrName, pCode[PC], pCode[PC+1], pCode[PC+2], pCode[PC+3],
                        pCode[PC+4], pCode[PC+5], pCode[PC+6], pCode[PC+7]);
                PC += 8;
                break;
            } */

            case ShortInlineBrTarget:
            {
				bool doBranch = false;
				switch (instr)
				{
					case CEE_BR_S:
						doBranch = true;
						break;
					case CEE_BEQ_S:
						doBranch = stack->pop() == stack->pop();
						break;
					case CEE_BGE_S:
						doBranch = stack->pop() >= stack->pop();
						break;
					case CEE_BLE_S:
						doBranch = stack->pop() <= stack->pop();
						break;
					case CEE_BGT_S:
						doBranch = stack->pop() > stack->pop();
						break;
					case CEE_BLT_S:
						doBranch = stack->pop() < stack->pop();
						break;
					case CEE_BGE_UN_S:
						doBranch = (uint32_t)stack->pop() >= (uint32_t)stack->pop();
						break;
					case CEE_BGT_UN_S:
						doBranch = (uint32_t)stack->pop() > (uint32_t)stack->pop();
						break;
					case CEE_BLE_UN_S:
						doBranch = (uint32_t)stack->pop() <= (uint32_t)stack->pop();
						break;
					case CEE_BLT_UN_S:
						doBranch = (uint32_t)stack->pop() < (uint32_t)stack->pop();
						break;
					case CEE_BNE_UN_S:
						doBranch = (uint32_t)stack->pop() != (uint32_t)stack->pop();
						break;
					case CEE_BRFALSE_S:
						doBranch = stack->pop() == 0;
						break;
					case CEE_BRTRUE_S:
						doBranch = stack->pop() == 0;
						break;
					default:
						InvalidOpCode(instr);
						return MethodState::Aborted;
				}
				
				if (doBranch)
				{
					char offset = (char) pCode[PC];
					long dest = (PC + 1) + (long) offset;
                
					PC = dest;
				}
				else 
				{
					PC++; // Skip offset byte
				}
				
                break;
            }
/*
            case InlineBrTarget:
            {
                long offset = pCode[PC] + (pCode[PC+1] << 8) + (pCode[PC+2] << 16) + (pCode[PC+3] << 24);
                long dest = (PC + 4) + (long) offset;

                if(g_fShowBytes)
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%2.2X%2.2X%2.2X%2.2X ", pCode[PC], pCode[PC+1], pCode[PC+2], pCode[PC+3]);
                    Len += 9;
                    PadTheString;
                }
                PC += 4;
                szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s IL_%04x", pszInstrName, dest);

                fNeedNewLine = TRUE;
                break;
            }

            case InlineSwitch:
            {
                DWORD cases = pCode[PC] + (pCode[PC+1] << 8) + (pCode[PC+2] << 16) + (pCode[PC+3] << 24);

                if(g_fShowBytes)
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%2.2X%2.2X%2.2X%2.2X ", pCode[PC], pCode[PC+1], pCode[PC+2], pCode[PC+3]);
                    Len += 9;
                    PadTheString;
                }
                if(cases) szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s ( ", pszInstrName);
                else szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s ( )", pszInstrName);
                printLine(GUICookie, szString);
                PC += 4;
                DWORD PC_nextInstr = PC + 4 * cases;
                for (i = 0; i < cases; i++)
                {
                    long offset = pCode[PC] + (pCode[PC+1] << 8) + (pCode[PC+2] << 16) + (pCode[PC+3] << 24);
                    long dest = PC_nextInstr + (long) offset;
                    szptr = &szString[0];
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),"%s          ",g_szAsmCodeIndent); //indent+label
                    if(g_fShowBytes)
                    {
                        szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),"/*      | %2.2X%2.2X%2.2X%2.2X ",         // comment
                            pCode[PC], pCode[PC+1], pCode[PC+2], pCode[PC+3]);
                        Len = 9;
                        PadTheString;
                    }

                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),"            IL_%04x%s", dest,(i == cases-1)? ")" : ",");
                    PC += 4;
                    printLine(GUICookie, szString);
                }
                continue;
            }

            case InlinePhi:
            {
                DWORD cases = pCode[PC];
                unsigned short *pus;
                DWORD i;

                if(g_fShowBytes)
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%2.2X", cases);
                    Len += 2;
                    for(i=0; i < cases*2; i++)
                    {
                        szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%2.2X", pCode[PC+1+i]);
                        Len += 2;
                    }
                    PadTheString;
                }

                szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s", pszInstrName);
                for(i=0, pus=(unsigned short *)(&pCode[PC+1]); i < cases; i++,pus++)
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr)," %d",*pus);
                }
                PC += 2 * cases + 1;
                break;
            }

            case InlineString:
            case InlineField:
            case InlineType:
            case InlineTok:
            case InlineMethod:
            {
                tk = pCode[PC] + (pCode[PC+1] << 8) + (pCode[PC+2] << 16) + (pCode[PC+3] << 24);
                tkType = TypeFromToken(tk);

                // Backwards compatible ldstr instruction.
                if (instr == CEE_LDSTR && TypeFromToken(tk) != mdtString)
                {
                    const WCHAR *v1 = W("");

                    if(g_fShowBytes)
                    {
                        szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%2.2X%2.2X%2.2X%2.2X ",
                            pCode[PC], pCode[PC+1], pCode[PC+2], pCode[PC+3]);
                        Len += 9;
                        PadTheString;
                    }

                    if(!g_pPELoader->getVAforRVA(tk, (void**) &v1))
                    {
                        char szStr[256];
                        sprintf_s(szStr,256,RstrUTF(IDS_E_SECTHEADER),tk);
                        printLine(GUICookie,szStr);
                    }
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s ", pszInstrName);
                    ConvToLiteral(szptr, v1, 0xFFFF);
                    PC += 4;
                    break;
                }

                if(g_fShowBytes)
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "(%2.2X)%2.2X%2.2X%2.2X ",
                        pCode[PC+3], pCode[PC+2], pCode[PC+1], pCode[PC]);
                    Len += 11;
                    PadTheString;
                }
                PC += 4;

                szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s ", pszInstrName);

                if ((tk & 0xFF000000) == 0)
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%#x ", tk);
                    break;
                }
                if(!pImport->IsValidToken(tk))
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),ERRORMSG(" [ERROR: INVALID TOKEN 0x%8.8X] "),tk);
                    break;
                }
                if(OpcodeInfo[instr].Type== InlineTok)
                {
                    switch (tkType)
                    {
                        default:
                            break;

                        case mdtMethodDef:
                        case mdtMethodSpec:
                            szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),KEYWORD("method "));
                            break;

                        case mdtFieldDef:
                            szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),KEYWORD("field "));
                            break;

                        case mdtMemberRef:
                            {
                                PCCOR_SIGNATURE typePtr;
                                const char*         pszMemberName;
                                ULONG       cComSig;

                                if (FAILED(pImport->GetNameAndSigOfMemberRef(
                                    tk,
                                    &typePtr,
                                    &cComSig,
                                    &pszMemberName)))
                                {
                                    szptr += sprintf_s(szptr, SZSTRING_REMAINING_SIZE(szptr), "ERROR ");
                                    break;
                                }
                                unsigned callConv = CorSigUncompressData(typePtr);

                                if (isCallConv(callConv, IMAGE_CEE_CS_CALLCONV_FIELD))
                                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),KEYWORD("field "));
                                else
                                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr),KEYWORD("method "));
                                break;
                            }
                    }
                }
                PrettyPrintToken(szString, tk, pImport,GUICookie,FuncToken); //TypeDef,TypeRef,TypeSpec,MethodDef,FieldDef,MemberRef,MethodSpec,String
                break;
            }

            case InlineSig:
            {
                if(g_fShowBytes)
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%2.2X%2.2X%2.2X%2.2X ",
                        pCode[PC], pCode[PC+1], pCode[PC+2], pCode[PC+3]);
                    // output the offset and the raw bytes
                    Len += 9;
                    PadTheString;
                }

                // get the signature token
                tk = pCode[PC] + (pCode[PC+1] << 8) + (pCode[PC+2] << 16) + (pCode[PC+3] << 24);
                PC += 4;
                tkType = TypeFromToken(tk);
                if (tkType == mdtSignature)
                {
                    // get the signature from the token
                    DWORD           cbSigLen;
                    PCCOR_SIGNATURE pComSig;
                    CQuickBytes     qbMemberSig;
                    if (FAILED(pImport->GetSigFromToken(tk, &cbSigLen, &pComSig)))
                    {
                        sprintf_s(szString, SZSTRING_SIZE, COMMENT("// ERROR: Invalid %08X record"), tk);
                        break;
                    }

                    qbMemberSig.Shrink(0);
                    const char* pszTailSig = PrettyPrintSig(pComSig, cbSigLen, "", &qbMemberSig, pImport,NULL);
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), "%-10s %s", pszInstrName, pszTailSig);
                }
                else
                {
                    szptr+=sprintf_s(szptr,SZSTRING_REMAINING_SIZE(szptr), ERRORMSG(RstrUTF(IDS_E_BADTOKENTYPE)), pszInstrName, tk);
                }
                break;
            }
			*/
			
			case InlineMethod:
            {
				if (instr != CEE_CALLVIRT && instr != CEE_CALL) 
				{
					InvalidOpCode(PC - 1);
					return MethodState::Aborted;
				}
				
				uint32_t tk = ((uint32_t)pCode[PC]) | ((uint32_t)pCode[PC+1] << 8) | ((uint32_t)pCode[PC+2] << 16) | ((uint32_t)pCode[PC+3] << 24);
                PC += 4;

                IlCode* newMethod = ResolveToken(currentMethod->codeReference, tk);
				
				if (newMethod == nullptr)
				{
					Firmata.sendString(F("Unknown token 0x"), tk);
					return MethodState::Aborted;
				}

            	// Save return PC
                currentFrame->UpdatePc(PC);
				int stackSize = newMethod->maxLocals;
            		
				if (newMethod->methodFlags & (byte)MethodFlags::Special)
				{
					stackSize = 0;
				}
				
				int argumentCount = newMethod->numArgs;
				ExecutionState* newState = new ExecutionState(newMethod->codeReference, stackSize, argumentCount);
				currentFrame->_next = newState;
				
				ObjectStack* oldStack = stack;
				// Start of the called method
				currentFrame = newState;
				currentFrame->ActivateState(&PC, &stack, &locals, &arguments);

            	// Load data pointer for the new method
				currentMethod = newMethod;
				pCode = newMethod->methodIl;
            	
				// Provide arguments to the new method
				while (argumentCount > 0)
				{
					argumentCount--;
					arguments->Set(argumentCount, oldStack->pop());
				}
            		
				Firmata.sendStringf(F("Pushed stack to method %d"), 2, currentMethod->codeReference);
				break;
            }
			default:
				InvalidOpCode(instr);
				return MethodState::Aborted;
        }
    	
    	// TODO: This is unneccessary
		currentFrame->UpdatePc(PC);
	}
	
	// We interrupted execution to not waste to much time here - the parent will return to us asap
	currentFrame->UpdatePc(PC);
	Firmata.sendString(F("Interrupting method at 0x"), PC);
	return MethodState::Running;
}

IlCode* FirmataIlExecutor::ResolveToken(byte codeReference, uint32_t token)
{
	IlCode* method;
	if ((token >> 24) == 0x0A)
	{
		// Use the token map first
		int mapEntry = 0;

		method = GetMethodByCodeReference(codeReference);
		uint32_t* entries = method -> tokenMap;
		while (mapEntry < method->tokenMapEntries * 2)
		{
			uint32_t memberRef = entries[mapEntry + 1];
			Firmata.sendString(F("MemberRef token 0x"), entries[mapEntry + 1]);
			Firmata.sendString(F("MethodDef token 0x"), entries[mapEntry]);
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

void FirmataIlExecutor::SendAck(ExecutorCommand subCommand)
{
	Firmata.startSysex();
	Firmata.write(SCHEDULER_DATA);
	Firmata.write((byte)ExecutorCommand::Ack);
	Firmata.write((byte)subCommand);
	Firmata.write(0); // Error code, just for completeness
	Firmata.endSysex();
}

void FirmataIlExecutor::SendNack(ExecutorCommand subCommand, ExecutionError errorCode)
{
	Firmata.startSysex();
	Firmata.write(SCHEDULER_DATA);
	Firmata.write((byte)ExecutorCommand::Nack);
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

IlCode* FirmataIlExecutor::GetMethodByCodeReference(byte codeReference)
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
		method = method->next;
		delete method;
	}

	delete _firstMethod;
	_firstMethod = nullptr;

	Firmata.sendString(F("Execution memory cleared. Free bytes: 0x"), freeMemory());
}

