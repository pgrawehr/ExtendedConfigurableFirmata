/*
  FirmataIlExecutor

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  See file LICENSE.txt for further informations on licensing terms.

*/


#include <ConfigurableFirmata.h>
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
}

void FirmataIlExecutor::handleCapability(byte pin)
{
	// TEST CODE only!
	/* if (pin == 0)
	{
		// Add args 0 and 1 and return the result
		byte code[] = { 00, 0x02, 0x03, 0x58, 0x0A, 0x2B, 0x00, 0x06, 0x2A, };
		int args[] = { 255, 7 };
		int result = 0;
		ExecuteIlCode(9, code, 10, 2, args, &result);
		Firmata.sendString(F("Code execution returned"), result);
	}*/
}

boolean FirmataIlExecutor::handleSysex(byte command, byte argc, byte *argv)
{
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
		  
		switch(argv[1])
		{
			case IL_LOAD: 
				if (argc < 4)
				{
					Firmata.sendString(F("Not enough IL data parameters"));
					return false;
				}
				LoadIlDataStream(argv[2], argv[3], argv[4], argc - 5, argv + 5);
			break;
			case IL_EXECUTE_NOW:
				DecodeParametersAndExecute(argv[2], argc - 3, argv + 3);
			break;
			case IL_DECLARE:
				LoadIlDeclaration(argv[2], argv[3], argv[4], argc - 5, argv + 5);
				break;
		}
        
        return true;
  }
  return false;
}

void FirmataIlExecutor::LoadIlDeclaration(byte codeReference, int flags, byte maxLocals, byte argc, byte* argv)
{
	if (_methods[codeReference].methodIl != NULL)
	{
		free(_methods[codeReference].methodIl);
		_methods[codeReference].methodIl = NULL;
	}
	
	_methods[codeReference].methodFlags = flags;
	_methods[codeReference].maxLocals = maxLocals;
	_methods[codeReference].numArgs = argv[0];
	uint32_t token = DecodeUint32(argv + 1);
	_methods[codeReference].methodToken = token;
	Firmata.sendStringf(F("Loaded metadata for token 0x%lx, Flags 0x%x"), 6, token, flags);
}

void FirmataIlExecutor::LoadIlDataStream(byte codeReference, byte codeLength, byte offset, byte argc, byte* argv)
{
	if (offset == 0)
	{
		if (_methods[codeReference].methodIl != NULL)
		{
			free(_methods[codeReference].methodIl);
			_methods[codeReference].methodIl = NULL;
		}
		byte* decodedIl = (byte*)malloc(codeLength);
		if (decodedIl == NULL)
		{
			Firmata.sendString(F("Not enough memory. "), codeLength);
			return;
		}
		int j = 0;
		for (byte i = 0; i < argc; i += 2) 
		{
			  decodedIl[j++] = argv[i] + (argv[i + 1] << 7);
		}
		_methods[codeReference].methodLength = codeLength;
		_methods[codeReference].methodIl = decodedIl;
	}
	else 
	{
		byte* decodedIl = _methods[codeReference].methodIl + offset;
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

void FirmataIlExecutor::DecodeParametersAndExecute(byte codeReference, byte argc, byte* argv)
{
	uint32_t result = 0;
	Firmata.sendStringf(F("Code execution for %d starts. Stack Size is %d."), 4, codeReference, _methods[codeReference].maxLocals);
	ExecutionState* rootState = new ExecutionState(codeReference, _methods[codeReference].maxLocals, _methods[codeReference].numArgs);
	for (int i = 0; i < _methods[codeReference].numArgs; i++)
	{
		rootState->UpdateArg(i, DecodeUint32(argv + (8 * i)));
	}
	
	bool execResult = ExecuteIlCode(rootState, _methods[codeReference].methodLength, 
		_methods[codeReference].methodIl, &result);
	
	delete rootState;
	
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
	Firmata.write(execResult ? 1 : 0);
	Firmata.write(1);
	for (int i = 0; i < 4; i++)
	{
		Firmata.sendValueAsTwo7bitBytes(replyData[i]);
	}
	Firmata.endSysex();
}

void InvalidOpCode(int offset)
{
	Firmata.sendString(F("Invalid/Unsupported opcode at offset "), offset);
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
bool FirmataIlExecutor::ExecuteIlCode(ExecutionState *state, int codeLength, byte* pCode, uint32_t* returnValue)
{
	ExecutionState* currentFrame = state;
	while (currentFrame->_next != NULL)
	{
		currentFrame = currentFrame->_next;
	}
	
	short LastPC = 0;
	
	short PC = 0;
	ObjectStack* stack;
	ObjectList* locals;
	ObjectList* arguments;
	
	currentFrame->ActivateState(&PC, &stack, &locals, &arguments);
	
    while (PC < codeLength)
    {
        DWORD   Len;
        OPCODE  instr;
		LastPC = PC;
		int methodIndex = currentFrame->MethodIndex();
		
		Firmata.sendStringf(F("PC: 0x%x in Method %d"), 4, PC, methodIndex);
        
		if (PC == 0 && (_methods[methodIndex].methodFlags & METHOD_SPECIAL))
		{
			int specialMethod = _methods[methodIndex].maxLocals;
			Firmata.sendString(F("Executing special method "), specialMethod);
			uint32_t retVal = ExecuteSpecialMethod(specialMethod, arguments);
			// We're called into a "special" (built-in) method. 
			// Perform a method return
			ExecutionState* frame = currentFrame;
			while (frame->_next != currentFrame)
			{
				frame = frame->_next;
			}
			// Remove the last frame and set the PC for the new current frame
			frame->_next = NULL;
			
			delete currentFrame;
			currentFrame = frame;
			currentFrame->ActivateState(&PC, &stack, &locals, &arguments);
			// If the method we just terminated is not of type void, we push the result to the 
			// stack of the calling method
			if ((_methods[methodIndex].methodFlags & METHOD_VOID) == 0)
			{
				stack->push(retVal);
			}
			continue;
		}
		
		instr = DecodeOpcode(&pCode[PC], &Len);
        if (instr == CEE_COUNT)
        {
			InvalidOpCode(LastPC);
            return false;
        }
		
		PC += Len;
		
		uint32_t intermediate;
		
		byte opCodeType = pgm_read_byte(OpcodeInfo + instr);
		
		if (!stack->empty())
		{
			Firmata.sendString(F("Top of Stack: "), stack->peek());
		}
            
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
						// Remove current method from execution stack
						ExecutionState* frame = state;
						if (frame == currentFrame)
						{
							// We're at the outermost frame
							return true;
						}
						
						// Find the frame which has the current frame as next (should be the second-last on the stack now)
						while (frame->_next != currentFrame)
						{
							frame = frame->_next;
						}
						// Remove the last frame and set the PC for the new current frame
						frame->_next = NULL;
						int methodIndex = currentFrame->MethodIndex();
						delete currentFrame;
						currentFrame = frame;
						currentFrame->ActivateState(&PC, &stack, &locals, &arguments);
						// If the method we just terminated is not of type void, we push the result to the 
						// stack of the calling method
						if ((_methods[methodIndex].methodFlags & METHOD_VOID) == 0)
						{
							stack->push(*returnValue);
						}
					}
					break;
                    case CEE_THROW:
						InvalidOpCode(LastPC);
                        return false;
					case CEE_NOP:
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
					case CEE_CEQ:
						stack->push(stack->pop() == stack->pop());
						break;
					case CEE_CGT:
						stack->push(stack->pop() > stack->pop());
						break;
					case CEE_NOT:
						stack->push(~stack->pop());
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
                    default:
						InvalidOpCode(LastPC);
                        return false;
                }
				break;
            }
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
						InvalidOpCode(LastPC);
						return false;
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
				if (instr != CEE_CALLVIRT) // TODO: Also support CALL, should be the same for most of what we do
				{
					InvalidOpCode(PC);
					return false;
				}
				
				uint32_t tk = ((uint32_t)pCode[PC]) | ((uint32_t)pCode[PC+1] << 8) | ((uint32_t)pCode[PC+2] << 16) | ((uint32_t)pCode[PC+3] << 24);
                PC += 4;

                int method = ResolveToken(tk);
				
				if (method == -1)
				{
					return false;
				}
				
				int stackSize = _methods[method].maxLocals;
				if (_methods[method].methodFlags & METHOD_SPECIAL)
				{
					stackSize = 0;
				}
				
				int argumentCount = _methods[method].numArgs;
				ExecutionState* newState = new ExecutionState(method, stackSize, argumentCount);
				currentFrame->_next = newState;
				
				ObjectStack* oldStack = stack;
				// Start of the called method
				currentFrame = newState;
				currentFrame->ActivateState(&PC, &stack, &locals, &arguments);
				
				// Provide arguments to the new method
				while (argumentCount > 0)
				{
					arguments[--argumentCount] = oldStack->pop();
				}
				Firmata.sendStringf(F("Pushed stack to method %d"), 2, method);
				break;
            }
			default:
				InvalidOpCode(LastPC);
				return false;
        }
	}
	
	// Should never actually reach the end of the code stream - the compiler always inserts a ret statement.
	InvalidOpCode(-1);
	return false;
}

int FirmataIlExecutor::ResolveToken(uint32_t token)
{
	// uint32_t tkType = TypeFromToken(tk);

    for (int i = 0; i < MAX_METHODS; i++)
	{
		if (_methods[i].methodToken == token)
		{
			Firmata.sendString(F("Resolved method token for method: "), i);
			Firmata.sendString(F("Method flags: "), _methods[i].methodFlags);
			return i;
		}
	}
	
	Firmata.sendString(F("Unresolved method token: "), token);
	return -1;
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
	for (int i = 0; i < MAX_METHODS; i++)
	{
		if (_methods[i].methodIl != NULL)
		{
			free(_methods[i].methodIl);
			_methods[i].methodIl = NULL;
		}
	}
}

