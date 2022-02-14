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
#include "ObjectVector.h"
#include "ObjectStack.h"
#include "Encoder7Bit.h"
#include "SelfTest.h"
#include "HardwareAccess.h"
#include "DependentHandle.h"
#include "MemoryManagement.h"
#include "FlashMemoryManager.h"
#include "Esp32FatSupport.h"
#include <stdint.h>
#include <cwchar>
#include <new>
#include <math.h>
#include <wctype.h>
#include "Exceptions.h"
#include "CustomClrException.h"
#include "FreeMemory.h"
#include "OverflowMath.h"
#include "FirmataStatusLed.h"
#include "RuntimeState.h"
#include "DebuggerCommand.h"
#include "StandardErrorCodes.h"
#include "ArduinoDueSupport.h"

typedef byte BYTE;

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
#define PopI8 1
#define PopR4 1
#define PopR8 1
const byte OpcodePops[] PROGMEM =
{
#define OPDEF(c,s,pop,push,type,args,l,s1,s2,ctrl) pop,
#include "opcode.def.h"
#undef OPDEF
};

OPCODE DecodeOpcode(const BYTE *pCode, uint16_t *pdwLen);

boolean FirmataIlExecutor::handlePinMode(byte pin, int mode)
{
  // This class does not handle individual pin modes
  return false;
}

FirmataIlExecutor::FirmataIlExecutor()
	: _nextStepBehavior()
{
	_methodCurrentlyExecuting = nullptr;
	_stringHeapRam = nullptr;
	_stringHeapRamSize = 0;
	_stringHeapFlash = nullptr;
	_instructionsExecuted = 0;
	_startupToken = 0;
	_startupFlags = 0;
	_startedFromFlash = false;
	_taskStartTime = millis();
	_specialTypeListFlash = nullptr;
	_specialTypeListRam = nullptr;
	_specialTypeListRamLength = 0;
	_flashMemoryManager = nullptr;
	_debugBreakActive = false;
	_debuggerEnabled = false;
	_commandsToSkip = 0;
	_lastError = 0;
	_breakOnException = false;
}

bool FirmataIlExecutor::AutoStartProgram()
{
	if (_startupToken != 0)
	{
		if (!_flashMemoryManager->ValidateFlashContents())
		{
			Firmata.sendString(F("Flash memory contents are inconsistent or damaged."));
			_startedFromFlash = false;
			_startupToken = 0;
			FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_ERROR, 5000);
		}

		// Auto-Load the program
		MethodBody* method = GetMethodByToken(_startupToken);
		if (method == nullptr)
		{
			Firmata.sendString(F("Startup method not found"));
			FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_ERROR, 5000);
			return false;
		}

		SetMemoryExecutionMode(true);
		// There's a problem: Because we're writing full objects to flash (including the vtable pointer), updating the firmware almost always
		// invalidates the program, causing this line to cause a core dump, because the virtual method calls fail.
		// We should somehow validate that the program matches the current firmware.
		ExecutionState* rootState = new ExecutionState(0, method->MaxExecutionStack(), method);
		if (rootState == nullptr)
		{
			Firmata.sendString(F("Out of memory starting task"));
			FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_ERROR, 5000);
			return false;
		}
		
		_instructionsExecuted = 0;
		_taskStartTime = millis();

		_methodCurrentlyExecuting = rootState;
	}
	
	return true;
}

void FirmataIlExecutor::Init()
{
	if (_flashMemoryManager == nullptr)
	{
		_flashMemoryManager = new FlashMemoryManager();
	}
	
	// This method is expected to be called at least once on startup, but not necessarily after a connection
	SelfTest test;
	test.PerformSelfTest();

	if (_lowLevelLibraries.empty())
	{
		HardwareAccess* access = new HardwareAccess();
		access->Init();
		_lowLevelLibraries.push_back(access);
		DependentHandle* dp = new DependentHandle();
		dp->Init();
		_lowLevelLibraries.push_back(dp);
#ifndef ARDUINO_DUE
		Esp32FatSupport* fat = new Esp32FatSupport();
		fat->Init();
		_lowLevelLibraries.push_back(fat);
#endif
	}

	_gc.Init(this);

	void* classes, *methods, *constants, *stringHeap, *clauses;
	int* specialTokens;
	_flashMemoryManager->Init(classes, methods, constants, stringHeap, specialTokens, clauses, _startupToken, _startupFlags, _staticVectorMemorySize);
	_classes.ReadListFromFlash(classes);
	_methods.ReadListFromFlash(methods);
	_constants.ReadListFromFlash(constants);
	_clauses.ReadListFromFlash(clauses);
	_stringHeapFlash = (byte*)stringHeap;
	_specialTypeListFlash = specialTokens;
	if (_startupToken != 0)
	{
		_startedFromFlash = true;
	}
	AutoStartProgram();
}

void FirmataIlExecutor::handleCapability(byte pin)
{
#if SIM
	if (pin == 1)
	{
		// In simulation, re-read the flash after a reset, to emulate a board reset.
		void* classes, * methods, * constants, * stringHeap, * clauses;
		int* specialTokenList;
		_flashMemoryManager->Init(classes, methods, constants, stringHeap, specialTokenList, clauses, _startupToken, _startupFlags, _staticVectorMemorySize);
		_classes.ReadListFromFlash(classes);
		_methods.ReadListFromFlash(methods);
		_constants.ReadListFromFlash(constants);
		_clauses.ReadListFromFlash(clauses);
		_stringHeapFlash = (byte*)stringHeap;
		_specialTypeListFlash = specialTokenList;
	}
#endif
}

// See https://stackoverflow.com/questions/18534494/convert-from-utf-8-to-unicode-c
uint16_t utf8_to_unicode(const char*& coded)
{
	int charcode = 0;
	int t = (*coded) & 0xFF; // No sign-extension here
	coded++;
	if (t < 128)
	{
		return (uint16_t)t;
	}
	int high_bit_mask = (1 << 6) - 1;
	int high_bit_shift = 0;
	int total_bits = 0;
	const int other_bits = 6;
	while ((t & 0xC0) == 0xC0)
	{
		t <<= 1;
		t &= 0xff;
		total_bits += 6;
		high_bit_mask >>= 1;
		high_bit_shift++;
		charcode <<= other_bits;
		charcode |= (*coded & 0xFF) & ((1 << other_bits) - 1);
		coded++;
	}
	charcode |= ((t >> high_bit_shift) & high_bit_mask) << total_bits;
	if (charcode > 0xFFFF)
	{
		charcode = 0x3F; // The Question Mark
	}
	return (uint16_t)charcode;
}

int unicode_to_utf8(int charcode, char*& output)
{
	if (charcode < 128)
	{
		*output = (char)charcode;
		output++;
		return 1;
	}
	else
	{
		*output = 0;
		int first_bits = 6;
		const int other_bits = 6;
		int first_val = 0xC0;
		int t = 0;
		int bytesUsed = 0;
		while (charcode >= (1 << first_bits))
		{
			t = 128 | (charcode & ((1 << other_bits) - 1));
			charcode >>= other_bits;
			first_val |= 1 << (first_bits);
			first_bits--;
			// Shift existing output to the right
			memmove(output + 1, output, bytesUsed);
			*output = (byte)t;

			bytesUsed++;
		}
		t = first_val | charcode;
		
		memmove(output + 1, output, bytesUsed);
		*output = (char)t;
		bytesUsed++;
		output += bytesUsed;
		return bytesUsed;
	}
}


void FirmataIlExecutor::SendQueryHardwareReply()
{
	ExecutorCommand subCommand = ExecutorCommand::QueryHardware;
	SendReplyHeader(subCommand);
	Firmata.sendPackedUInt14(0); // Some flags
	Firmata.write(sizeof(int));
	Firmata.write(sizeof(void*));
	Firmata.sendPackedUInt32(_flashMemoryManager->TotalFlashMemory());
	Firmata.sendPackedUInt32(_flashMemoryManager->UsedFlashMemory());
	Firmata.sendPackedUInt32(freeMemory());
	Firmata.sendPackedUInt32(MAX_DATA_BYTES);  // Maximum size of one command sequence
	Firmata.endSysex();
}

boolean FirmataIlExecutor::handleSysex(byte command, byte argc, byte* argv)
{
	ExecutorCommand subCommand = ExecutorCommand::None;
	if (command == SCHEDULER_DATA)
	{
		if (argc < 4)
		{
			Firmata.sendString(F("Error in Scheduler command: Not enough parameters"));
			return false;
		}
		if (argv[0] != 0x7F)
		{
			// Scheduler message type must be 0x7F, specific meaning follows
			Firmata.sendString(F("Error in Scheduler command: Unknown command syntax"));
			return false;
		}

		byte sequenceNo = argv[1];
		subCommand = (ExecutorCommand)argv[2];

		// Correct for the late-added sequence in the header. So we don't need to adjust all offsets below
		argv++;
		argc--;

		// TRACE(Firmata.sendString(F("Handling client command "), (int)subCommand));
		if (IsExecutingCode() && subCommand != ExecutorCommand::ResetExecutor && subCommand != ExecutorCommand::KillTask && subCommand != ExecutorCommand::DebuggerCommand)
		{
			Firmata.sendStringf(F("Execution engine busy. Ignoring command %d."), subCommand);
			SendAckOrNack(subCommand, sequenceNo, ExecutionError::EngineBusy);
			return true;
		}

		try
		{
			switch (subCommand)
			{
			case ExecutorCommand::QueryHardware:
				{
				SendQueryHardwareReply();
				return true;
				}
			case ExecutorCommand::LoadIl:
				FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_LOADING_PROGRAM, 200);
				if (argc < 8)
				{
					Firmata.sendString(F("Not enough IL data parameters"));
					SendAckOrNack(subCommand, sequenceNo, ExecutionError::InvalidArguments);
					return true;
				}
				// 14-bit values transmitted for length and offset
				SendAckOrNack(subCommand, sequenceNo, LoadIlDataStream(DecodePackedUint32(argv + 2), DecodePackedUint14(argv + 7), DecodePackedUint14(argv + 9), argc - 11, argv + 11));
				break;
			case ExecutorCommand::StartTask:
			{
				ExecutionError error = DecodeParametersAndExecute(DecodePackedUint32(argv + 2), DecodePackedUint14(argv + 2 + 5), argc - (5 + 2 + 2), argv + 5 + 2 + 2);
				SendAckOrNack(subCommand, sequenceNo, error);
			}
				break;
			case ExecutorCommand::DeclareMethod:
				FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_LOADING_PROGRAM, 200);
				if (argc < 6)
				{
					Firmata.sendString(F("Not enough IL data parameters"));
					SendAckOrNack(subCommand, sequenceNo, ExecutionError::InvalidArguments);
					return true;
				}
				SendAckOrNack(subCommand, sequenceNo, LoadIlDeclaration(DecodePackedUint32(argv + 2), argv[7], argv[8], argv[9],
				                                                        (NativeMethod)DecodePackedUint32(argv + 10)));
				break;
			case ExecutorCommand::MethodSignature:
				FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_LOADING_PROGRAM, 200);
				if (argc < 4)
				{
					Firmata.sendString(F("Not enough IL data parameters"));
					SendAckOrNack(subCommand, sequenceNo, ExecutionError::InvalidArguments);
					return true;
				}
				SendAckOrNack(subCommand, sequenceNo, LoadMethodSignature(DecodePackedUint32(argv + 2), argv[7], argc - 8, argv + 8));
				break;
			case ExecutorCommand::ExceptionClauses:
				FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_LOADING_PROGRAM, 200);
				if (argc < 7 * 5)
				{
					Firmata.sendString(F("Not enough IL data parameters"));
					SendAckOrNack(subCommand, sequenceNo, ExecutionError::InvalidArguments);
					return true;
				}
				SendAckOrNack(subCommand, sequenceNo, LoadExceptionClause(DecodePackedUint32(argv + 2), DecodePackedUint32(argv + 7),
				                                                          DecodePackedUint32(argv + 12), DecodePackedUint32(argv + 17), DecodePackedUint32(argv + 22), DecodePackedUint32(argv + 27), DecodePackedUint32(argv + 32)));
				break;
			case ExecutorCommand::ClassDeclarationEnd:
			case ExecutorCommand::ClassDeclaration:
			{
				FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_LOADING_PROGRAM, 200);
				if (argc < 19)
				{
					Firmata.sendString(F("Not enough IL data parameters"));
					SendAckOrNack(subCommand, sequenceNo, ExecutionError::InvalidArguments);
				}

				bool isLastPart = subCommand == ExecutorCommand::ClassDeclarationEnd;
				SendAckOrNack(subCommand, sequenceNo, LoadClassSignature(isLastPart,
				                                                         DecodePackedUint32(argv + 2),
				                                                         DecodePackedUint32(argv + 2 + 5), DecodePackedUint14(argv + 2 + 5 + 5), DecodePackedUint14(argv + 2 + 5 + 5 + 2) << 2,
				                                                         DecodePackedUint14(argv + 2 + 5 + 5 + 2 + 2), DecodePackedUint14(argv + 2 + 5 + 5 + 2 + 2 + 2), argc - 20, argv + 20));
			}
				break;
			case ExecutorCommand::Interfaces:
				FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_LOADING_PROGRAM, 200);
				if (argc < 6)
				{
					Firmata.sendString(F("Not enough parameters"));
					SendAckOrNack(subCommand, sequenceNo, ExecutionError::InvalidArguments);
				}
				SendAckOrNack(subCommand, sequenceNo, LoadInterfaces(DecodePackedUint32(argv + 2), argc - 2, argv + 2));
				break;
			case ExecutorCommand::SpecialTokenList:
				FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_LOADING_PROGRAM, 200);
				SendAckOrNack(subCommand, sequenceNo, LoadSpecialTokens(DecodePackedUint32(argv + 2), DecodePackedUint32(argv + 2 + 5), argc - 12, argv + 12));
				break;
			case ExecutorCommand::SetConstantMemorySize:
				FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_LOADING_PROGRAM, 200);
				SendAckOrNack(subCommand, sequenceNo, PrepareStringLoad(DecodePackedUint32(argv + 2), DecodePackedUint32(argv + 2 + 5)));
				break;
			case ExecutorCommand::ConstantData:
				FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_LOADING_PROGRAM, 200);
				SendAckOrNack(subCommand, sequenceNo, LoadConstant(subCommand, DecodePackedUint32(argv + 2), DecodePackedUint32(argv + 2 + 5),
				                                                   DecodePackedUint32(argv + 2 + 5 + 5), argc - 17, argv + 17));
				break;
			case ExecutorCommand::GlobalMetadata:
				SendAckOrNack(subCommand, sequenceNo, LoadGlobalMetadata(DecodePackedUint32(argv + 2 + 5)));
				break;
			case ExecutorCommand::EraseFlash:
				KillCurrentTask();
				_startupToken = 0;
				_startupFlags = 0;
				_flashMemoryManager->Clear();
				_classes.clear(true);
				_methods.clear(true);
				_constants.clear(true);
				_clauses.clear(true);
				_stringHeapFlash = nullptr;
				freeEx(_stringHeapRam);
				_stringHeapRamSize = 0;
				_startupToken = 0;
				
				_specialTypeListFlash = nullptr;
				freeEx(_specialTypeListRam);
				_specialTypeListRam = nullptr;
				_specialTypeListRamLength = 0;
				// Fall trough
				[[fallthrough]];
			case ExecutorCommand::ResetExecutor:
				if (argv[2] == 1)
				{
					KillCurrentTask();
					reset();
					SendAckOrNack(subCommand, sequenceNo, ExecutionError::None);
				}
				else
				{
					SendAckOrNack(subCommand, sequenceNo, ExecutionError::InvalidArguments);
				}
				break;
			case ExecutorCommand::KillTask:
			{
					// TODO: This currently ignores the argument (the task id).
				KillCurrentTask();
				SendAckOrNack(subCommand, sequenceNo, ExecutionError::None);
				break;
			}
			case ExecutorCommand::CopyToFlash:
				{
				FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_LOADING_PROGRAM, 500);
					// Copy all members currently in ram to flash
				_classes.CopyContentsToFlash(_flashMemoryManager);
				_methods.CopyContentsToFlash(_flashMemoryManager);
				_constants.CopyContentsToFlash(_flashMemoryManager);
				}
				SendAckOrNack(subCommand, sequenceNo, ExecutionError::None);
				break;

			case ExecutorCommand::WriteFlashHeader:
			{
				FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_LOADING_PROGRAM, 500);
				void* classesPtr = _classes.CopyListToFlash(_flashMemoryManager);
				void* methodsPtr = _methods.CopyListToFlash(_flashMemoryManager);
				void* constantPtr = _constants.CopyListToFlash(_flashMemoryManager);
				void* clausesPtr = _clauses.CopyListToFlash(_flashMemoryManager);
				void* stringPtr = CopyStringsToFlash();
				int* specialTokenListPtr = CopySpecialTokenListToFlash();
				_startupToken = DecodePackedUint32(argv + 2 + 10);
				_startupFlags = DecodePackedUint32(argv + 2 + 15);

				_classes.ValidateListOrder();
				_methods.ValidateListOrder();
				_constants.ValidateListOrder();
				_flashMemoryManager->WriteHeader(DecodePackedUint32(argv + 2), DecodePackedUint32(argv + 2 + 5), classesPtr, methodsPtr, constantPtr, stringPtr,
					specialTokenListPtr, clausesPtr, _startupToken, _startupFlags, _staticVectorMemorySize);

				// Reset this flag after programming, or we'll immediately start executing code if there was _any_ valid program in flash when the CPU started.
				_startedFromFlash = false;
				SendAckOrNack(subCommand, sequenceNo, ExecutionError::None);
				SendQueryHardwareReply();
				printMemoryStatistics();
			}
			break;

			case ExecutorCommand::CheckFlashVersion:
				{
				bool result = _flashMemoryManager->ContainsMatchingData(DecodePackedUint32(argv + 2), DecodePackedUint32(argv + 2 + 5));
				SendAckOrNack(subCommand, sequenceNo, result ? ExecutionError::None : ExecutionError::InvalidArguments);
				}
				break;
			case ExecutorCommand::DebuggerCommand:
				{
				uint32_t debuggerCommand = DecodePackedUint32(argv + 2);
				uint32_t debuggerArg1 = 0;
				uint32_t debuggerArg2 = 0;
					if (argc >= 17)
					{
						debuggerArg1 = DecodePackedUint32(argv + 2 + 5);
					}
					if (argc >= 22)
					{
						debuggerArg2 = DecodePackedUint32(argv + 2 + 10);
					}
				SendAckOrNack(subCommand, sequenceNo, ExecuteDebuggerCommand((DebuggerCommand)debuggerCommand, debuggerArg1, debuggerArg2));
				}
				break;
			default:
				// Unknown command
				SendAckOrNack(subCommand, sequenceNo, ExecutionError::InvalidArguments);
				break;

			} // End of switch
		}
		catch(ExecutionEngineException& ex)
		{
			Firmata.sendString(STRING_DATA, ex.Message());
			SendAckOrNack(subCommand, sequenceNo, ExecutionError::InternalError);
		}
		catch(OutOfMemoryException& ex)
		{
			Firmata.sendString(F("Out of memory loading data"));
			_gc.PrintStatistics();
			Firmata.sendString(STRING_DATA, ex.Message());
			
			reset();
			SendAckOrNack(subCommand, sequenceNo, ExecutionError::OutOfMemory);
		}
		catch(Exception& ex)
		{
			Firmata.sendString(STRING_DATA, ex.Message());
			SendAckOrNack(subCommand, sequenceNo, ExecutionError::InternalError);
		}
		return true;
	}
	return false;
}

ExecutionError FirmataIlExecutor::LoadGlobalMetadata(uint32_t staticVectorMemorySize)
{
	_staticVectorMemorySize = staticVectorMemorySize;
	return ExecutionError::None;
}

ExecutionError FirmataIlExecutor::LoadExceptionClause(int methodToken, int clauseType, int tryOffset, int tryLength, int handlerOffset, int handlerLength, int exceptionFilterToken)
{
	ExceptionClause* clause = new ExceptionClause(methodToken);
	clause->ClauseType = (ExceptionHandlingClauseOptions)clauseType;
	clause->TryOffset = (uint16_t)tryOffset;
	clause->TryLength = (uint16_t)tryLength;
	clause->HandlerOffset = (uint16_t)handlerOffset;
	clause->HandlerLength = (uint16_t)handlerLength;
	clause->FilterToken = exceptionFilterToken;
	_clauses.Insert(clause);
	return ExecutionError::None;
}

void* FirmataIlExecutor::CopyStringsToFlash()
{
	if (_stringHeapFlash != nullptr)
	{
		throw ExecutionEngineException("String flash heap already written");
	}
	
	// This is pretty straight-forward: We just copy the whole string heap to flash.
	// Since it internally only uses relative addresses, we don't have to care about anything else.
	byte* target = (byte*)_flashMemoryManager->FlashAlloc(_stringHeapRamSize);
	_flashMemoryManager->CopyToFlash(_stringHeapRam, target, _stringHeapRamSize);
	if (_stringHeapRam != nullptr)
	{
		freeEx(_stringHeapRam);
		_stringHeapRam = nullptr;
		_stringHeapRamSize = 0;
	}
	
	_stringHeapFlash = target;
	return target;
}

int* FirmataIlExecutor::CopySpecialTokenListToFlash()
{
	if (_specialTypeListFlash != nullptr)
	{
		throw ExecutionEngineException("Special Type list already written to flash");
	}

	// This is pretty straight-forward: We just copy the whole thing to flash
	int* target = (int*)_flashMemoryManager->FlashAlloc(_specialTypeListRamLength * sizeof(int));
	_flashMemoryManager->CopyToFlash(_specialTypeListRam, target, _specialTypeListRamLength * sizeof(int));
	if (_specialTypeListRam != nullptr)
	{
		freeEx(_specialTypeListRam);
		_specialTypeListRam = nullptr;
		_specialTypeListRamLength = 0;
	}

	_specialTypeListFlash = target;
	return target;
}


char* FirmataIlExecutor::GetString(int stringToken, int& length)
{
	byte* ret = GetString(_stringHeapRam, stringToken, length);
	if (ret == nullptr)
	{
		ret = GetString(_stringHeapFlash, stringToken, length);
	}

	if (ret == nullptr)
	{
		throw ClrException(SystemException::NotSupported, stringToken);
	}
	
	return reinterpret_cast<char*>(ret);
}

byte* FirmataIlExecutor::GetString(byte* heap, int stringToken, int& length)
{
	if (heap == nullptr)
	{
		return nullptr;
	}

	int* tokenPtr = (int*)heap;
	int token = *tokenPtr;
	length = stringToken & 0xFFFF;
	while (token != 0 && token != stringToken)
	{
		int len = token & 0xFFFF;
		tokenPtr = AddBytes(tokenPtr, len + 4); // Including the token itself
		token = *tokenPtr;
	}

	if (token == stringToken)
	{
		return (byte*)AddBytes(tokenPtr, 4);
	}

	return nullptr;
}

/// <summary>
/// Prepares the memory for loading constants and strings
/// </summary>
/// <param name="constantSize">Total size of all constants (currently unused)</param>
/// <param name="stringListSize">Total size of all strings</param>
/// <returns>Execution result</returns>
ExecutionError FirmataIlExecutor::PrepareStringLoad(uint32_t constantSize, uint32_t stringListSize)
{
	if (_stringHeapRam != nullptr)
	{
		freeEx(_stringHeapRam);
		_stringHeapRam = nullptr;
		_stringHeapRamSize = 0;
	}
	if (stringListSize > 0)
	{
		_stringHeapRamSize = stringListSize;
		_stringHeapRam = (byte*)mallocEx(_stringHeapRamSize);
		memset(_stringHeapRam, 0, _stringHeapRamSize);
		if (_stringHeapRam == nullptr)
		{
			return ExecutionError::OutOfMemory;
		}
	}
	return ExecutionError::None;
}

ExecutionError FirmataIlExecutor::LoadSpecialTokens(uint32_t totalListLength, uint32_t offset, byte argc, byte* argv)
{
	if (_specialTypeListRam == nullptr)
	{
		if (offset != 0)
		{
			return ExecutionError::InvalidArguments;
		}
		_specialTypeListRamLength = totalListLength + 1; // We need a terminating 0 element
		_specialTypeListRam = (int*)mallocEx(_specialTypeListRamLength * sizeof(int));
		memset(_specialTypeListRam, 0, _specialTypeListRamLength * sizeof(int));
	}

	if (offset + (argc / 5) > _specialTypeListRamLength)
	{
		return ExecutionError::InvalidArguments;
	}
	
	int bytesLeft = argc;
	while(bytesLeft >= 5)
	{
		_specialTypeListRam[offset] = DecodePackedUint32(argv);
		bytesLeft -= 5;
		argv += 5;
		offset++;
	}
	
	return ExecutionError::None;
}

ExecutionError FirmataIlExecutor::LoadConstant(ExecutorCommand executorCommand, uint32_t constantToken, uint32_t currentEntryLength, uint32_t offset, byte argc, byte* argv)
{
	if (constantToken >= 0x10000)
	{
		// A string element
		char* data;
		if (_stringHeapRam == nullptr)
		{
			// Must be preallocated
			return ExecutionError::InvalidArguments;
		}

		uint32_t newStringLen = constantToken & 0xFFFF;
		if (newStringLen != currentEntryLength)
		{
			return ExecutionError::InvalidArguments;
		}
		int* tokenPtr = (int*)_stringHeapRam;
		int token = *tokenPtr;
		while (token != 0 && (uint32_t)token != constantToken)
		{
			int len = token & 0xFFFF;
			tokenPtr = AddBytes(tokenPtr, len + 4); // Including the token itself
			token = *tokenPtr;
		}
		if (tokenPtr > (int*)AddBytes(_stringHeapRam, _stringHeapRamSize - currentEntryLength))
		{
			OutOfMemoryException::Throw("String Heap not large enough");
		}
		
		*tokenPtr = constantToken;
		int numToDecode = num7BitOutbytes(argc);
		data = (char*)AddBytes(tokenPtr, 4 + offset);
		Encoder7Bit.readBinary(numToDecode, argv, (byte*)data);

		// Verification
		int resultLen = 0;
		char* result = GetString(constantToken, resultLen);
		if (result != (char*)AddBytes(tokenPtr, 4))
		{
			throw ExecutionEngineException("The string that was just inserted is not there");
		}
		return ExecutionError::None;
	}

	int* data2;
	if (offset == 0)
	{
		int numToDecode = num7BitOutbytes(argc);
		ConstantEntry* data2 = (ConstantEntry*)mallocEx(currentEntryLength + 2 * sizeof(int));
		Encoder7Bit.readBinary(numToDecode, argv, (byte*)AddBytes(data2, 2 * sizeof(int)));
		data2->Token = constantToken;
		data2->Length = currentEntryLength;
		_constants.Insert(data2);
		return ExecutionError::None;
	}

	int numToDecode = num7BitOutbytes(argc);
	data2 = (int*)GetConstant(constantToken);
	if (data2 == nullptr)
	{
		return ExecutionError::InvalidArguments;
	}
	
	Encoder7Bit.readBinary(numToDecode, argv, (byte*)AddBytes(data2, offset));

	return ExecutionError::None;
}

/// <summary>
/// Returns the constant with the given token (without header). Returns null on failure
/// </summary>
byte* FirmataIlExecutor::GetConstant(int token)
{
	for (auto iterator = _constants.GetIterator(); iterator.Next();)
	{
		int* currentWithHeader = (int*)iterator.Current();
		if (*currentWithHeader == token)
		{
			return (byte*)AddBytes(currentWithHeader, 2 * sizeof(int));
		}
	}

	return nullptr;
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

byte* FirmataIlExecutor::AllocGcInstance(size_t bytes)
{
	byte* ret = _gc.Allocate(bytes);
	if (ret != nullptr)
	{
		memset(ret, 0, bytes);
	}

	return ret;
}

// Used if it is well known that a reference now runs out of scope
void FreeGcInstance(Variable& obj)
{
	if (obj.Object != nullptr)
	{
		freeEx(obj.Object);
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

	int topLevelMethod = _methodCurrentlyExecuting->TaskId();

	// Send a status report, to end any process waiting for this method to return.
	SendExecutionResult(topLevelMethod, _currentException, Variable(), MethodState::Killed);
	Firmata.sendString(F("Code execution aborted"));
	
	CleanStack(_methodCurrentlyExecuting);
	_methodCurrentlyExecuting = nullptr;
	// Make sure we stay stopped (hard-reset to restart from flash is possible, but typically the flash is going to be reprogrammed now)
	_startupFlags = 0;
	_startupToken = 0;
	_nextStepBehavior.Kind = BreakpointType::None;
	_breakpoints.clear(true);
	_debugBreakActive = false;
	_debuggerEnabled = false;
	_commandsToSkip = 0;
	_lastError = 0;
	_breakOnException = false;
	SetMemoryExecutionMode(false);
}

void FirmataIlExecutor::CleanStack(ExecutionState* state)
{
	while (state != nullptr)
	{
		auto previous = state;
		state = state->_next;
		delete previous;
	}
}

void FirmataIlExecutor::report(bool elapsed)
{
	// Keep track of timers or background jobs
	for (uint32_t i = 0; i < _lowLevelLibraries.size(); i++)
	{
		_lowLevelLibraries[i]->Update();
	}
	
	// Check that we have an existing execution context, and if so continue there.
	if (!IsExecutingCode())
	{
		if (_startupFlags & 1 && _startedFromFlash)
		{
			// If the startup flags bit 0 is set, restart, either trough hardware or trough software
			HardwareAccess::Reboot();
			reset();
			AutoStartProgram();
		}
		return;
	}

	FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_EXECUTING_PROGRAM, 100);
	Variable retVal;
	MethodState execResult = ExecuteIlCode(_methodCurrentlyExecuting, &retVal);

	if (execResult == MethodState::Running)
	{
		// The method is still running
		return;
	}

	SetMemoryExecutionMode(false);
	int methodindex = _methodCurrentlyExecuting->TaskId();
	SendExecutionResult(methodindex, _currentException, retVal, execResult);

	// The method ended
	CleanStack(_methodCurrentlyExecuting);
	_methodCurrentlyExecuting = nullptr;
}

ExecutionError FirmataIlExecutor::LoadIlDeclaration(int methodToken, int flags, byte maxStack, byte argCount,
	NativeMethod nativeMethod)
{
	TRACE(Firmata.sendStringf(F("Loading declaration for token %x, Flags 0x%x"), (int)methodToken, (int)flags));
	MethodBody* method = GetMethodByToken(methodToken);
	if (method != nullptr)
	{
		Firmata.sendString(F("Error: Method already defined"));
		return ExecutionError::InvalidArguments;
	}

	void* dataPtr = mallocEx(sizeof(MethodBodyDynamic));
	method = new(dataPtr) MethodBodyDynamic((byte) flags, (byte)argCount, (byte)maxStack); // placement new!
	if (method == nullptr)
	{
		return ExecutionError::OutOfMemory;
	}
	
	method->_nativeMethod = nativeMethod;
	method->methodToken = methodToken;
	
	// And attach to the list
	_methods.Insert(method);
	TRACE(Firmata.sendStringf(F("Loaded metadata for token 0x%lx, Flags 0x%x"), methodToken, (int)flags));
	return ExecutionError::None;
}

ExecutionError FirmataIlExecutor::LoadMethodSignature(int methodToken, byte signatureType, byte argc, byte* argv)
{
	TRACE(Firmata.sendString(F("Loading Declaration.")));
	MethodBodyDynamic* method = (MethodBodyDynamic*)GetMethodByToken(methodToken);
	if (method == nullptr)
	{
		// This operation is illegal if the method is unknown
		Firmata.sendString(F("LoadMethodSignature for unknown token"));
		return ExecutionError::InvalidArguments;
	}

	if (!method->IsDynamic())
	{
		Firmata.sendString(F("Cannot update flash methods"));
		return ExecutionError::InvalidArguments;
	}

	VariableDescription desc;
	int size;
	// There's a "length of list" byte we don't need
	argv++;
	argc--;
	if (signatureType == 0)
	{
		// Argument types. (This can be called multiple times for very long argument lists)
		for (byte i = 0; i < argc - 1;) // The last byte in the arglist is the END_SYSEX byte
		{
			desc.Type = (VariableKind)argv[i];
			size = argv[i + 1] | argv[i + 2] << 7;
			desc.Size = (uint16_t)(size << 2); // Size is given as multiples of 4 (so we can again gain the full 16 bit with only 2 7-bit values)
			method->AddArgumentDescription(desc);
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
			desc.Size = (uint16_t)(size << 2); // Size is given as multiples of 4 (so we can again gain the full 16 bit with only 2 7-bit values)
			method->AddLocalDescription(desc);
			i += 3;
		}
	}
	else
	{
		return ExecutionError::InvalidArguments;
	}
	
	return ExecutionError::None;
}

ExecutionError FirmataIlExecutor::LoadIlDataStream(int methodToken, uint16_t codeLength, uint16_t offset, byte argc, byte* argv)
{
	// TRACE(Firmata.sendStringf(F("Going to load IL Data for method %d, total length %d offset %x"), codeReference, codeLength, offset));
	MethodBody* method = GetMethodByToken(methodToken);
	if (method == nullptr)
	{
		// This operation is illegal if the method is unknown
		Firmata.sendString(F("LoadIlDataStream for unknown token 0x"), methodToken);
		return ExecutionError::InvalidArguments;
	}

	if (offset == 0)
	{
		if (method->_methodIl != nullptr)
		{
			freeEx(method->_methodIl);
			method->_methodIl = nullptr;
		}
		byte* decodedIl = (byte*)mallocEx(codeLength);
		if (decodedIl == nullptr)
		{
			Firmata.sendString(F("Not enough memory. "), codeLength);
			return ExecutionError::OutOfMemory;
		}

		int numToDecode = num7BitOutbytes(argc);
		Encoder7Bit.readBinary(numToDecode, argv, decodedIl);
		method->_methodLength = codeLength;
		method->_methodIl = decodedIl;
	}
	else 
	{
		byte* decodedIl = method->_methodIl + offset;
		int numToDecode = num7BitOutbytes(argc);
		Encoder7Bit.readBinary(numToDecode, argv, decodedIl);
	}

	TRACE(Firmata.sendStringf(F("Loaded IL Data for method %d, offset %x"), methodToken, offset));
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

void FirmataIlExecutor::SendExecutionResult(int32_t codeReference, RuntimeException& ex, Variable returnValue, MethodState execResult)
{
	// Reply format:
	// bytes 0-1: Reference of method that exited
	// byte 2: Status. See below
	// byte 3: Number of integer values returned
	// bytes 4+: Return values
	// Firmata.sendStringf(F("Task %d is ending."), codeReference);
	Firmata.startSysex();
	Firmata.write(SCHEDULER_DATA);
	Firmata.write((byte)ExecutorCommand::Reply);
	Firmata.write((byte)RuntimeState::TaskTermination);
	Firmata.write(codeReference & 0x7F);
	Firmata.write((codeReference >> 7) & 0x7F);
	
	// 0: Code execution completed, called method ended
	// 1: Code execution aborted due to exception (i.e. unsupported opcode, method not found)
	// 2: Intermediate data from method (not used here)
	Firmata.write((byte)execResult);
	if (ex.ExceptionType != SystemException::None && execResult == MethodState::Aborted)
	{
		Firmata.write((byte)(1 + 1 /* ExceptionArg*/ + 2 * RuntimeException::MaxStackTokens + 1)); // Number of arguments that follow
		if (ex.ExceptionType == SystemException::None)
		{
			SendPackedUInt32((uint32_t)SystemException::CustomException);
		}
		else
		{
			SendPackedUInt32((uint32_t)ex.ExceptionType);
		}

		SendPackedUInt32(ex.TokenOfException);
		
		SendPackedUInt32(0); // A dummy marker
		for (int i = 0; i < RuntimeException::MaxStackTokens; i++)
		{
			SendPackedUInt32(ex.StackTokens[i]);
			SendPackedUInt32(ex.PerStackPc[i]);
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

	uint32_t taskEnd = millis();
	int32_t delta = taskEnd - _taskStartTime;

	// Overflow?
	if (taskEnd < _taskStartTime)
	{
		delta = (0x7FFFFFFF - _taskStartTime) + taskEnd;
	}
	
	if (delta > 2000)
	{
		int32_t troughput = _instructionsExecuted / (delta / 1000);
		Firmata.sendStringf(F("Executed %d instructions in %dms (%d instructions/second)"), _instructionsExecuted, delta, troughput);
	}
}

void FirmataIlExecutor::SendPackedUInt32(uint32_t value)
{
	Firmata.sendPackedUInt32(value);
}

void FirmataIlExecutor::SendPackedUInt64(uint64_t value)
{
	Firmata.sendPackedUInt64(value);
}


ExecutionError FirmataIlExecutor::DecodeParametersAndExecute(int methodToken, int taskId, byte argc, byte* argv)
{
	Variable result;
	MethodBody* method = GetMethodByToken(methodToken);
	if (method == nullptr)
	{
		return ExecutionError::InvalidArguments;
	}

	SetMemoryExecutionMode(true);

	TRACE(Firmata.sendStringf(F("Code execution for %d starts. Stack Size is %d."), methodToken, method->MaxExecutionStack()));
	ExecutionState* rootState = new ExecutionState(taskId, method->MaxExecutionStack(), method);
	if (rootState == nullptr)
	{
		OutOfMemoryException::Throw("Out of memory starting task");
	}
	_instructionsExecuted = 0;
	_taskStartTime = millis();
	
	_methodCurrentlyExecuting = rootState;
	int idx = 0;
	for (int i = 0; i < method->NumberOfArguments(); i++)
	{
		VariableDescription& desc = method->GetArgumentAt(i);
		VariableKind k = desc.Type;
		if (k == VariableKind::Int64 || k == VariableKind::Uint64 || k == VariableKind::Double)
		{
			uint64_t combined = DecodeUint32(argv + idx);
			combined += static_cast<uint64_t>(DecodeUint32(argv + idx + 8)) << 32;
			rootState->SetArgumentValue(i, combined, k);
			idx += 16;
		}
		else if (k == VariableKind::ReferenceArray)
		{
			// We can't normally call methods with object/array parameters, because we don't currently support serializing them.
			// With one exception: The main method may have a string[] array as argument,
			// so create a new, empty string array.
			idx += 8; // value is ignored
			Variable arr;
			AllocateArrayInstance((int)KnownTypeTokens::String, 0, arr);
			rootState->SetArgumentValue(i, arr);
		}
		else
		{
			rootState->SetArgumentValue(i, DecodeUint32(argv + idx), k);
			idx += 8;
		}
	}

	return ExecutionError::None;

	// Don't actually start the execution, so that the start request is immediately returning, and we don't have to separately handle
	// immediate errors
	/* MethodState execResult = ExecuteIlCode(rootState, &result);
	if (execResult == MethodState::Running)
	{
		// The method is still running
		return ExecutionError::None;
	}

	SendExecutionResult(taskId, _currentException, result, execResult);
	
	// The method ended very quickly
	CleanStack(_methodCurrentlyExecuting);
	_methodCurrentlyExecuting = nullptr;

	return ExecutionError::None;
	*/
}

void FirmataIlExecutor::InvalidOpCode(uint16_t pc, OPCODE opCode)
{
	Firmata.sendStringf(F("Invalid/Unsupported opcode 0x%x at method offset 0x%x"), opCode, pc);
	
	throw ClrException("Invalid opcode", SystemException::InvalidOpCode, opCode);
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
	SetField4(cls, type, result, 0);
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

ClassDeclaration* FirmataIlExecutor::GetTypeFromTypeInstance(Variable& ownTypeInstance)
{
	ClassDeclaration* typeClassDeclaration = GetClassDeclaration(ownTypeInstance);
	Variable ownToken = GetField(typeClassDeclaration, ownTypeInstance, 0);
	ClassDeclaration* t1 = _classes.GetClassWithToken(ownToken.Int32);
	return t1;
}

bool FirmataIlExecutor::StringEquals(const VariableVector& args)
{
	return StringEquals(args, 0);
}

bool FirmataIlExecutor::StringEquals(const VariableVector& args, int stringComparison)
{
	ASSERT(args.size() >= 2);
	Variable& a = args[0];
	Variable& b = args[1];
	if (a.Object == b.Object)
	{
		return true;
	}
	if (a.Object == nullptr || b.Object == nullptr)
	{
		return true;
	}

	ClassDeclaration* ty1 = GetClassDeclaration(a);
	ClassDeclaration* ty2 = GetClassDeclaration(b);
	ASSERT(ty1->ClassToken == (int)KnownTypeTokens::String);
	ASSERT(ty2->ClassToken == (int)KnownTypeTokens::String);
	int len1 = *AddBytes((int*)a.Object, 4);
	int len2 = *AddBytes((int*)b.Object, 4);
	if (len1 != len2)
	{
		return false;
	}

	// The even members of StringComparison are CurrentCulture, InvariantCulture and Ordinal ->
	// We handle them all as ordinal
	if (stringComparison % 2 == 0)
	{
		// This function seems to be broken on the Arduino DUE, therefore we implement it manually.
		// (The possibly missing terminating 0 cannot be the issue, since it works in simulation)
		// int cmp = wcscmp((wchar_t*)AddBytes(a.Object, STRING_DATA_START), (wchar_t*)AddBytes(b.Object, STRING_DATA_START));
		
		int cmp = memcmp(AddBytes(a.Object, STRING_DATA_START), AddBytes(b.Object, STRING_DATA_START), len1 * 2);
		return cmp == 0;
	}
	else
	{
		// The odd members use IgnoreCase. For the invariant culture.
		wchar_t* left = (wchar_t*)AddBytes(a.Object, STRING_DATA_START);
		wchar_t* right = (wchar_t*)AddBytes(b.Object, STRING_DATA_START);
		for (int i = 0; i < len1; i++)
		{
			if (towlower(*left) != towlower(*right))
			{
				return false;
			}
			left += 1;
			right += 1;
		}
		return true;
	}
}

/// <summary>
/// Returns the given C# string as C UTF8 string (with trailing zero)
/// The caller is responsible for freeing the memory.
/// </summary>
/// <param name="string">An object of type System::String</param>
char* FirmataIlExecutor::GetAsUtf8String(Variable& string)
{
	wchar_t* input = (wchar_t*)string.Object;
	if (input == nullptr)
	{
		// Also allocate a dummy element for an empty string - simplifies handling
		char* empty = (char*)malloc(4);
		empty[0] = 0;
		return empty;
	}

	int length = *AddBytes(input, 4);
	return GetAsUtf8String(AddBytes(input, STRING_DATA_START), length);
}

char* FirmataIlExecutor::GetAsUtf8String(const wchar_t* stringData, int length)
{
	uint16_t* input = (uint16_t*)stringData;
	if (input == nullptr)
	{
		// Also allocate a dummy element for an empty string - simplifies handling
		char* empty = (char*)malloc(4);
		empty[0] = 0;
		return empty;
	}
	
	int outLength = 0;
	char* outbuf = (char*)malloc(length * 4 + 2);
	char* outStart = outbuf;
	for(int i = 0; i < length; i++)
	{
		uint16_t charToEncode = *input;
		input++;
		outLength += unicode_to_utf8(charToEncode, outbuf);
	}

	*outbuf = 0;
	return outStart;
}

// Executes the given OS function. Note that args[0] is the this pointer for instance methods
void FirmataIlExecutor::ExecuteSpecialMethod(ExecutionState* currentFrame, NativeMethod method, const VariableVector& args, Variable& result)
{
	TRACE(Firmata.sendStringf(F("Executing special method 0x%x"), (int32_t)method));
	for (uint32_t i = 0; i < _lowLevelLibraries.size(); i++)
	{
		if (_lowLevelLibraries[i]->ExecuteHardwareAccess(this, currentFrame, method, args, result))
		{
			return;
		}
	}

	// Note: All methods should do some minimal parameter validation, at least the count.
	// This lets us more quickly detect inconsistent builds (unsychronized enum values)
	switch (method)
	{
	case NativeMethod::TypeEquals:
		ASSERT(args.size() == 2);
		{
			// This implements System::Type::Equals(object)
			result.Type = VariableKind::Boolean;
			Variable& type1 = args[0]; // type1.
			Variable& type2 = args[1]; // type2.
			if (type1.Object == type2.Object)
			{
				result.Boolean = true;
				break;
			}

			if (type1.Object == nullptr || type2.Object == nullptr)
			{
				// Due to the above, they cannot be equal if one is null
				result.Boolean = false;
				break;
			}
			ClassDeclaration* ty = _classes.GetClassWithToken(2);
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
			ClassDeclaration* ty = _classes.GetClassWithToken(*(data + 2));
			int32_t size = *(data + 1);
			byte* targetPtr = (byte*)(data + ARRAY_DATA_START/4);
			memcpy(targetPtr, field.Object, size * ty->ClassDynamicSize);
		}
		break;
	case NativeMethod::RuntimeHelpersIsReferenceOrContainsReferencesCore:
		{
			ASSERT(args.size() == 1);
			Variable ownTypeInstance = args[0]; // A type instance
			ClassDeclaration* typeClassDeclaration = GetClassDeclaration(ownTypeInstance);
			Variable ownToken = GetField(typeClassDeclaration, ownTypeInstance, 0);
			ClassDeclaration* ty = _classes.GetClassWithToken(ownToken.Int32);
			
			// This is a shortcut for now. This should test whether the given type is a reference type or a value type with embedded references.
			result.Type = VariableKind::Boolean;
			if (ty->IsValueType())
			{
				result.Boolean = false;
			}
			else
			{
				result.Boolean = true;
			}
		}
		break;
	case NativeMethod::RuntimeHelpersIsBitwiseEquatable:
		// I think our implementation allows this to work for all types
		result.Type = VariableKind::Boolean;
		result.Boolean = true;
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
			if (ty->IsValueType())
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
			Variable& arguments = args[1]; // An array of types
			ASSERT(arguments.Type == VariableKind::ReferenceArray);
			uint32_t* data = (uint32_t*)arguments.Object;
			int32_t size = *(data + 1);
			ClassDeclaration* typeOfType = _classes.GetClassWithToken(2);
			uint32_t parameter = *(data + ARRAY_DATA_START/4); // First element of array
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

			if ((argumentType.Int32 & GENERIC_TOKEN_MASK) != 0 || size > 1)
			{
				// The argument is not a trivial type -> we have to search the extended type list to find the correct combined token.
				int newToken = ReverseSearchSpecialTypeList(genericToken, true, arguments.Object);
				if (newToken == 0)
				{
					// Still 0? That's bad.
					throw ClrException(SystemException::ClassNotFound, genericToken); // Error here might be missleading, but we do not know the token that we can't construct. That's just the point.
				}

				type.Int32 = newToken;
			}
			else
			{
				// The sum of a generic type and its only type argument will yield the token for the combination
				type.Int32 = genericToken + argumentType.Int32;
			}
			type.Type = VariableKind::RuntimeTypeHandle;
			GetTypeFromHandle(currentFrame, result, type);
			// result is returning the newly constructed type instance
		}
		break;
	case NativeMethod::ObjectGetType:
		{
		ASSERT(args.size() == 1); // The this pointer
		ClassDeclaration* cls = GetClassDeclaration(args[0]);
			Variable type;
			type.Int32 = cls->ClassToken;
			type.Type = VariableKind::RuntimeTypeHandle;
			GetTypeFromHandle(currentFrame, result, type);
		}
		break;
	case NativeMethod::TypeCreateInstanceForAnotherGenericParameter:
		{
			// The definition of this (private) function isn't 100% clear, but
			// "CreateInstanceForAnotherGenericType(typeof(List<int>), typeof(bool))" should return an instance(!) of List<bool>.
			Variable type1 = args[0]; // type1. An instantiated generic type
			Variable type2 = args[1]; // type2. a type parameter
			ClassDeclaration* ty = _classes.GetClassWithToken(2);
			Variable tok1 = GetField(ty, type1, 0);
			Variable tok2 = GetField(ty, type2, 0);
			int token = tok1.Int32;
			token = token & GENERIC_TOKEN_MASK;
			int searchArray[4];
			// Create a temporary array instance on the stack
			searchArray[0] = 9; // Array
			searchArray[1] = 1; // Length
			searchArray[2] = 20; // Int
			searchArray[3] = tok2.Int32;
			if (tok2.Int32 & GENERIC_TOKEN_MASK)
			{
				// The new token is a special token
				token = ReverseSearchSpecialTypeList(token, false, searchArray);
				if (token == 0)
				{
					throw ClrException(SystemException::ClassNotFound, tok2.Int32);
				}
			}
			else
			{
				token = token + tok2.Int32; // Default case
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
			Variable ownToken = GetField(typeClassDeclaration, ownTypeInstance, 0);
			Variable otherToken = GetField(typeClassDeclaration, otherTypeInstance, 0);
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
			
			ClassDeclaration* t1 = _classes.GetClassWithToken(ownToken.Int32);
			
			ClassDeclaration* t2 = _classes.GetClassWithToken(otherToken.Int32);
			
			ClassDeclaration* parent = _classes.GetClassWithToken(t1->ParentToken, false);
			while (parent != nullptr)
			{
				if (parent->ClassToken == t2->ClassToken)
				{
					result.Boolean = true;
					break;
				}

				parent = _classes.GetClassWithToken(parent->ParentToken, false);
			}

			if (t1->ImplementsInterface(t2->ClassToken))
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
		Variable ownToken = GetField(typeClassDeclaration, ownTypeInstance, 0);
		Variable otherToken = GetField(typeClassDeclaration, otherTypeInstance, 0);
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

		ClassDeclaration* t1 = _classes.GetClassWithToken(ownToken.Int32);

		ClassDeclaration* t2 = _classes.GetClassWithToken(otherToken.Int32);

		// Am I a base class of the other?
		ClassDeclaration* parent = _classes.GetClassWithToken(t2->ParentToken, false);
		while (parent != nullptr)
		{
			if (parent->ClassToken == t1->ClassToken)
			{
				result.Boolean = true;
				break;
			}

			parent = _classes.GetClassWithToken(parent->ParentToken, false);
		}

		// Am I an interface the other implements?
		if (t2->ImplementsInterface(t1->ClassToken))
		{
			result.Boolean = true;
			break;
		}
		result.Boolean = false;
	}
	break;
	case NativeMethod::TypeGetGenericTypeDefinition:
		{
			// Given a constructed generic type, this returns the open generic type.
			ASSERT(args.size() == 1);
			Variable type1 = args[0]; // type1. An (instantiated) generic type
			ClassDeclaration* ty = _classes.GetClassWithToken(KnownTypeTokens::Type);
			Variable tok1 = GetField(ty, type1, 0);
			int token = tok1.Int32;
			if ((token & SPECIAL_TOKEN_MASK) == SPECIAL_TOKEN_MASK)
			{
				int* tokenListEntry = GetSpecialTokenListEntry(token, true);
				token = tokenListEntry[2];
			}
			else
			{
				token = token & GENERIC_TOKEN_MASK;

				if (token == 0)
				{
					// Type was not generic -> throw InvalidOperationException
					throw ClrException(SystemException::InvalidOperation, token);
				}
			}
			
			void* ptr = CreateInstanceOfClass((int)KnownTypeTokens::Type, 0);

			result.Object = ptr;
			result.Type = VariableKind::Object;

			tok1.Int32 = token;
			tok1.Type = VariableKind::Int32;
			SetField4(ty, tok1, result, 0);
		
		}
		break;
	case NativeMethod::EnumInternalGetValues:
		{
			ASSERT(args.size() == 1);
			Variable ownTypeInstance = args[0]; // A type instance
			ClassDeclaration* typeClassDeclaration = GetClassDeclaration(ownTypeInstance);
			Variable ownToken = GetField(typeClassDeclaration, ownTypeInstance, 0);
			ClassDeclaration* enumType = _classes.GetClassWithToken(ownToken.Int32);
			ASSERT(enumType->IsEnum());
			// Number of static fields computed as total static size divided by underlying type size
			int numberOfValues = enumType->ClassStaticSize / enumType->ClassDynamicSize;
			AllocateArrayInstance((int)KnownTypeTokens::Uint64, numberOfValues, result);
			uint64_t* data = (uint64_t*)AddBytes(result.Object, ARRAY_DATA_START);
			int idx = 0;
			for (int i = 0; i <= numberOfValues; i++) // One extra, because we later skip the actual value field and use only the static fields here
			{
				Variable* field = enumType->GetFieldByIndex(i);
				if ((field->Type & VariableKind::StaticMember) == VariableKind::Void)
				{
					continue;
				}

				// Values are currently never > 32Bit (would need an extension to the class transfer protocol)
				data[idx] = field->Uint64;
				idx++;
			}
		break;
		}
	case NativeMethod::TypeIsEnum:
		ASSERT(args.size() == 1);
	{
		// Find out whether the current type inherits (directly) from System.Enum
			Variable ownTypeInstance = args[0]; // A type instance
			ClassDeclaration* typeClassDeclaration = GetClassDeclaration(ownTypeInstance);
			Variable ownToken = GetField(typeClassDeclaration, ownTypeInstance, 0);
			result.Type = VariableKind::Boolean;
			ClassDeclaration* t1 = _classes.GetClassWithToken(ownToken.Int32);
			// IsEnum returns true for enum types, but not if the type itself is "System.Enum".
			result.Boolean = t1->ParentToken == (int)KnownTypeTokens::Enum;
			if (result.Boolean)
			{
				// Secondary verification
				ASSERT(t1->IsEnum());
			}
			break;
	}
	case NativeMethod::TypeIsValueType:
		{
			// The type represented by the type instance (it were quite pointless if Type.IsValueType returned whether System::Type was a value type - it is not)
			ClassDeclaration* t1 = GetTypeFromTypeInstance(args[0]);
			result.Type = VariableKind::Boolean;
			result.Boolean = t1->IsValueType();
		}
		break;
	case NativeMethod::TypeGetGenericArguments:
		ASSERT(args.size() == 1);
		{
			// Get the type of the generic argument as an array. It is similar to GetGenericTypeDefinition, but returns the other part (for a single generic argument)
			ASSERT(args.size() == 1);
			Variable type1 = args[0]; // type1. An (instantiated) generic type
			ClassDeclaration* ty = _classes.GetClassWithToken(KnownTypeTokens::Type);
			Variable tok1 = GetField(ty, type1, 0);
			int writableToken = tok1.Int32;
			int* tokenList = &writableToken;
			int arraySize = 1;
			
			// If the token is a generic (open) type, we return "self"
			if ((int)(writableToken & GENERIC_TOKEN_MASK) == writableToken)
			{
				writableToken = (int)KnownTypeTokens::Type;
			}
			else if ((writableToken & SPECIAL_TOKEN_MASK) == SPECIAL_TOKEN_MASK)
			{
				// This is token from our special list. The generic arguments need to be looked up.
				int* tokenListEntry = GetSpecialTokenListEntry(writableToken, true);
				int length = tokenListEntry[0];
				arraySize = length - 3;
				tokenList = tokenListEntry + 3;
			}
			else
			{
				// otherwise we return the type of the generic argument
				writableToken = writableToken & ~GENERIC_TOKEN_MASK;
			}

			AllocateArrayInstance((int)KnownTypeTokens::Type, arraySize, result);

			int i = 0;
			while (arraySize > 0)
			{
				void* ptr = CreateInstanceOfClass((int)KnownTypeTokens::Type, 0);

				Variable t1;
				t1.Object = ptr;
				t1.Type = VariableKind::Object;

				int nextToken = *tokenList;
				if (_classes.GetClassWithToken(nextToken) == nullptr)
				{
					throw ClrException(SystemException::ClassNotFound, nextToken);
				}

				tok1.Int32 = nextToken;
				SetField4(ty, tok1, t1, 0);
				uint32_t* data = (uint32_t*)result.Object;
				// Set the element of the array to the type instance
				*(data + 3 + i) = (uint32_t)ptr;

				tokenList++; // go to next element
				i++;
				arraySize--;
			}
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
	case NativeMethod::BitConverterDoubleToUInt64Bits:
		ASSERT(args.size() == 1);
		result.setSize(8);
		result.Type = VariableKind::Uint64;
		result.Uint64 = args[0].Uint64; // Using the wrong union element here does what we want: A binary conversion from double to int64
		break;
	case NativeMethod::BitConverterDoubleToInt64Bits:
		ASSERT(args.size() == 1);
		result.setSize(8);
		result.Type = VariableKind::Int64;
		result.Int64 = args[0].Int64; // Using the wrong union element here does what we want: A binary conversion from double to int64
		break;
	case NativeMethod::UInt64BitsToDouble:
	case NativeMethod::BitConverterInt64BitsToDouble:
		ASSERT(args.size() == 1);
		result.setSize(8);
		result.Type = VariableKind::Double;
		result.Double = args[0].Double;
		break;
	case NativeMethod::UnsafeNullRef:
		{
			// This just returns a null pointer
		result.Object = nullptr;
		result.Type = VariableKind::AddressOfVariable;
		}
		break;
	case NativeMethod::UnsafeSizeOfType:
		{
			Variable ownTypeInstance = args[0]; // A type instance
			ClassDeclaration* typeClassDeclaration = GetClassDeclaration(ownTypeInstance);
			if (typeClassDeclaration == nullptr)
			{
				throw ClrException("Unknown type for sizeof()", SystemException::NullReference, 0);
			}
			Variable ownToken = GetField(typeClassDeclaration, ownTypeInstance, 0);
			// The type represented by the type instance (it were quite pointless if Type.IsValueType returned whether System::Type was a value type - it is not)
			ClassDeclaration* t1 = _classes.GetClassWithToken(ownToken.Int32);
			result.Type = VariableKind::Int32;
			result.Int32 = t1->ClassDynamicSize;
			result.setSize(4);
		}
		break;
	case NativeMethod::UnsafeAreSame:
		{
			// This compares two references for equality
		ASSERT(args.size() == 2);
		Variable a = args[0];
		Variable b = args[1];
		ASSERT(a.Type == VariableKind::AddressOfVariable);
		ASSERT(b.Type == VariableKind::AddressOfVariable);
		result.Type = VariableKind::Boolean;
		result.Boolean = a.Object == b.Object;
		}
		break;
	case NativeMethod::UnsafeAddByteOffset:
		{
		ASSERT(args.size() == 2);
		result.Type = VariableKind::AddressOfVariable;
		result.Object = AddBytes(args[0].Object, args[1].Int32);
		result.setSize(4);
		}
		break;
	case NativeMethod::StringCompareTo:
		{
			// Actually, this should do a language-dependent sorting. But for now, the implementation is the same as CompareStringsOrdinal
			ASSERT(args.size() == 2);
			result.Type = VariableKind::Int32;
			result.setSize(4);
			Variable& a = args[0];
			Variable& b = args[1];
			if (b.Object == nullptr)
			{
				result.Int32 = 1; // by definition, this returns >0 for comparing to null
			}
			else
			{
				ClassDeclaration* ty1 = GetClassDeclaration(a);
				ClassDeclaration* ty2 = GetClassDeclaration(b);
				ASSERT(ty1->ClassToken == (int)KnownTypeTokens::String);
				ASSERT(ty2->ClassToken == (int)KnownTypeTokens::String);
				int len1 = *AddBytes((int*)a.Object, 4);
				int len2 = *AddBytes((int*)b.Object, 4);
				int min = MIN(len1, len2);
				for (int i = 0; i < min; i++)
				{
					uint16_t c1 = *AddBytes((uint16_t*)a.Object, STRING_DATA_START + i * 2);
					uint16_t c2 = *AddBytes((uint16_t*)b.Object, STRING_DATA_START + i * 2);
					if (c1 < c2)
					{
						result.Int32 = -1;
						return;
					}
					else if (c1 > c2)
					{
						result.Int32 = 1;
						return;
					}
				}

				// If they're equal so far, the shorter string shall precede the longer one.
				if (len1 == len2)
				{
					result.Int32 = 0;
					return;
				}
				
				result.Int32 = len1 < len2 ? -1 : 1;
				return;
			}
		}
		break;
	case NativeMethod::UnsafeAs2:
		{
		ASSERT(args.size() == 1);
			if (args[0].fieldSize() > 8)
			{
				// LargeValueStructs not supported here (TODO)
				throw ClrException(SystemException::NotSupported, currentFrame->_executingMethod->methodToken);
			}
			result = args[0];
		}
		break;
	case NativeMethod::UnsafeAsPointer:
		{
			ASSERT(args.size() == 1);
			result = args[0];
			result.Type = VariableKind::Uint32;
		}
		break;
	case NativeMethod::ByReferenceCtor:
		{
		ASSERT(args.size() == 2); // this + object
		Variable ptr = args[1];
		Variable& self = args[0]; // ByReference<T> is a struct, therefore the "this" pointer is a reference
		// *((int*)self.Object) = ptr.Uint32;
		void** thisPtr = reinterpret_cast<void**>(self.Object);
		void* val = ptr.Object;
		*thisPtr = val;
		// ClassDeclaration* ty = GetClassDeclaration(args[0]);
		// Variable data(ptr.Uint32, VariableKind::AddressOfVariable);
		// SetField4(*ty, data, args[0], 0);
		}
		break;
	case NativeMethod::ByReferenceValue:
		{
		ASSERT(args.size() == 1); // property getter
		Variable& self = args[0]; // ByReference<T> is a struct, therefore the "this" pointer is a reference
		//result.Uint32 = *((int*)self.Object);
		//result.Type = VariableKind::AddressOfVariable;
		void** thisPtr = reinterpret_cast<void**>(self.Object);
		void* value = *thisPtr;
		result.Type = VariableKind::AddressOfVariable;
		result.Object = value;
		result.setSize(4);
		}
		break;
	case NativeMethod::StringGetElem:
		ASSERT(args.size() == 2); // indexer
	{
		Variable& self = args[0];
		Variable& index = args[1];
		uint32_t length = *AddBytes((uint32_t*)self.Object, 4);
		if (index.Uint32 >= length)
		{
			throw ClrException("String indexer: Index out of range", SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
		}
		uint16_t b = *AddBytes((uint16_t*)self.Object, STRING_DATA_START + (index.Int32 * SIZEOF_CHAR)); // Get char at given offset
		result.Int32 = b;
		result.Type = VariableKind::Uint32;
		result.setSize(2);
	}
	break;
	case NativeMethod::StringEquals: // These two are technically identical
	case NativeMethod::StringEqualsStatic:
		{
		result.Type = VariableKind::Boolean;
		result.Boolean = StringEquals(args);
		}
	break;
	case NativeMethod::StringEqualsStringComparison:
		result.Type = VariableKind::Boolean;
		result.Boolean = StringEquals(args, args[2].Int32);
		break;
	case NativeMethod::StringUnEqualsStatic:
	{
		result.Type = VariableKind::Boolean;
		result.Boolean = !StringEquals(args);
	}
	break;
	case NativeMethod::StringGetHashCode:
		ASSERT(args.size() == 1);
		result.Type = VariableKind::Int32;
		result.Int32 = (int32_t)args[0].Object; // Use memory address as hash code
		break;
	case NativeMethod::StringToString:
		ASSERT(args.size() == 1);
		result.Type = VariableKind::Object;
		result.Object = args[0].Object; // Return "this"
		break;
	case NativeMethod::StringFastAllocateString:
		{
		// This creates an instance of System.String with the indicated length but no content.
		ASSERT(args.size() == 1);
		Variable& lengthVar = args[0];
		int length = lengthVar.Int32;
		result.Type = VariableKind::Object;
		result.setSize(sizeof(void*));
		byte* classInstance = (byte*)CreateInstanceOfClass((int)KnownTypeTokens::String, length + 1); // +1 for the terminating 0

		ClassDeclaration* stringInstance = _classes.GetClassWithToken(KnownTypeTokens::String);
		// Length
		Variable v(VariableKind::Int32);
		v.Int32 = length;
		result.Object = classInstance;
		SetField4(stringInstance, v, result, 0);
		}
	break;
	case NativeMethod::StringGetPinnableReference:
		ASSERT(args.size() == 1); // this
		{
			Variable& self = args[0];
			result.setSize(sizeof(void*));
			result.Type = VariableKind::AddressOfVariable;
			result.Object = AddBytes(self.Object, STRING_DATA_START);
		}
		break;
	case NativeMethod::DelegateInternalEqualTypes:
	{
		ASSERT(args.size() == 2);
		// Both parameters should be of the same delegate type.
		ASSERT(args[0].Type == VariableKind::Object);
		ASSERT(args[1].Type == VariableKind::Object);
		ClassDeclaration* cls1 = GetClassDeclaration(args[0]);
		ClassDeclaration* cls2 = GetClassDeclaration(args[1]);
		result.Type = VariableKind::Boolean;
		result.setSize(4);
		result.Boolean = cls1->ClassToken == cls2->ClassToken;
	}
		break;
	case NativeMethod::RuntimeHelpersGetHashCode:
	case NativeMethod::ObjectGetHashCode:
		{
		ASSERT(args.size() == 1);
		result.Type = VariableKind::Int32;
		result.setSize(4);
		// The memory address serves pretty fine as general hash code, as long as we don't have a heap compacting GC.
		result.Int32 = (int)args[0].Object;
		}
		break;
	case NativeMethod::MemoryMarshalGetArrayDataReference:
	{
		ASSERT(args.size() == 1);
		Variable& array = args[0];
		ASSERT(array.Type == VariableKind::ReferenceArray || array.Type == VariableKind::ValueArray);
		result.Object = AddBytes(array.Object, ARRAY_DATA_START);
		result.Type = VariableKind::AddressOfVariable;
		result.setSize(sizeof(void*));
	}
	break;
	case NativeMethod::MarshalCopyReverse4: // Marshal.Copy(byte[] source, int startIndex, IntPtr destination, int length)
	{
		ASSERT(args.size() == 4);
		Variable& array = args[0];
		byte* src = AddBytes((byte*)array.Object, ARRAY_DATA_START);
		src += args[1].Int32;
		byte* dest = (byte*)args[2].Object; // an intptr's value is actually the address
		int length = args[3].Int32;
		memcpy(dest, src, length);
	}
	break;
	case NativeMethod::BufferZeroMemory:
		{
		ASSERT(args.size() == 2);
		result.Type = VariableKind::Void;
		Variable& b = args[0];
		Variable& length = args[1];
		memset(b.Object, 0, length.Int32);
		}
		break;
	case NativeMethod::BufferMemmove:
		{
		ASSERT(args.size() == 3);
		result.Type = VariableKind::Void;
		Variable& dest = args[0];
		Variable& src = args[1];
		Variable& length = args[2];
		memmove(dest.Object, src.Object, length.Int32);
		}
		break;
	case NativeMethod::ArrayCopyCore:
		{
		ASSERT(args.size() == 5);
		Variable& srcArray = args[0];
		Variable& srcIndex = args[1];
		Variable& dstArray = args[2];
		Variable& dstIndex = args[3];
		Variable& length = args[4];
		ASSERT(srcArray.Type == VariableKind::ReferenceArray || srcArray.Type == VariableKind::ValueArray);
		ASSERT(dstArray.Type == VariableKind::ReferenceArray || dstArray.Type == VariableKind::ValueArray);
		ClassDeclaration* srcType = GetClassWithToken(*AddBytes((int32_t*)srcArray.Object, 8));
		ClassDeclaration* dstType = GetClassWithToken(*AddBytes((int32_t*)srcArray.Object, 8));
			if (srcType != dstType)
			{
				throw ClrException(SystemException::ArrayTypeMismatch, srcType->ClassToken);
			}
		byte* srcPtr = (byte*)AddBytes(srcArray.Object, ARRAY_DATA_START + (srcType->ClassDynamicSize * srcIndex.Int32));
		byte* dstPtr = (byte*)AddBytes(dstArray.Object, ARRAY_DATA_START + (dstType->ClassDynamicSize * dstIndex.Int32));
		int bytesToCopy = 0;
		if (srcArray.Type == VariableKind::ReferenceArray)
		{
			bytesToCopy = sizeof(void*) * length.Int32;
		}
		else
		{
			bytesToCopy = srcType->ClassDynamicSize * length.Int32;
		}
		memmove(dstPtr, srcPtr, bytesToCopy);
		result.Type = VariableKind::Void;
		}
		break;
	case NativeMethod::ArrayInternalCreate:
	{
		ASSERT(args.size() == 4);
		Variable& ownTypeInstance = args[0];
		Variable& dimensions = args[1];
		Variable& pLengths = args[2];
		ClassDeclaration* typeClassDeclaration = GetClassDeclaration(ownTypeInstance);
		if (typeClassDeclaration == nullptr)
		{
			throw ClrException("Unknown type for sizeof()", SystemException::NullReference, 0);
		}

		if (dimensions.Int32 != 1)
		{
			throw ClrException("Arrays with more than 1 dimension are not supported", SystemException::NotSupported, currentFrame->_executingMethod->methodToken);
		}
			
		Variable token = GetField(typeClassDeclaration, ownTypeInstance, 0);
		int* length = (int*)pLengths.Object;
		AllocateArrayInstance(token.Int32, *length, result);
		break;
	}
	case NativeMethod::ArrayGetLength:
		{
		ASSERT(args.size() == 1);
		Variable& value1 = args[0];
		uint32_t* data = (uint32_t*)value1.Object;
		int32_t size = *(data + 1);
		result.Int32 = size;
		result.Type = VariableKind::Int32;
		break;
		}
	case NativeMethod::ArrayGetValue1:
		{
		ASSERT(args.size() == 2);
		Variable& value1 = args[0];
		if (value1.Object == nullptr)
		{
			throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
		}
		// The instruction suffix (here .i4) indicates the element size
		uint32_t* data = (uint32_t*)value1.Object;
		int32_t size = *(data + 1);
		int32_t token = *(data + 2);
		int32_t index = args[1].Int32;
		if (index < 0 || index >= size)
		{
			throw ClrException("Index out of range in ArrayGetValue1 operation", SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
		}

		ClassDeclaration* elemTy = GetClassWithToken(token);
		
		if (elemTy->IsValueType())
		{
			Variable* var = (Variable*)alloca(elemTy->ClassDynamicSize + sizeof(Variable));
			memmove(&(var->Uint32), AddBytes(data, ARRAY_DATA_START + index * elemTy->ClassDynamicSize), elemTy->ClassDynamicSize);
			var->Type = elemTy->ClassDynamicSize > 8 ? VariableKind::LargeValueType : VariableKind::Int64;
			var->setSize(elemTy->ClassDynamicSize);
			var->Marker = VARIABLE_DEFAULT_MARKER;
			result = Box(*var, elemTy);
		}
		else
		{
			// can only be an object now
			result.Type = VariableKind::Object;
			result.Object = (void*)*(AddBytes(data, ARRAY_DATA_START + index * sizeof(void*)));
		}
		break;
		}
	case NativeMethod::ArraySetValue1:
	{
		// This operation is (for single-dimensional arrays) equivalent to STELEM with the given target type
		ASSERT(args.size() == 3);
		Variable& value1 = args[0];
		if (value1.Object == nullptr)
		{
			throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
		}
		// The instruction suffix (here .i4) indicates the element size
		uint32_t* data = (uint32_t*)value1.Object;
		int32_t size = *(data + 1);
		int32_t token = *(data + 2);
		int32_t index = args[2].Int32;
		if (index < 0 || index >= size)
		{
			throw ClrException("Index out of range in STELEM.I4 operation", SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
		}

		ClassDeclaration* elemTy = GetClassWithToken(token);

		if (value1.Type == VariableKind::ValueArray)
		{
			// If the target array is a value array, unbox the argument.
			if (args[1].isValueType())
			{
				// The type of the value is object, so this must be a boxed value
				throw ClrException(SystemException::ArrayTypeMismatch, currentFrame->_executingMethod->methodToken);
			}
			
			void* boxPtr = args[1].Object;
			void* boxedValue = AddBytes(boxPtr, sizeof(void*));
			switch (elemTy->ClassDynamicSize)
			{
			case 1:
			{
				byte* dataptr = (byte*)data;
				*(dataptr + ARRAY_DATA_START + index) = *(byte*)boxedValue;
				break;
			}
			case 2:
			{
				short* dataptr = (short*)data;
				*(dataptr + ARRAY_DATA_START / 2 + index) = *(short*)boxedValue;
				break;
			}
			case 4:
			{
				*(data + ARRAY_DATA_START / 4 + index) = *(int*)boxedValue;
				break;
			}
			case 8:
			{
				uint64_t* dataptr = (uint64_t*)data;
				memcpy(AddBytes(dataptr, ARRAY_DATA_START + 8 * index), boxedValue, 8);
				break;
			}
			default: // Arbitrary size of the elements in the array
			{
				byte* dataptr = (byte*)data;
				byte* targetPtr = AddBytes(dataptr, ARRAY_DATA_START + (elemTy->ClassDynamicSize * index));
				memcpy(targetPtr, boxedValue, elemTy->ClassDynamicSize);
				break;
			}
			case 0: // That's fishy
				throw ClrException("Cannot address array with element size 0", SystemException::ArrayTypeMismatch, token);
			}
		}
		else
		{
			if (args[1].Type != VariableKind::Object && args[1].Type != VariableKind::ValueArray && args[1].Type != VariableKind::ReferenceArray)
			{
				// STELEM.ref shall throw if the value type doesn't match the array type. We don't test the dynamic type, but
				// at least it should be a reference
				throw ClrException("Array type mismatch", SystemException::ArrayTypeMismatch, currentFrame->_executingMethod->methodToken);
			}
			// can only be an object now
			*(data + ARRAY_DATA_START / 4 + index) = (uint32_t)args[1].Object;
		}
		break;
	}
	case NativeMethod::EnumToUInt64:
		{
		// The this pointer is passed via a double indirection to this method!
		ASSERT(args.size() == 1);
		result.Type = VariableKind::Int64;
		result.setSize(8);
		ClassDeclaration** reference = (ClassDeclaration**)args[0].Object;
		ClassDeclaration* clsType = *reference;
		if (clsType->ClassDynamicSize <= 4)
		{
			int* obj = (int*)AddBytes(reference, 4);
			int32_t value = *obj;
			result.Uint64 = value;
		}
		else
		{
			// Probably rare: Enum with 64 bit base type
			int64_t* obj = (int64_t*)AddBytes(reference, 4);
			int64_t value2 = *obj;
			result.Uint64 = value2;
		}
		break;
		}
	case NativeMethod::EnumInternalBoxEnum:
		{
		ASSERT(args.size() == 2);
		ClassDeclaration* ty = GetTypeFromTypeInstance(args[0]);
		Variable& value = args[1];
		result = Box(value, ty);
		}
		break;
	case NativeMethod::GcCollect:
		ASSERT(args.size() == 4); // Has 4 args, but they mostly are for optimization purposes
		_gc.Collect(args[0].Int32, this);
		result.Type = VariableKind::Void;
		break;
	case NativeMethod::GcGetTotalAllocatedBytes:
		ASSERT(args.size() == 1);
		result.Type = VariableKind::Int64;
		result.Int64 = _gc.TotalAllocatedBytes();
		break;
	case NativeMethod::GcGetTotalMemory:
		ASSERT(args.size() == 1);
		result.Type = VariableKind::Int64;
		result.Int64 = _gc.TotalMemory();
		break;
	case NativeMethod::GcTotalAvailableMemoryBytes:
		{
		ASSERT(args.size() == 0);
		result.Type = VariableKind::Int64;
		result.Int64 = _gc.AllocatedMemory();
		break;
		}
	case NativeMethod::StringCtorCharCount:
	{
		// This is a ctor. The actual implementation is in the NEWOBJ instruction, therefore this just needs to copy the reference back
		ASSERT(args.size() == 3);
		result = args[0];
		break;
	}
	case NativeMethod::StringCtorSpan:
	case NativeMethod::StringCtorCharPtr:
	case NativeMethod::StringCtorCharPtr3:
	case NativeMethod::StringCtorCharArray:
		{
			// This is a ctor. The actual implementation is in the NEWOBJ instruction, therefore this just needs to copy the reference back
			result = args[0];
		break;
		}
	case NativeMethod::MathCeiling:
		{
		result = args[0]; // Copy input type
		result.Double = ceil(result.Double);
		break;
		}
	case NativeMethod::MathFloor:
	{
		result = args[0]; // Copy input type
		result.Double = floor(result.Double);
		break;
	}
	case NativeMethod::MathLog:
	{
		result = args[0]; // Copy input type
		result.Double = log(result.Double);
		break;
	}
	case NativeMethod::MathLog2:
	{
		result = args[0]; // Copy input type
		result.Double = log2(result.Double);
		break;
	}
	case NativeMethod::MathLog10:
	{
		result = args[0]; // Copy input type
		result.Double = log10(result.Double);
		break;
	}
	case NativeMethod::MathSin:
	{
		result = args[0]; // Copy input type
		result.Double = sin(result.Double);
		break;
	}
	case NativeMethod::MathCos:
	{
		result = args[0]; // Copy input type
		result.Double = cos(result.Double);
		break;
	}
	case NativeMethod::MathTan:
	{
		result = args[0]; // Copy input type
		result.Double = tan(result.Double);
		break;
	}
	case NativeMethod::MathSqrt:
	{
		result = args[0]; // Copy input type
		result.Double = sqrt(result.Double);
		break;
	}
	case NativeMethod::MathPow:
	{
		ASSERT(args.size() == 2);
		result = args[0]; // Copy input type
		result.Double = pow(args[0].Double, args[1].Double);
		break;
	}
	case NativeMethod::MathExp:
		{
		result = args[0];
		result.Double = exp(args[0].Double);
		break;
		}
	case NativeMethod::MathAbs:
	{
		result = args[0];
		result.Double = abs(args[0].Double);
		break;
	}
	case NativeMethod::DebugWriteLine:
		{
		ASSERT(args.size() == 1);
		Variable& string = args.at(0);
		char* cstr = GetAsUtf8String(string);
		Firmata.sendString(STRING_DATA, cstr);
		free(cstr);
		break;
		}
	case NativeMethod::Kernel32_WideCharToMultiByte:
		{
		ASSERT(args.size() == 8);
		result.Type = VariableKind::Int32;
			// Arg0 is the destination code page
		int codePage = args[0].Int32;
		wchar_t* source = (wchar_t*)args[2].Object;
		int sourceLen = args[3].Int32;
		char* destination = (char*)args[4].Object;
		int destinationLen = args[5].Int32;
			if (destinationLen == 0)
			{
				result.Int32 = sourceLen * 2; // Should be enough
				break;
			}
		int usedDestination = 0;
		if (codePage == 1200) // UTF-16. Just copy input to output for now
		{
			if (destinationLen < 2 * sourceLen)
			{
				result.Int32 = 0;
				SetLastError(ERROR_INSUFFICIENT_BUFFER);
				break;
			}
			memcpy_s(destination, destinationLen, source, sourceLen * 2);
			result.Int32 = destinationLen;
		}
		else if (codePage == 65001) // UTF-8
		{
			for (int i = 0; i < sourceLen; i++)
			{
				if (usedDestination >= destinationLen - 1)
				{
					result.Type = VariableKind::Int32;
					result.Int32 = 0;
					SetLastError(ERROR_INSUFFICIENT_BUFFER);
					break;
				}
				uint16_t charToEncode = *source;
				source++;
				usedDestination += unicode_to_utf8(charToEncode, destination);
			}
			*destination = 0;
			result.Int32 = usedDestination;
		}
		else
		{
			result.Int32 = 0;
			SetLastError(ERROR_INVALID_PARAMETER);
		}
		}
		break;
	case NativeMethod::Kernel32_WriteConsole:
		{
		ASSERT(args.size() == 5);
		result.Type = VariableKind::Int32;
		result.Int32 = ERROR_SUCCESS;
			if (args[0].Int32 == STANDARD_OUTPUT_HANDLE || args[0].Int32 == STANDARD_ERROR_HANDLE) // Standard or error outputs
			{
				char* buf = GetAsUtf8String((wchar_t*)args[1].Object, args[2].Int32);
				Firmata.sendString(STRING_DATA, buf);
				freeEx(buf);
				int* written = (int*)args[3].Object; // A ref parameter
				*written = args[2].Int32;
			}
		}
		break;
	case NativeMethod::NoOp:
		// this is used for methods that should just be suppressed (e.g. some variants of Debug.Write)
		// This saves a few bytes of memory each time, because we don't have to provide an empty implementation
		result.Type = VariableKind::Void;
		break;
	default:
		throw ClrException("Unknown internal method", SystemException::MissingMethod, currentFrame->_executingMethod->methodToken);
	}

}

/// <summary>
/// Finds the token that is the class constructed from all elements in tokenList. This is used for extended cases of Type.MakeGenericType()
/// </summary>
/// <param name="mainToken">Token of the main class (i.e. token of IEnumerable{T}, or Dictionary{TKey, TValue})</param>
/// <param name="tokenList">Pointer to a managed array</param>
///	<param name="tokenListContainsTypes">True if the token list contains object references to instances of System.Type, false if it directly contains tokens</param>
/// <param name="searchList">The list to search (either RAM or Flash)</param>
/// <returns>The token that can be used to construct the combined type</returns>
int FirmataIlExecutor::ReverseSearchSpecialTypeList(int mainToken, void* tokenList, bool tokenListContainsTypes, const int* searchList)
{
	if (searchList != nullptr)
	{
		int entryLength = searchList[0];
		int index = 0;
		ClassDeclaration* typeOfType = _classes.GetClassWithToken(2);

		while (entryLength > 0)
		{
			// Every entry has at least 4 values (the length, the token we're looking for, the main token and one sub-token)
			if (searchList[index + 2] == mainToken)
			{
				int* data = (int*)tokenList;
				int32_t size = *(data + 1);
				for (int param = 0; param < size; param++)
				{
					uint32_t parameter = *(data + param + ARRAY_DATA_START / 4); // Element of array
					int tokenToFind = 0;
					if (tokenListContainsTypes)
					{
						Variable argumentTypeInstance;
						// First, get element of array (an object)
						argumentTypeInstance.Uint32 = parameter;
						argumentTypeInstance.Type = VariableKind::Object;
						// then get its first field, which is the type token
						Variable argumentType = GetField(typeOfType, argumentTypeInstance, 0);
						tokenToFind = argumentType.Int32;
					}
					else
					{
						tokenToFind = (int)parameter;
					}
					if (tokenToFind != searchList[index + 3 + param])
					{
						goto doContinue;
					}
				}

				// If we get here, we have found one entry where the main token and all parameter tokens match.
				// That means we have found the entry that MakeGenericType shall construct
				return searchList[index + 1];
			}
			doContinue:
			index += entryLength;
			entryLength = searchList[index];
		}
	}

	return 0;
}

int FirmataIlExecutor::ReverseSearchSpecialTypeList(int32_t genericToken, bool tokenListContainsTypes, void* tokenList)
{
	int newToken = ReverseSearchSpecialTypeList(genericToken, tokenList, tokenListContainsTypes, _specialTypeListRam);
	if (newToken == 0)
	{
		newToken = ReverseSearchSpecialTypeList(genericToken, tokenList, tokenListContainsTypes, _specialTypeListFlash);
	}

	return newToken;
}

int* FirmataIlExecutor::GetSpecialTokenListEntry(int token, bool searchWithMainToken)
{
	int* result = GetSpecialTokenListEntryCore(_specialTypeListFlash, token, searchWithMainToken);
	if (result == nullptr)
	{
		result = GetSpecialTokenListEntryCore(_specialTypeListRam, token, searchWithMainToken);
	}

	if (result == nullptr)
	{
		throw ClrException("Unable to find special token", SystemException::ClassNotFound, token);
	}

	return result;
}

int* FirmataIlExecutor::GetSpecialTokenListEntryCore(int* tokenList, int token, bool searchWithMainToken)
{
	if (tokenList == nullptr)
	{
		return nullptr;
	}
	
	// If searchWithMainToken is true, we search for the combined token, otherwise we search for the left type (the open type)
	int compareIndex = searchWithMainToken ? 1 : 2;
	while (*tokenList != 0)
	{
		int length = tokenList[0];
		if (tokenList[compareIndex] == token)
		{
			return tokenList;
		}
		
		tokenList += length;
	}
	
	return nullptr;
}


Variable FirmataIlExecutor::GetField(ClassDeclaration* type, const Variable& instancePtr, int fieldNo)
{
	int idx = 0;
	uint32_t offset = sizeof(void*);
	// We could be faster by doing
	// offset += Variable::datasize() * fieldNo;
	// but we still need the field handle for the type
	byte* o = (byte*)instancePtr.Object;

	if (o == nullptr)
	{
		throw ClrException("NullReferenceException accessing field", SystemException::NullReference, type->ClassToken);
	}

	vector<Variable*> allfields;
	CollectFields(type, allfields);
	for (auto handle1 =allfields.begin(); handle1 != allfields.end(); ++handle1)
	{
		Variable* handle = *handle1;
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
			ASSERT(handle->fieldSize() <= 8);
			memcpy(&v.Object, (o + offset), handle->fieldSize());
			v.Type = handle->Type;
			return v;
		}

		offset += handle->fieldSize();
		idx++;
	}
	
	throw ExecutionEngineException("Field not found in class.");
}


void FirmataIlExecutor::SetField4(ClassDeclaration* type, const Variable& data, Variable& instance, int fieldNo)
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
Variable& FirmataIlExecutor::GetVariableDescription(ClassDeclaration* vtable, int32_t token)
{
	if (vtable->ParentToken > 1) // Token 1 is the token of System::Object, which does not have any fields, so we don't need to go there.
	{
		ClassDeclaration* parent = _classes.GetClassWithToken(vtable->ParentToken);
		Variable& v = GetVariableDescription(parent, token);
		if (v.Type != VariableKind::Void)
		{
			return v;
		}
	}

	int idx = 0;
	for (auto handle = vtable->GetFieldByIndex(idx); handle != nullptr; handle = vtable->GetFieldByIndex(++idx))
	{
		if (handle->Int32 == token)
		{
			return *handle;
		}
	}

	return _clearVariable;
}

void FirmataIlExecutor::CollectFields(ClassDeclaration* vtable, vector<Variable*>& vector)
{
	// Do a prefix-recursion to collect all fields in the class pointed to by vtable and its bases. The updated
	// vector must be sorted base-class members first
#if GC_DEBUG_LEVEL >= 2
	if ((int)vtable == 0xaaaaaaaa)
	{
		throw ExecutionEngineException("Accessing deleted object - this is a GC error");
	}
#endif
	if (vtable->ParentToken > 1) // Token 1 is the token of System::Object, which does not have any fields, so we don't need to go there.
	{
		ClassDeclaration* parent = _classes.GetClassWithToken(vtable->ParentToken);
		CollectFields(parent, vector);
	}

	int idx = 0;
	for (auto handle = vtable->GetFieldByIndex(idx); handle != nullptr; handle = vtable->GetFieldByIndex(++idx))
	{
		vector.push_back(handle);
	}
}

/// <summary>
/// Load a value from field "token" of instance "obj". Returns the pointer to the location of the value (which might have arbitrary size)
/// </summary>
/// <param name="obj">The instance which contains the field (can be an object, a reference to an object or a value type)</param>
/// <param name="token">The field token</param>
/// <param name="description">[Out] The description of the field returned</param>
/// <returns>Pointer to the data of the field</returns>
byte* FirmataIlExecutor::Ldfld(Variable& obj, int32_t token, VariableDescription& description)
{
	byte* o;
	ClassDeclaration* vtable;
	int offset;
	if (obj.Type == VariableKind::AddressOfVariable)
	{
		vtable = ResolveClassFromFieldToken(token);
		offset = 0; // No extra header
		o = (byte*)obj.Object; // Data being pointed to
	}
	else if (obj.Type != VariableKind::Object)
	{
		// Ldfld from a value type needs one less indirection, but we need to get the type first.
		// The value type does not carry the type information. Lets derive it from the field token.
		// TODO: This is slow, not what one expects from accessing a value type
		vtable = ResolveClassFromFieldToken(token);
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

	Firmata.sendStringf(F("Class %lx has no member %lx"), vtable->ClassToken, token);
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
		vtable = ResolveClassFromFieldToken(token);
		offset = 0; // No extra header
		o = (byte*)obj.Object; // Data being pointed to
	}
	else if (obj.Type != VariableKind::Object)
	{
		// Ldfld from a value type needs one less indirection, but we need to get the type first.
		// The value type does not carry the type information. Lets derive it from the field token.
		// TODO: This is slow, not what one expects from accessing a value type
		vtable = ResolveClassFromFieldToken(token);
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

	Firmata.sendStringf(F("Class %lx has no member %lx"), vtable->ClassToken, token);
	throw ClrException(SystemException::FieldAccess, token);
}

void FirmataIlExecutor::InitStaticVector()
{
	if (_staticVectorMemorySize == 0 || _staticVector != nullptr)
	{
		return;
	}

	_staticVector = (byte*)mallocEx(_staticVectorMemorySize);
	if (_staticVector == nullptr)
	{
		OutOfMemoryException::Throw("Not enough memory to allocate root vector for statics");
	}

	byte* currentPtr = _staticVector;
	for (auto iterator = _classes.GetIterator(); iterator.Next();)
	{
		// TRACE(Firmata.sendString(F("Class "), cls.ClassToken));
		int idx = 0;
		ClassDeclaration* current = iterator.Current();
		if (current->IsEnum())
		{
			continue;
		}
		for (auto field = current->GetFieldByIndex(idx); field != nullptr; field = current->GetFieldByIndex(++idx))
		{
			if ((field->Type & VariableKind::StaticMember) == VariableKind::Void)
			{
				continue;
			}
			auto initValue = _constants.BinarySearchKey(field->Int32);
			if (initValue != nullptr && initValue->Length > 8)
			{
				// This is a const field of a PrivateImplementationDetails class, and a large one. Don't duplicate these.
				continue;
			}
			int* token = (int*)currentPtr;
			*token = field->Int32;
			Variable* var = AddBytes((Variable*)currentPtr, 4);
			var->Type = field->Type & ~VariableKind::StaticMember;
			size_t sizeToUse = MAX(field->fieldSize(), 4);
			var->Marker = VARIABLE_DEFAULT_MARKER;
			var->setSize((uint16_t)sizeToUse);
			// Firmata.sendStringf(F("Adding field 0x%x with size %d at offset %d"), field->Int32, sizeToUse, currentPtr - _staticVector);

			memset(&var->Int32, 0, field->fieldSize());
			// Reference types are not initialized directly in metadata, I think (but always use an explicit load call)
			if (initValue != nullptr)
			{
				memcpy(&var->Int64, &initValue->DataStart, sizeToUse);
			}
			currentPtr = AddBytes(currentPtr, 4 + 4 + sizeToUse);
			ASSERT(currentPtr <= _staticVector + _staticVectorMemorySize);
		}
	}
}

/// <summary>
/// Load a value from a static field
/// </summary>
/// <param name="token">Token of the static field</param>
///	<param name="description">Filled with the field description when returning</param>
///	<returns>A pointer to the data of the field</returns>
byte* FirmataIlExecutor::Ldsfld(int token, VariableDescription& description)
{
	InitStaticVector();

	Variable* ptr = FindStaticField(token);

	description.Marker = VARIABLE_DEFAULT_MARKER;
	description.Size = ptr->fieldSize();
	description.Type = ptr->Type & ~VariableKind::StaticMember;
	return (byte*)&ptr->Int32;
}

Variable* FirmataIlExecutor::FindStaticField(int32_t token) const
{
	size_t offset = 0;
	while (offset < _staticVectorMemorySize)
	{
		int currentToken = *AddBytes((int*)_staticVector, offset);
		Variable* ptr = (Variable*)AddBytes(_staticVector, offset + sizeof(int32_t));
		if (token == currentToken)
		{
			return ptr;
		}

		offset += sizeof(int32_t) + 4 + ptr->fieldSize();
	}

	throw ClrException(SystemException::FieldAccess, token);
}

/// <summary>
/// Load a value address of a static field
/// </summary>
/// <param name="token">Token of the static field</param>
Variable FirmataIlExecutor::Ldsflda(int token)
{
	Variable ret;
	InitStaticVector();

	ret.Type = VariableKind::AddressOfVariable;
	ret.Marker = VARIABLE_DEFAULT_MARKER;
	ret.setSize(4);

	auto entry1 = _constants.BinarySearchKey(token);

	if (entry1 != nullptr)
	{
		// This static field has a non-zero default value.
		// Just return the address within the constant vector. The compiler
		// should never try to write to these fields.
		ret.Object = &entry1->DataStart;
		return ret;
	}

	Variable* staticVar = FindStaticField(token);
	ret.Object = &staticVar->Int32;
	return ret;
}


void FirmataIlExecutor::Stsfld(int token, Variable& value)
{
	InitStaticVector();

	Variable* ptr = FindStaticField(token);
	ASSERT(value.fieldSize() <= ptr->fieldSize());
	memcpy_s(&ptr->Int32, ptr->fieldSize(), &value.Int32, ptr->fieldSize());
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
		cls = ResolveClassFromFieldToken(token);
		offset = 0; // No extra header
		o = (byte*)obj.Object; // Data being pointed to
	}
	else if (obj.Type != VariableKind::Object)
	{
		// Stfld to a value type needs one less indirection, but we need to get the type first.
		// The value type does not carry the type information. Lets derive it from the field token.
		// TODO: This is slow, not what one expects from accessing a value type

		cls = ResolveClassFromFieldToken(token);
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
case VariableKind::AddressOfVariable:\
	intermediate.Uint32 = value1.Uint32 op value2.Uint32;\
	intermediate.Type = VariableKind::AddressOfVariable;\
	break;\
case VariableKind::NativeHandle:\
	intermediate.Uint32 = value1.Uint32 op value2.Uint32;\
	intermediate.Type = VariableKind::NativeHandle;\
	break;\
case VariableKind::Object: /* Only allowed in unsafe context */\
	intermediate.Uint32 = value1.Uint32 op value2.Uint32;\
	intermediate.Type = VariableKind::Object;\
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
	Firmata.sendStringf(F("Comparing value of type %d, Value %d"), value1.Type, value1.Int32);\
	throw ClrException("Unsupported case in binary operation", SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);\
}


#define ComparisonOperation(op) \
intermediate.Type = VariableKind::Boolean;\
switch (value1.Type)\
{\
case VariableKind::Int32:\
	intermediate.Boolean = value1.Int32 op value2.Int32;\
	break;\
case VariableKind::Object:\
case VariableKind::NativeHandle:\
case VariableKind::AddressOfVariable:\
case VariableKind::ReferenceArray:\
case VariableKind::ValueArray:\
	intermediate.Boolean = value1.Object op value2.Object; \
	break;\
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
case VariableKind::Void: /* Happens when comparing references in uninitialized array fields */ \
	intermediate.Boolean = value1.Object op value2.Object;\
	break;\
default:\
	throw ClrException("Unsupported case in comparison operation", SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);\
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
	throw ClrException("Unsupported case in binary operation", SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);\
}

Variable MakeUnsigned(Variable &value)
{
	if (value.Type == VariableKind::LargeValueType)
	{
		throw ClrException("MakeUnsigned on value type is illegal", SystemException::InvalidOperation, 0);
	}
	Variable copy = value;
	if (copy.Type == VariableKind::Int64 || copy.Type == VariableKind::Uint64)
	{
		copy.Type = VariableKind::Uint64; 
	}
	else if (copy.Type == VariableKind::Int32)
	{
		copy.Type = VariableKind::Uint32;
	}
	return copy;
}

Variable MakeSigned(Variable& value)
{
	if (value.Type == VariableKind::LargeValueType)
	{
		throw ClrException("MakeUnsigned on value type is illegal", SystemException::InvalidOperation, 0);
	}
	Variable copy = value;
	if (copy.Type == VariableKind::Int64 || copy.Type == VariableKind::Uint64)
	{
		copy.Type = VariableKind::Int64;
	}
	else if (copy.Type == VariableKind::Uint32)
	{
		copy.Type = VariableKind::Int32;
	}
	return copy;
}

/// <summary>
/// This returns the object reference from value if it is an address, otherwise the start of the object
/// </summary>
/// <param name="value"></param>
/// <returns></returns>
void* GetRealTargetAddress(const Variable& value)
{
	if (value.Type == VariableKind::AddressOfVariable || value.Type == VariableKind::Uint32 || value.Type == VariableKind::Int32) // Not all operations correctly keep the type of addresses (TODO)
	{
		return value.Object;
	}
	else if (value.Type == VariableKind::Object)
	{
		// This is undocumented, but we seem to need it to make String.Format with value types work correctly
		return AddBytes(value.Object, sizeof(void*)); // Point to the data, not the object header
	}

	throw ExecutionEngineException("Unsupported source type for indirect memory addressing");
}

void FirmataIlExecutor::ClearExecutionStack(VariableDynamicStack* stack)
{
	while (!stack->empty())
	{
		stack->pop();
	}
}

MethodState FirmataIlExecutor::BasicStackInstructions(ExecutionState* currentFrame, uint16_t PC, VariableDynamicStack* stack, VariableVector* locals, VariableVector* arguments,
                                                      OPCODE instr, Variable& value1, Variable& value2, Variable& value3)
{
	Variable intermediate;
	switch (instr)
	{
	case CEE_THROW:
	{
		// Throw empties the execution stack
		ClearExecutionStack(stack);
		ClassDeclaration* exceptionType = GetClassDeclaration(value1);
		Variable messageField = GetField(exceptionType, value1, 0); // Message pointer
		char* cstr = GetAsUtf8String(messageField);
		currentFrame->UpdatePc(PC);
		Firmata.sendStringf(F("Exception thrown at 0x%x in 0x%x: %s"), PC, currentFrame->_executingMethod->methodToken, cstr);
		free(cstr);
		throw CustomClrException(value1, exceptionType->ClassToken);
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

	case CEE_ADD_OVF:
	{
		bool fail = false;
		switch (value1.Type)
		{
		case VariableKind::Int32:
		case VariableKind::Uint32:
		case VariableKind::AddressOfVariable:
		case VariableKind::NativeHandle:
		case VariableKind::Object:
			fail = sadd_overflow(value1.Int32, value2.Int32, &intermediate.Int32);
			intermediate.Type = VariableKind::Int32;
			break;
		case VariableKind::Uint64:\
		case VariableKind::Int64:\
			fail = sadd_overflow(value1.Int64, value2.Int64, &intermediate.Int64);
			intermediate.Type = VariableKind::Int64;
			break;

		default:
			Firmata.sendStringf(F("Comparing value of type %d, Value %d"), value1.Type, value1.Int32);
			throw ClrException("Unsupported case in binary operation", SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);
		}
		if (fail)
		{
			throw ClrException("Integer addition overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		stack->push(intermediate);
		break;
	}
	case CEE_ADD_OVF_UN:
	{
		bool fail = false;
		switch (value1.Type)
		{
		case VariableKind::Int32:
		case VariableKind::Uint32:
		case VariableKind::AddressOfVariable:
		case VariableKind::NativeHandle:
		case VariableKind::Object:
			fail = uadd_overflow(value1.Uint32, value2.Uint32, &intermediate.Uint32);
			intermediate.Type = VariableKind::Uint32;
			break;
		case VariableKind::Uint64:\
		case VariableKind::Int64:\
			fail = uadd_overflow(value1.Uint64, value2.Uint64, &intermediate.Uint64);
			intermediate.Type = VariableKind::Uint64;
			break;

		default:
			Firmata.sendStringf(F("Comparing value of type %d, Value %d"), value1.Type, value1.Int32); 
			throw ClrException("Unsupported case in binary operation", SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken); 
		}
		if (fail)
		{
			throw ClrException("Integer addition overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		stack->push(intermediate);
		break;
	}
	case CEE_ADD:
		// For some of the operations, the result doesn't depend on the whether the variables are signed or not, due to correct overflow
		BinaryOperation(+);
		stack->push(intermediate);
		break;
	case CEE_SUB_OVF:
	{
		bool fail = false;
		switch (value1.Type)
		{
		case VariableKind::Int32:
		case VariableKind::Uint32:
		case VariableKind::AddressOfVariable:
		case VariableKind::NativeHandle:
		case VariableKind::Object:
			fail = ssub_overflow(value1.Int32, value2.Int32, &intermediate.Int32);
			intermediate.Type = VariableKind::Int32;
			break;
		case VariableKind::Uint64:
		case VariableKind::Int64:
			fail = ssub_overflow(value1.Int64, value2.Int64, &intermediate.Int64);
			intermediate.Type = VariableKind::Int64;
			break;

		default:
			Firmata.sendStringf(F("Comparing value of type %d, Value %d"), value1.Type, value1.Int32);
			throw ClrException("Unsupported case in binary operation", SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);
		}
		if (fail)
		{
			throw ClrException("Integer addition overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		stack->push(intermediate);
		break;
	}
	case CEE_SUB_OVF_UN:
	{
		bool fail = false;
		switch (value1.Type)
		{
		case VariableKind::Int32:
		case VariableKind::Uint32:
		case VariableKind::AddressOfVariable:
		case VariableKind::NativeHandle:
		case VariableKind::Object:
			fail = usub_overflow(value1.Uint32, value2.Uint32, &intermediate.Uint32);
			intermediate.Type = VariableKind::Uint32;
			break;
		case VariableKind::Uint64:\
		case VariableKind::Int64:\
			fail = usub_overflow(value1.Uint64, value2.Uint64, &intermediate.Uint64);
			intermediate.Type = VariableKind::Uint64;
			break;

		default:
			Firmata.sendStringf(F("Comparing value of type %d, Value %d"), value1.Type, value1.Int32);
			throw ClrException("Unsupported case in binary operation", SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);
		}
		if (fail)
		{
			throw ClrException("Integer addition overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		stack->push(intermediate);
		break;
	}
	case CEE_SUB:
		BinaryOperation(-);
		stack->push(intermediate);
		break;
	case CEE_MUL_OVF:
	{
		bool fail = false;
		switch (value1.Type)
		{
		case VariableKind::Int32:
		case VariableKind::Uint32:
		case VariableKind::AddressOfVariable:
		case VariableKind::NativeHandle:
		case VariableKind::Object:
			fail = smul_overflow(value1.Int32, value2.Int32, &intermediate.Int32);
			intermediate.Type = VariableKind::Int32;
			break;
		case VariableKind::Uint64:\
		case VariableKind::Int64:\
			fail = smul_overflow(value1.Int64, value2.Int64, &intermediate.Int64);
			intermediate.Type = VariableKind::Int64;
			break;

		default:
			Firmata.sendStringf(F("Comparing value of type %d, Value %d"), value1.Type, value1.Int32);
			throw ClrException("Unsupported case in binary operation", SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);
		}
		if (fail)
		{
			throw ClrException("Integer multiplication overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		stack->push(intermediate);
		break;
	}
	case CEE_MUL_OVF_UN:
	{
		bool fail = false;
		switch (value1.Type)
		{
		case VariableKind::Int32:
		case VariableKind::Uint32:
		case VariableKind::AddressOfVariable:
		case VariableKind::NativeHandle:
		case VariableKind::Object:
			fail = umul_overflow(value1.Uint32, value2.Uint32, &intermediate.Uint32);
			intermediate.Type = VariableKind::Uint32;
			break;
		case VariableKind::Uint64:\
		case VariableKind::Int64:\
			fail = umul_overflow(value1.Uint64, value2.Uint64, &intermediate.Uint64);
			intermediate.Type = VariableKind::Uint64;
			break;

		default:
			Firmata.sendStringf(F("Comparing value of type %d, Value %d"), value1.Type, value1.Int32);
			throw ClrException("Unsupported case in binary operation", SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);
		}
		if (fail)
		{
			throw ClrException("Integer multiplication overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		stack->push(intermediate);
		break;
	}
	case CEE_MUL:
		BinaryOperation(*);
		stack->push(intermediate);
		break;
	case CEE_DIV:
		if (value2.Uint64 == 0)
		{
			throw ClrException(SystemException::DivideByZero, currentFrame->_executingMethod->methodToken);
		}

		if (value1.Type == VariableKind::Int32)
		{
			if (value1.Int32 == 0x7FFFFFFF && value2.Int32 == -1)
			{
				throw ClrException(SystemException::Arithmetic, currentFrame->_executingMethod->methodToken);
			}
		}
		else if (value1.Type == VariableKind::Int64)
		{
			if (value1.Int64 == 0x7FFFFFFFFFFFFFFF && value2.Int64 == -1)
			{
				throw ClrException(SystemException::Arithmetic, currentFrame->_executingMethod->methodToken);
			}
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
			if (value1.Int32 == 0x7FFFFFFF && value2.Int32 == -1)
			{
				throw ClrException(SystemException::Arithmetic, currentFrame->_executingMethod->methodToken);
			}
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
			if (value1.Int64 == 0x7FFFFFFFFFFFFFFF && value2.Int64 == -1)
			{
				throw ClrException(SystemException::Arithmetic, currentFrame->_executingMethod->methodToken);
			}
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
			throw ClrException("Unsupported case in modulo operation", SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken); \
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
			intermediate.Uint32 = value1.Uint32 / value2.Uint32;
			intermediate.Type = VariableKind::Uint32;
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
			intermediate.Uint32 = value1.Uint32 % value2.Uint32;
			intermediate.Type = VariableKind::Uint32;
		}
		else
		{
			intermediate.Uint64 = value1.Uint64 % value2.Uint64;
			intermediate.Type = VariableKind::Uint64;
		}
		
		stack->push(intermediate);
		break;
	case CEE_CGT_UN:
		intermediate.Type = VariableKind::Boolean;
		switch (value1.Type)
		{
		case VariableKind::Object:
		case VariableKind::AddressOfVariable:
		case VariableKind::ReferenceArray:
		case VariableKind::ValueArray:
			intermediate.Boolean = value1.Object > value2.Object;
			break;
		case VariableKind::Int32:
		case VariableKind::RuntimeTypeHandle:
		case VariableKind::Boolean:
		case VariableKind::Uint32:
			intermediate.Boolean = value1.Uint32 > value2.Uint32;
			break;

		case VariableKind::Int64:
		case VariableKind::Uint64:
			intermediate.Boolean = value1.Uint64 > value2.Uint64;
			break;
		case VariableKind::Float:
			intermediate.Boolean = value1.Float > value2.Float;
			break;
		case VariableKind::Double:
			intermediate.Boolean = value1.Double > value2.Double;
			break;
		case VariableKind::Void:
			intermediate.Boolean = value1.Object > value2.Object;
			break;
		default:
			throw ClrException("Unsupported case in comparison operation", SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);
		};
		stack->push(intermediate);
		break;
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
	case CEE_CLT_UN:
		intermediate.Type = VariableKind::Boolean;
		switch (value1.Type)
		{
		case VariableKind::Int32:
			intermediate.Boolean = value1.Uint32 < value2.Uint32;
			break;
		case VariableKind::Object:
		case VariableKind::AddressOfVariable:
		case VariableKind::ReferenceArray:
		case VariableKind::ValueArray:
			intermediate.Boolean = value1.Object < value2.Object;
			break;
		case VariableKind::RuntimeTypeHandle:
		case VariableKind::Boolean:
		case VariableKind::Uint32:
			intermediate.Boolean = value1.Uint32 < value2.Uint32;
			break;
		case VariableKind::Uint64:
			intermediate.Boolean = value1.Uint64 < value2.Uint64;
			break;
		case VariableKind::Int64:
			intermediate.Boolean = value1.Uint64 < value2.Uint64;
			break;
		case VariableKind::Float:
			intermediate.Boolean = value1.Float < value2.Float;
			break;
		case VariableKind::Double:
			intermediate.Boolean = value1.Double < value2.Double;
			break;
		case VariableKind::Void:
			intermediate.Boolean = value1.Object < value2.Object;
			break;
		default:
			throw ClrException("Unsupported case in comparison operation", SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);
		};
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
		[[fallthrough]];
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
			int8_t b = *((int8_t*)GetRealTargetAddress(value1));
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = b;
			stack->push(intermediate);
		}
		break;
	case CEE_LDIND_I2:
		{
			int16_t s = *((int16_t*)GetRealTargetAddress(value1));
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = s;
			stack->push(intermediate);
		}
		break;
	case CEE_LDIND_I4:
		{
			int32_t i = *((int32_t*)GetRealTargetAddress(value1));
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = i;
			stack->push(intermediate);
		}
		break;
	case CEE_LDIND_I8:
		{
			int64_t i8 = *((int64_t*)GetRealTargetAddress(value1));
			intermediate.Type = VariableKind::Int64;
			intermediate.Int64 = i8;
			stack->push(intermediate);
			break;
		}
	case CEE_LDIND_R8:
	{
		double r8 = *((double*)GetRealTargetAddress(value1));
		intermediate.Type = VariableKind::Double;
		intermediate.Double = r8;
		stack->push(intermediate);
		break;
	}
	case CEE_LDIND_R4:
	{
		float r4 = *((float*)GetRealTargetAddress(value1));
		intermediate.Type = VariableKind::Float;
		intermediate.Float = r4;
		stack->push(intermediate);
		break;
	}
	case CEE_LDIND_U1:
		{
			// Weird: The definition says that this loads as Int32 as well (and therefore does a sign-extension)
			byte b = *((byte*)GetRealTargetAddress(value1));
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = b;
			stack->push(intermediate);
		}
		break;
	case CEE_LDIND_U2:
		{
			uint16_t s = *((uint16_t*)GetRealTargetAddress(value1));
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = s;
			stack->push(intermediate);
		}
		break;
	case CEE_LDIND_U4:
		{
			uint32_t i = *((uint32_t*)GetRealTargetAddress(value1));
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = i;
			stack->push(intermediate);
		}
		break;
	case CEE_LDIND_REF:
		{
			uint32_t* pTarget = (uint32_t*)GetRealTargetAddress(value1);
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
	case CEE_STIND_I2:
	{
		// Store a byte (i.e. a bool) to the place where value1 points to
		int16_t* pTarget = (int16_t*)value1.Object;
		*pTarget = (int16_t)value2.Uint32;
	}
	break;
	case CEE_STIND_I:
	case CEE_STIND_I4:
	{
		// Store a byte (i.e. a bool) to the place where value1 points to
		int32_t* pTarget = (int32_t*)value1.Object;
		*pTarget = value2.Int32;
	}
	break;
	case CEE_STIND_R4:
	{
		// Store a byte (i.e. a bool) to the place where value1 points to
		float* pTarget = (float*)value1.Object;
		*pTarget = value2.Float;
	}
	break;
	case CEE_STIND_R8:
	{
		// Store a byte (i.e. a bool) to the place where value1 points to
		double* pTarget = (double*)value1.Object;
		*pTarget = value2.Double;
	}
	break;
	case CEE_STIND_I8:
	{
		// Store a byte (i.e. a bool) to the place where value1 points to
		int64_t* pTarget = (int64_t*)value1.Object;
		*pTarget = value2.Int64;
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
			throw ClrException("Array Index out of range", SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
		}

		// This can only be a value type (of type short or ushort)
		uint16_t* sPtr = (uint16_t*)data;
		if (instr == CEE_LDELEM_I2)
		{
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = *(sPtr + 6 + index);
			SignExtend(intermediate, 2);
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
			throw ClrException("Index out of range in STELEM.I2 operation", SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
			return MethodState::Aborted;
		}

		// This can only be a value type (of type short or ushort)
		uint16_t* sPtr = (uint16_t*)data;
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
			throw ClrException("Index out of range in LDELEM.I1 operation", SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
			return MethodState::Aborted;
		}

		// This can only be a value type (of type byte or sbyte)
		byte* bytePtr = (byte*)data;
		byte value = *AddBytes(bytePtr, ARRAY_DATA_START + index);
		if (instr == CEE_LDELEM_I1)
		{
			intermediate.Type = VariableKind::Int32;
			intermediate.Int32 = value; // Does this properly sign-extend?
		}
		else
		{
			intermediate.Type = VariableKind::Uint32;
			intermediate.Uint32 = value & 0xFF;
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
			throw ClrException("Index out of range in STELEM.I1 operation", SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
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
				throw ClrException("Index out of range in LDELEM.I4 operation", SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
			}

			// Note: Here, size of Variable is equal size of pointer, but this doesn't hold for the other LDELEM variants
			if (value1.Type == VariableKind::ValueArray)
			{
				if (instr == CEE_LDELEM_I4)
				{
					intermediate.Type = VariableKind::Int32;
					intermediate.Int32 = *(data + ARRAY_DATA_START/4 + index);
				}
				else
				{
					intermediate.Type = VariableKind::Uint32;
					intermediate.Uint32 = *(data + ARRAY_DATA_START/4 + index);
				}

				stack->push(intermediate);
			}
			else
			{
				Variable r(VariableKind::Object);
				r.Uint32 = *(data + ARRAY_DATA_START/4 + index);
				stack->push(r);
			}
		}
		break;
	case CEE_LDELEM_I8:
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
			throw ClrException("Index out of range in LDELEM.I8 operation", SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
		}

		if (value1.Type == VariableKind::ValueArray)
		{
			intermediate.Type = VariableKind::Int64;
			intermediate.Int64 = *AddBytes(data, ARRAY_DATA_START + index * 8);

			stack->push(intermediate);
		}
		else
		{
			throw ClrException("Unsupported operation: LDELEM.i8 with a reference array", SystemException::NotSupported, currentFrame->_executingMethod->methodToken);
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
			throw ClrException("Index out of range in STELEM.I4 operation", SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
		}

		if (value1.Type == VariableKind::ValueArray)
		{
			*(data + ARRAY_DATA_START/4 + index) = value3.Int32;
		}
		else
		{
			if (instr == CEE_STELEM_REF && (value3.Type != VariableKind::Object && value3.Type != VariableKind::ValueArray && value3.Type != VariableKind::ReferenceArray))
			{
				// STELEM.ref shall throw if the value type doesn't match the array type. We don't test the dynamic type, but
				// at least it should be a reference
				throw ClrException("Array type mismatch", SystemException::ArrayTypeMismatch, currentFrame->_executingMethod->methodToken);
			}
			// can only be an object now
			*(data + ARRAY_DATA_START/4 + index) = (uint32_t)value3.Object;
		}
	}
	break;
	case CEE_CONV_OVF_I1_UN:
		intermediate = MakeUnsigned(value1);
		if (!FitsIn<int8_t, true, 0, 127>(intermediate))
		{
			throw ClrException("Integer overflow converting to signed byte", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		goto CEE_CONV_I1_LABEL;

	case CEE_CONV_OVF_I1:
		if (!FitsIn<int8_t, true, -128, 127>(value1))
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		[[fallthrough]];
		// Fall trough
	case CEE_CONV_I1:
	{
			CEE_CONV_I1_LABEL:
		intermediate.Type = VariableKind::Int32;
		// This first truncates to 8 bit and then sign-extends
		int64_t v = (value1.Int64 & 0x00FF);
		if (v >= 0x80)
		{
			v |= ~0x00FF;
		}
		switch (value1.Type)
		{
		case VariableKind::Int32:
			intermediate.Int32 = (int)v;
			break;
		case VariableKind::Uint32:
			intermediate.Int32 = (int)v;
			break;
		case VariableKind::Int64:
			intermediate.Int32 = (int)v;
			break;
		case VariableKind::Float:
			intermediate.Int32 = (byte)value1.Float;
			break;
		case VariableKind::Double:
			intermediate.Int32 = (byte)value1.Double;
			break;
		case VariableKind::AddressOfVariable:
			// If it was an address, keep that designation (this converts from Intptr to Uintptr, which is mostly a no-op)
			intermediate.Int32 = (int32_t)v;
			intermediate.Type = VariableKind::AddressOfVariable;
			break;
		default: // The conv statement never throws
			intermediate.Int32 = (int32_t)v;
			break;
		}
	}
		stack->push(intermediate);
		break;
	case CEE_CONV_OVF_U1_UN:
		intermediate = MakeUnsigned(value1);
		if (!FitsIn<uint8_t, false, 0, 255>(intermediate))
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		goto CEE_CONV_U1_LABEL;
	case CEE_CONV_OVF_U1:
		if (!FitsIn<uint8_t, false, 0, 255>(value1))
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		[[fallthrough]];
		// Fall trough
	case CEE_CONV_U1:
	{
			CEE_CONV_U1_LABEL:
		intermediate.Type = VariableKind::Uint32;
		// This truncates to 8 bit
		uint64_t v = (value1.Uint64 & 0x00FF);
		switch (value1.Type)
		{
		case VariableKind::Int32:
			intermediate.Uint32 = (unsigned int)v;
			break;
		case VariableKind::Uint32:
			intermediate.Uint32 = (unsigned int)v;
			break;
		case VariableKind::Int64:
			intermediate.Uint32 = (unsigned int)v;
			break;
		case VariableKind::Float:
			intermediate.Uint32 = (uint8_t)value1.Float;
			break;
		case VariableKind::Double:
			intermediate.Uint32 = (uint8_t)value1.Double;
			break;
		case VariableKind::AddressOfVariable:
			// If it was an address, keep that designation (this converts from Intptr to Uintptr, which is mostly a no-op)
			intermediate.Uint32 = (int32_t)v;
			intermediate.Type = VariableKind::AddressOfVariable;
			break;
		default: // The conv statement never throws
			intermediate.Uint32 = (uint8_t)v;
			break;
		}
		stack->push(intermediate);
		break;
	}
	case CEE_CONV_OVF_I2_UN:
		intermediate = MakeUnsigned(value1);
		if (!FitsIn<int16_t, true, 0, 32767>(intermediate))
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		goto CEE_CONV_I2_LABEL;
		// Fall trough
	case CEE_CONV_OVF_I2:
		if (!FitsIn<int16_t, true, -32768, 32767>(value1))
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		[[fallthrough]];
		// Fall trough
	case CEE_CONV_I2:
		{
			CEE_CONV_I2_LABEL:
			intermediate.Type = VariableKind::Int32;
			// This first truncates to 16 bit and then sign-extends
			int64_t v = (value1.Int64 & 0xFFFF);
			if (v >= 0x8000)
			{
				v |= ~0xFFFF;
			}
			switch (value1.Type)
			{
			case VariableKind::Int32:
				intermediate.Int32 = (int)v;
				break;
			case VariableKind::Uint32:
				intermediate.Int32 = (int)v;
				break;
			case VariableKind::Int64:
				intermediate.Int32 = (int)v;
				break;
			case VariableKind::Float:
				intermediate.Int32 = (short)value1.Float;
				break;
			case VariableKind::Double:
				intermediate.Int32 = (short)value1.Double;
				break;
			case VariableKind::AddressOfVariable:
				// If it was an address, keep that designation (this converts from Intptr to Uintptr, which is mostly a no-op)
				intermediate.Int32 = (int32_t)v;
				intermediate.Type = VariableKind::AddressOfVariable;
				break;
			default: // The conv statement never throws
				intermediate.Int32 = (int32_t)v;
				break;
			}
		}
		stack->push(intermediate);
		break;
	case CEE_CONV_OVF_U2_UN:
		intermediate = MakeUnsigned(value1);
		if (!FitsIn<uint16_t, false, 0, 0xFFFF>(intermediate))
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		goto CEE_CONV_U2_LABEL;
		// Fall trough
	case CEE_CONV_OVF_U2:
		if (!FitsIn<uint16_t, false, 0, 0xFFFF>(value1))
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		[[fallthrough]];
		// Fall trough
	case CEE_CONV_U2:
	{
		CEE_CONV_U2_LABEL:
		intermediate.Type = VariableKind::Uint32;
		// This first truncates to 16 bit and then zero-extends
		uint64_t v = (value1.Uint64 & 0xFFFF);
		switch (value1.Type)
		{
		case VariableKind::Int32:
			intermediate.Uint32 = (unsigned int)v;
			break;
		case VariableKind::Uint32:
			intermediate.Int32 = (unsigned int)v;
			break;
		case VariableKind::Int64:
			intermediate.Int32 = (unsigned int)v;
			break;
		case VariableKind::Float:
			intermediate.Int32 = (uint16_t)value1.Float;
			break;
		case VariableKind::Double:
			intermediate.Int32 = (uint16_t)value1.Double;
			break;
		case VariableKind::AddressOfVariable:
			// If it was an address, keep that designation (this converts from Intptr to Uintptr, which is mostly a no-op)
			intermediate.Int32 = (int32_t)v;
			intermediate.Type = VariableKind::AddressOfVariable;
			break;
		default: // The conv statement never throws
			intermediate.Uint32 = (uint16_t)v;
			break;
		}
		stack->push(intermediate);
		break;
	}
	case CEE_CONV_OVF_I_UN:
	case CEE_CONV_OVF_I4_UN:
		intermediate = MakeUnsigned(value1);
		if (!FitsIn<int32_t, true, 0, 0x7FFFFFFF>(intermediate))
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		goto CEE_CONV_I4_LABEL;
		// Fall trough
	case CEE_CONV_OVF_I4:
		if (!FitsIn<int32_t, true, -2147483648, 2147483647>(value1))
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		[[fallthrough]];
		// Fall trough
	// Fall trough
	// Luckily, the C++ compiler takes over the actual magic happening in these conversions
	case CEE_CONV_I:
	case CEE_CONV_I4:
		CEE_CONV_I4_LABEL:
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
	case CEE_CONV_OVF_U_UN:
	case CEE_CONV_OVF_U4_UN:
		intermediate = MakeUnsigned(value1);
		if (!FitsIn<uint32_t, false, 0, 0xFFFFFFFF>(intermediate))
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		goto CEE_CONV_U4_LABEL;
		// Fall trough
	case CEE_CONV_OVF_U4:
		if (!FitsIn<uint32_t, false, 0, 0xFFFFFFFF>(value1))
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		[[fallthrough]];
		// Fall trough
	case CEE_CONV_U:
	case CEE_CONV_U4:
		CEE_CONV_U4_LABEL:
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
	case CEE_CONV_OVF_I8_UN:
		intermediate = MakeUnsigned(value1);
		if (!FitsIn<int64_t, true, 0, 9223372036854775807>(intermediate))
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		goto CEE_CONV_I8_LABEL;
		// Fall trough
	case CEE_CONV_OVF_I8:
		if (!FitsIn<int64_t, true, -9223372036854775807, 9223372036854775807>(value1)) // There appears to be a problem with assigning the largest negative int64 value to a constant
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		[[fallthrough]];
		// Fall trough
	case CEE_CONV_I8:
		CEE_CONV_I8_LABEL:
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
	case CEE_CONV_OVF_U8_UN:
		intermediate = MakeUnsigned(value1);
		if (!FitsIn<uint64_t, true, 0, 18446744073709551615>(intermediate))
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		goto CEE_CONV_U8_LABEL;
		// Fall trough
	case CEE_CONV_OVF_U8:
		if (!FitsIn<uint64_t, true, 0, 18446744073709551615>(value1))
		{
			throw ClrException("Integer overflow", SystemException::Overflow, currentFrame->_executingMethod->methodToken);
		}
		[[fallthrough]];
		// Fall trough
	case CEE_CONV_U8:
		CEE_CONV_U8_LABEL:
		intermediate.Type = VariableKind::Uint64;
		switch (value1.Type)
		{
		case VariableKind::Int32:
			intermediate.Uint64 = value1.Uint32; // This should zero-extend
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
			intermediate.Double = value1.Int32; // The source type is always expected to be signed
			break;
		case VariableKind::Int64:
			intermediate.Double = (double)value1.Int64;
			break;
		case VariableKind::Uint64:
			intermediate.Double = (double)value1.Int64;
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
			intermediate.Float = (float)value1.Int32; // Source type is always assumed to be signed.
			break;
		case VariableKind::Int64:
			intermediate.Float = (float)value1.Int64;
			break;
		case VariableKind::Uint64:
			intermediate.Float = (float)value1.Int64;
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
	case CEE_LOCALLOC:
		{
		Variable temp;
		temp.Marker = VARIABLE_DECLARATION_MARKER;
		temp.Type = VariableKind::LargeValueType;
		temp.setSize((uint16_t)value1.Int32);
		Variable& newStuff = currentFrame->_localStorage.insert(0, temp);
		intermediate.Type = VariableKind::AddressOfVariable; // Unmanaged pointer
		intermediate.Object = &newStuff.Int32;
		intermediate.Marker = VARIABLE_DEFAULT_MARKER;
		intermediate.setSize(4);
		stack->push(intermediate);
		}
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
	ClassDeclaration* ty = _classes.GetClassWithToken(tokenOfArrayType);
	uint32_t* data;
	uint64_t sizeToAllocate;
	if (ty->IsValueType())
	{
		
		// Value types are stored directly in the array. Element 0 (of type int32) will contain the array type token (since arrays are also objects), index 1 the array length,
		// Index 2 is the array content type token
		// For value types, ClassDynamicSize may be smaller than a memory slot, because we don't want to store char[] or byte[] with 64 bits per element
		sizeToAllocate = (uint64_t)ty->ClassDynamicSize * numberOfElements;
		if (sizeToAllocate > INT32_MAX - 64 * 1024)
		{
			result = Variable();
			return 0;
		}
		data = (uint32_t*)AllocGcInstance((uint32_t)(sizeToAllocate + ARRAY_DATA_START));
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
		data = (uint32_t*)AllocGcInstance((uint32_t)(sizeToAllocate + ARRAY_DATA_START));
		result.Type = VariableKind::ReferenceArray;
	}

	if (data == nullptr)
	{
		result = Variable();
		return 0;
	}

	ClassDeclaration* arrType = _classes.GetClassWithToken(KnownTypeTokens::Array);

	uint32_t ptrAsInt = (uint32_t)arrType;
	*data = ptrAsInt; // This crashes the CPU if the alignment of the new block does not match
	*(data + 1)= numberOfElements;
	*(data + 2) = tokenOfArrayType;
	result.Object = data;
	return (int)sizeToAllocate;
}

// This macro only works in the function below. It ensures the stack variable "tempVariable" has enough room to
// store variable of the given size (+ header)
#define EnsureStackVarSize(size)\
	if (tempVariable == nullptr)\
	{\
		tempVariable = (Variable*)alloca((size)  + sizeof(Variable)); /* this is a bit more than what we need, but doesn't matter (is stack memory only) */ \
		sizeOfTemp = size;\
	} else if (sizeOfTemp < (size_t)(size))\
	{\
		tempVariable = (Variable*)alloca((size)  + sizeof(Variable));\
		sizeOfTemp = size;\
	}\
	tempVariable->Uint64 = 0;

/// <summary>
/// Boxes a value type
/// </summary>
/// <param name="value">Instance of value type to be boxed</param>
/// <param name="ty">Type of value and type of boxed object</param>
/// <returns>An object that represents the boxed variable (of type object)</returns>
Variable FirmataIlExecutor::Box(Variable& value, ClassDeclaration* ty)
{
	// TODO: This requires special handling for types derived from Nullable<T>

	// Here, the boxed size is expected
	size_t sizeOfClass = SizeOfClass(ty);
	TRACE(Firmata.sendString(F("Boxed class size is 0x"), sizeOfClass));
	void* ret = AllocGcInstance(sizeOfClass);

	// Save a reference to the class declaration in the first entry of the newly created instance.
	// this will serve as vtable.
	ClassDeclaration** vtable = (ClassDeclaration**)ret;
	*vtable = ty;
	Variable r;
	r.Marker = VARIABLE_DEFAULT_MARKER;
	r.setSize(4);
	r.Object = ret;
	r.Type = VariableKind::Object;

	// Check the size we need to box. The variable value might be 64 bit, even if the boxed class size can only contain 32 bits (ie. when boxing an enum in InternalBoxEnum)
	int sizeToCopy = ty->ClassDynamicSize;
	if (sizeToCopy > value.fieldSize())
	{
		sizeToCopy = value.fieldSize();
	}
	// Copy the value to the newly allocated boxed instance
	if (value.Type == VariableKind::AddressOfVariable)
	{
		memcpy(AddBytes(ret, sizeof(void*)), value.Object, sizeToCopy);
	}
	else
	{
		memcpy(AddBytes(ret, sizeof(void*)), &value.Int32, sizeToCopy);
	}
	return r;
}

/// <summary>
/// Returns true if the class directly implements the given method. Only returns correct results for value types so far. 
/// </summary>
bool ImplementsMethodDirectly(ClassDeclaration* cls, int32_t methodToken)
{
	int idx = 0;
	for (auto handle = cls->GetMethodByIndex(idx); handle != nullptr; handle = cls->GetMethodByIndex(++idx))
	{
		if (handle->MethodToken() == methodToken)
		{
			return true;
		}

		if (handle->ImplementsMethod(methodToken))
		{
			return true;
		}
	}

	return false;
}

/// <summary>
/// Checks whether a list of arguments possibly matches a method.
/// This is a bit of guesswork, because our type information is to limited to find exact matches.
/// Tested for constructor sonly
/// </summary>
int FirmataIlExecutor::MethodMatchesArgumentTypes(MethodBody* declaration, Variable& argumentArray)
{
	int* data = (int*)argumentArray.Object;
	int argsLen = *(data + 1);
	bool isCtor = false;
	if (declaration->MethodFlags() & (int)MethodFlags::Ctor)
	{
		// A ctor specifies an argument less than what it actually gets
		if (declaration->NumberOfArguments() != argsLen + 1)
		{
			return false;
		}
		isCtor = true;
	}
	else
	{
		if (declaration->NumberOfArguments() != argsLen)
		{
			return false;
		}
	}
	
	for (int idx = 0; idx < argsLen; idx++)
	{
		void* object = (void*)*(data + idx + ARRAY_DATA_START / 4);
		VariableDescription& desc = declaration->GetArgumentAt(idx + isCtor ? 1 : 0);
		ClassDeclaration* ty = (ClassDeclaration*)*((int*)object);
		bool typeMayMatch = false;
		if (ty->IsValueType() && desc.Type != VariableKind::ReferenceArray && desc.Type != VariableKind::AddressOfVariable && desc.Type != VariableKind::Object)
		{
			typeMayMatch = true;
		}
		else if (!ty->IsValueType() && desc.Type == VariableKind::Object)
		{
			typeMayMatch = true;
		}

		if (!typeMayMatch)
		{
			return false;
		}
	}

	return true;
}


uint32_t FirmataIlExecutor::ReadUint32FromArbitraryAddress(byte* pCode)
{
	return static_cast<int32_t>(((uint32_t)pCode[0]) + (((uint32_t)pCode[1]) << 8) + (((uint32_t)pCode[2]) << 16) + (((uint32_t)pCode[3]) << 24));
}

uint16_t FirmataIlExecutor::CreateExceptionFrame(ExecutionState* currentFrame, uint16_t continuationAddress, ExceptionClause* c, Variable &exception)
{
	ExceptionFrame* frame = new ExceptionFrame(c);
	frame->ContinuationPc = continuationAddress;
	ASSERT(exception.Type == VariableKind::Object || exception.Type == VariableKind::Void);
	frame->Exception = exception;
	uint16_t newPc = c->HandlerOffset;
	if (currentFrame->_exceptionFrame == nullptr)
	{
		currentFrame->_exceptionFrame = frame;
	}
	else
	{
		ExceptionFrame* f = currentFrame->_exceptionFrame;
		while (f->Next != nullptr)
		{
			f = f->Next;
		}
		f->Next = frame;
	}
	return newPc;
}

Variable FirmataIlExecutor::CreateStringInstance(size_t length, const char* string)
{
	Variable stringVariable;
	byte* classInstance = (byte*)CreateInstanceOfClass((int)KnownTypeTokens::String, length + 1); // +1 for the terminating 0
	// The string data is stored inline in the class data junk
	uint16_t* stringData = (uint16_t*)AddBytes(classInstance, STRING_DATA_START);
	int i = 0;
	const char* stringIterator = string;
	while(stringIterator < string + length)
	{
		// Several bytes in the input might make up a byte in the output
		stringData[i] = utf8_to_unicode(stringIterator);
		i++;
	}

	length = i; // We waste a bit of memory here, but that's hopefully negligible over the added complexity if we need to calculate the required size in an extra step
	stringData[i] = 0; // Add terminating 0 (the constant array does not include these)
	ClassDeclaration* stringInstance = _classes.GetClassWithToken(KnownTypeTokens::String);
	// Length
	Variable v(VariableKind::Int32);
	v.Int32 = length;
	stringVariable.Type = VariableKind::Object;
	stringVariable.Object = classInstance;
	SetField4(stringInstance, v, stringVariable, 0);
	return stringVariable;
}

// Preconditions for save execution: 
// - codeLength is correct
// - argc matches argList
// - It was validated that the method has exactly argc arguments
MethodState FirmataIlExecutor::ExecuteIlCode(ExecutionState *rootState, Variable* returnValue)
{
	const int NUM_INSTRUCTIONS_AT_ONCE = 50;

	if (_debugBreakActive)
	{
		// This won't change while we're sitting on a breakpoint.
		return MethodState::Running;
	}
	
	ExecutionState* currentFrame = rootState;
	while (currentFrame->_next != NULL)
	{
		currentFrame = currentFrame->_next;
	}

	int instructionsExecutedThisLoop = 0;
	int constrainedTypeToken = 0; // Only used for the CONSTRAINED. prefix
	MethodBody* target = nullptr; // Used for the calli instruction
	uint16_t PC = 0;
	VariableDynamicStack* stack;
	VariableVector* locals;
	VariableVector* arguments;
	
	// Temporary location for a stack variable of arbitrary size. The memory is allocated using alloca() when needed
	Variable* tempVariable = nullptr;
	size_t sizeOfTemp = 0;
	
	currentFrame->ActivateState(&PC, &stack, &locals, &arguments);

	MethodBody* currentMethod = currentFrame->_executingMethod;

	byte* pCode = currentMethod->_methodIl;
	TRACE(u32 startTime = micros());
	try
	{
	// The compiler always inserts a return statement, so we can never run past the end of a method,
	// however we use this counter to interrupt code execution every now and then to go back to the main loop
	// and check for other tasks (i.e. serial input data)
    while (instructionsExecutedThisLoop < NUM_INSTRUCTIONS_AT_ONCE)
    {
#if DEBUGGER

		if (CheckForBreakCondition(currentFrame, PC, pCode))
		{
			currentFrame->UpdatePc(PC);
			SendDebugState(rootState);
			_debugBreakActive = true;
			break; // exit while loop
		}

#endif
    	immediatellyContinue: // Label used by prefix codes, to prevent interruption
		instructionsExecutedThisLoop++;
		
		uint16_t len;
        OPCODE  instr;
		
		TRACE(Firmata.sendStringf(F("PC: 0x%x in Method 0x%lx"), PC, currentMethod->methodToken));

    	if (PC == 0 && (currentMethod->MethodFlags() & (byte)MethodFlags::SpecialMethod))
		{
			NativeMethod specialMethod = currentMethod->NativeMethodNumber();

			TRACE(Firmata.sendString(F("Executing special method "), (int)specialMethod));
			Variable retVal;
    		if (specialMethod == NativeMethod::ActivatorCreateInstance)
    		{
    			// This very special function is directly handled here, because it needs to manipulate the call stack, so that
    			// we can execute the called ctor.
    			// This function has 6 arguments, but most of them are irrelevant or we don't care.
				ASSERT(arguments->size() == 6);
				Variable& type = arguments->at(0);
				ClassDeclaration* typeOfType = GetClassDeclaration(type);
				ASSERT(typeOfType->ClassToken == (int)KnownTypeTokens::Type);
				int typeToken = GetField(typeOfType, type, 0).Int32;
				ClassDeclaration* typeToCreate = GetClassWithToken(typeToken);
    			if (typeToCreate->IsValueType())
    			{
					throw ClrException("Cannot create value types using CreateInstance", SystemException::NotSupported, typeToken);
    			}
    			
				Variable& argsArray = arguments->at(3);
				ASSERT(argsArray.Type == VariableKind::ReferenceArray);
				int* data = (int*)argsArray.Object;
				int argsLen = *(data + 1);
				int idx = 0;
    			
				ExecutionState* newState = nullptr;
				void* newInstance = nullptr;
				for (auto ctor = typeToCreate->GetMethodByIndex(idx); ctor != nullptr; ctor = typeToCreate->GetMethodByIndex(++idx))
				{
					auto declaration = GetMethodByToken(ctor->MethodToken());
					if (declaration == nullptr) // token is not a method
					{
						continue;
					}
					if (declaration->MethodFlags() == (int)MethodFlags::Ctor && MethodMatchesArgumentTypes(declaration, argsArray)) // +1, because a ctor has an implicit this argument
					{
						newInstance = CreateInstanceOfClass(typeToken, 0);
						newState = new ExecutionState(currentFrame->TaskId(), declaration->MaxExecutionStack(), declaration);
						newState->ActivateState(&PC, &stack, &locals, &arguments);
						int argNum = 0;
						for (; argNum < argsLen; argNum++)
						{
							void* ptrToObject = (void*)*(data + ARRAY_DATA_START / 4 + argNum);
							VariableDescription& desc = declaration->GetArgumentAt(argNum + 1);
							ClassDeclaration* ty = (ClassDeclaration*)*((int*)ptrToObject);
							if (desc.Type == VariableKind::Object)
							{
								arguments->at(argNum + 1).Object = ptrToObject;
							}
							else 
							{
								// Since the argument array is of type object[], we know we have to unbox here
								memcpy(&arguments->at(argNum + 1).Int32, AddBytes(ptrToObject, sizeof(void*)), ty->ClassDynamicSize);
							}
						}

						arguments->at(0).Object = newInstance;
						break;
					}
				}

				if (newInstance == nullptr)
				{
					freeEx(newState);
					throw ClrException(SystemException::ClassNotFound, typeToken);
				}

				ExecutionState* frame = rootState; // start at root
				while (frame->_next != currentFrame)
				{
					frame = frame->_next;
				}

				// Remove the last frame and set the PC for the new current frame. This will make it look like the ctor was called normally using a newobj instruction.
				frame->_next = newState;
    			// This is a bit ugly, but we have to get at the stack of our caller to push the new instance (we had already activated the new frame above)
				frame->ActivateState(&PC, &stack, &locals, &arguments);
				Variable v;
				v.Type = VariableKind::Object;
				v.Object = newInstance;
				stack->push(v);
    			// We replace the frame for the Activator method with the new ctor.
				ExecutionState* exitingFrame = currentFrame;
				currentFrame = newState;
    			
				newState->ActivateState(&PC, &stack, &locals, &arguments);
				delete exitingFrame;
    		}
			else
			{
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
				// stack of the calling method.
				if ((currentMethod->MethodFlags() & (byte)MethodFlags::Void) == 0 && (currentMethod->MethodFlags() & (byte)MethodFlags::Ctor) == 0)
				{
					currentFrame->ActivateState(&PC, &stack, &locals, &arguments);
					stack->push(retVal);
				}
				else
				{
					currentFrame->ActivateState(&PC, &stack, &locals, &arguments);
				}

				delete exitingFrame;
			}
    		
			currentMethod = currentFrame->_executingMethod;

			pCode = currentMethod->_methodIl;
			continue;
		}
		
		if (PC >= currentMethod->MethodLength())
		{
			// Except for a hacking attempt, this may happen if a branch instruction missbehaves
			throw ExecutionEngineException("Security violation: Attempted to execute code past end of method");
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
					// stack of the calling method. If it was a ctor, we have already pushed the return value.
					if ((currentMethod->MethodFlags() & (byte)MethodFlags::Void) == 0 && (currentMethod->MethodFlags() & (byte)MethodFlags::Ctor) == 0)
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
					pCode = currentMethod->_methodIl;
					TRACE(Firmata.sendStringf(F("Popped stack back to method 0x%x"), currentMethod->methodToken));
					break;
				}
				else if (instr == CEE_READONLY_)
				{
					// We can ignore this, I think.
					goto immediatellyContinue;
				}
				else if (instr == CEE_ENDFINALLY)
				{
					// Can we try to find another handler around the current block?
					if (currentFrame->_exceptionFrame != nullptr)
					{
						ExceptionFrame* frame = currentFrame->_exceptionFrame;
						ExceptionFrame* previous = nullptr;
						Variable exception;
						// This chain is only required for the rare case where another exception (including handler) sits inside a finally clause.
						while (frame->Next != nullptr)
						{
							previous = frame;
							frame = frame->Next;
						}
						uint16_t nextPc = frame->ContinuationPc;
						exception = frame->Exception;
						if (previous)
						{
							delete previous->Next;
							previous->Next = nullptr;
						}
						else
						{
							delete currentFrame->_exceptionFrame;
							currentFrame->_exceptionFrame = nullptr;
						}

						if (nextPc == 0 && exception.Type == VariableKind::Object)
						{
							// Rethrow
							currentFrame->UpdatePc(PC); // Save current PC, to make sure we don't enter the same handler again
							auto cl = GetClassDeclaration(exception);
							throw CustomClrException(exception, cl->ClassToken);
						}
						else if (nextPc == 0)
						{
							// If we entered the finally using a leave instruction, the target cannot be 0, and otherwise there must be an exception present
							throw ExecutionEngineException("ENDFINALLY without an active exception and no valid PC to continue on");
						}

						// Executes the previous leave instruction (we only get here if we are passing trough a finally after a leave)
						PC = nextPc; 
					}
					else
					{
						throw ExecutionEngineException("ENDFINALLY without an active exception");
					}
					goto immediatellyContinue;
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
						goto immediatellyContinue;
					case CEE_LDC_I4_S:
						intermediate.Int32 = data;
						intermediate.Type = VariableKind::Int32;
						SignExtend(intermediate, 1);
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
				uint32_t v = ReadUint32FromArbitraryAddress(pCode + PC);
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
				// We need to read the 64 bit value as two 32-bit values, since the CPU crashes on unaligned reads of 64 bit values otherwise
				int64_t data = ((int64_t)(ReadUint32FromArbitraryAddress(pCode + PC))) | ((uint64_t) (ReadUint32FromArbitraryAddress(pCode + PC + 4))) << 32; // Little endian!
				PC += 8;
				if (instr == CEE_LDC_I8)
				{
					intermediate.Type = VariableKind::Int64;
					intermediate.Int64 = data;
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
				// We need to read the 64 bit value as two 32-bit values, since the CPU crashes on unaligned reads of 64 bit values otherwise
				int64_t data = ((int64_t)(ReadUint32FromArbitraryAddress(pCode + PC))) | ((uint64_t)(ReadUint32FromArbitraryAddress(pCode + PC + 4))) << 32; // Little endian!
				// Firmata.sendString(F("After 64 bit access"));
				double v = *reinterpret_cast<double*>(&data);
				// Firmata.sendString(F("after conversion access"));
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
				int32_t data = ReadUint32FromArbitraryAddress(pCode + PC);
				float v = *reinterpret_cast<float*>(&data);
				PC += 4;
				if (instr == CEE_LDC_R4)
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
				int32_t leaveSource = -1;
				switch (instr)
				{
					case CEE_LEAVE: 
					case CEE_LEAVE_S:
						{
						intermediate.Boolean = true;
						leaveSource = PC - 1;
						ClearExecutionStack(stack);
						}
						break;
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
						value1 = MakeUnsigned(value1);
						ComparisonOperation(>= );
						break;
					case CEE_BGE:
					case CEE_BGE_S:
						value1 = MakeSigned(value1);
						ComparisonOperation(>= );
						break;
					case CEE_BLE_UN:
					case CEE_BLE_UN_S:
						value1 = MakeUnsigned(value1);
						ComparisonOperation(<= );
						break;
					case CEE_BLE:
					case CEE_BLE_S:
						value1 = MakeSigned(value1);
						ComparisonOperation(<= );
						break;
					case CEE_BGT_UN:
					case CEE_BGT_UN_S:
						value1 = MakeUnsigned(value1);
						ComparisonOperation(> );
						break;
					case CEE_BGT:
					case CEE_BGT_S:
						value1 = MakeSigned(value1);
						ComparisonOperation(> );
						break;
					case CEE_BLT_UN:
					case CEE_BLT_UN_S:
						value1 = MakeUnsigned(value1);
						ComparisonOperation(< );
						break;
					case CEE_BLT:
					case CEE_BLT_S:
						value1 = MakeSigned(value1);
						ComparisonOperation(< );
						break;
					case CEE_BNE_UN:
					case CEE_BNE_UN_S:
						value1 = MakeUnsigned(value1);
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
					int32_t offset = ReadUint32FromArbitraryAddress(pCode + PC);
					int32_t dest = (PC + 4) + offset;
					PC = (short)dest;
				}
				else
				{
					PC += 4;
				}

            	if (leaveSource >= 0)
            	{
            		// We're executing a leave command. Instead of continuing at the new PC, first locate a finally clause and go there.
					currentFrame->UpdatePc(PC);
					ExceptionClause* c = nullptr;
					Variable noException;
					if (LocateCatchHandler(currentFrame, leaveSource, noException, &c))
					{
						PC = CreateExceptionFrame(currentFrame, PC, c, noException);
					}
					// We didn't find a finally clause - continue normally where the leave instruction pointed us to.
            	}
				TRACE(Firmata.sendString(F("Branch instr. Next is "), PC));
				break;
            }
			case InlineField:
	            {
				int32_t token = ReadUint32FromArbitraryAddress(pCode + PC);
				PC += 4;
		            switch(instr)
		            {
		            	// The ldfld instruction loads a field value of an object to the stack
					case CEE_LDFLD:
						{
						Variable& obj = stack->top();
						stack->pop();
						
						VariableDescription desc;
						byte* dataPtr = Ldfld(obj, token, desc);

						// Combine the variable again with its metadata, so we can put it back to the stack
						EnsureStackVarSize(desc.fieldSize());
						tempVariable->setSize(desc.Size);
						tempVariable->Marker = 0x37;
						tempVariable->Type = desc.Type;
						memcpy(&(tempVariable->Int32), dataPtr, desc.Size);
						SignExtend(*tempVariable, desc.Size);
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
							throw ClrException("Null reference exception in STFLD instruction", SystemException::NullReference, currentFrame->_executingMethod->methodToken);
						}
						break;
					}
		            	// Store a static field value on the stack
					case CEE_STSFLD:
						Stsfld(token, stack->top());
						stack->pop();
						break;
		            case CEE_LDSFLD:
					{
						VariableDescription desc;
						byte* dataPtr = Ldsfld(token, desc);

						// Combine the variable again with its metadata, so we can put it back to the stack
						EnsureStackVarSize(desc.fieldSize());
						tempVariable->setSize(desc.Size);
						tempVariable->Marker = 0x37;
						tempVariable->Type = desc.Type;
						memcpy(&(tempVariable->Int32), dataPtr, desc.Size);
						SignExtend(*tempVariable, desc.Size);
						stack->push(*tempVariable);
					}
					break;
					case CEE_LDFLDA:
					// This one is tricky, because it can load both instance and static fields
					{
						Variable& obj = stack->top();
						stack->pop();
						ClassDeclaration* ty = ResolveClassFromFieldToken(token);
						Variable& desc = GetVariableDescription(ty, token);
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
			case InlineSig: // The calli instruction. The sig argument (a token) has currently no meaning for us
			{
				Variable& methodTarget = stack->top();
				stack->pop();
				ASSERT(methodTarget.Type == VariableKind::FunctionPointer);
				target = (MethodBody*)methodTarget.Object;
			}
			[[fallthrough]];
			// FALL TROUGH!
			case InlineMethod:
            {
				if (instr != CEE_CALLVIRT && instr != CEE_CALL && instr != CEE_NEWOBJ && instr != CEE_LDFTN && instr != CEE_CALLI)
				{
					InvalidOpCode(PC, instr);
					return MethodState::Aborted;
				}

				void* newObjInstance = nullptr;

				// This uses this complex addressing because we need this to work independently of any alignment restrictions
				int32_t tk = ReadUint32FromArbitraryAddress(pCode + PC);
				PC += 4;

				// Save return PC
				currentFrame->UpdatePc(PC);

				MethodBody* newMethod = nullptr;
				if (instr == CEE_CALLI)
				{
					newMethod = target;
				}
				else
				{
					newMethod = GetMethodByToken(tk);
				}
            	if (newMethod == nullptr)
				{
					Firmata.sendString(F("Unknown token 0x"), tk);
					throw ClrException("Unknown target method token in call instruction", SystemException::MissingMethod, tk);
				}

				ClassDeclaration* cls = nullptr;
				if (instr == CEE_CALLVIRT || (instr == CEE_CALLI && (newMethod->MethodFlags() & (int)MethodFlags::Virtual)))
				{
					// For a virtual call, we need to grab the instance we operate on from the stack.
					// The this pointer for the new method is the object that is last on the stack, so we need to use
					// the argument count. Fortunately, this also works if so far we only have the abstract base.
					Variable& instance = stack->nth(newMethod->NumberOfArguments() - 1); // numArgs includes the this pointer

					// The constrained prefix just means that it may be a reference. We'll have to check its type
					// TODO: Add test that checks the different cases
					if (constrainedTypeToken != 0)
					{
						ASSERT(instance.Type == VariableKind::AddressOfVariable);
						// The reference points to an instance of type constrainedTypeToken
						cls = _classes.GetClassWithToken(constrainedTypeToken);
						if (cls->IsValueType())
						{
							// This will be the this pointer for a method call on a value type (we'll have to do a real boxing if the
							// callee is virtual (can be the methods ToString(), GetHashCode() or Equals() - that is, one of the virtual methods of
							// System.Object, System.Enum or System.ValueType OR a method implementing an interface)
							if (!ImplementsMethodDirectly(cls, newMethod->methodToken))
							{
								// Box. Actually a rare case.
								instance = Box(instance, cls);
							}
							// otherwise just passes the reference unmodified as this pointer (the this pointer on a value type method call is 
							
							/* if (newMethod->methodFlags & (int)MethodFlags::Virtual)
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
							}*/
						}
						else
						{
							// Dereference the instance
							instance.Object = reinterpret_cast<void*>(*(int*)(instance.Object));
							instance.Type = VariableKind::Object;
						}
					}
					
					if (instance.Object == nullptr)
					{
						throw ClrException("Null reference exception calling virtual method", SystemException::NullReference, tk);
					}

					if (cls == nullptr)
					{
						// The vtable is actually a pointer to the class declaration and at the beginning of the object memory
						byte* o = (byte*)instance.Object;
						// Get the first data element of where the object points to
						cls = ((ClassDeclaration*)(*(int*)o));
					}

					TRACE(Firmata.sendStringf(F("Callvirt on instance of class %lx"), cls->ClassToken));

					if (instance.Type != VariableKind::Object && instance.Type != VariableKind::ValueArray &&
						instance.Type != VariableKind::ReferenceArray && instance.Type != VariableKind::AddressOfVariable)
					{
						throw ClrException("Virtual function call on something that is not an object", SystemException::InvalidCast, currentFrame->_executingMethod->methodToken);
					}

					while (cls->ParentToken >= 1) // System.Object does not inherit methods from anywhere
					{
						int idx = 0;
						for (auto met = cls->GetMethodByIndex(idx); met != nullptr; met = cls->GetMethodByIndex(++idx))
						{
							// The method is being called using the static type of the target
							int metToken = met->MethodToken();
							if (metToken == newMethod->methodToken)
							{
								goto outer;
							}

							if (met->ImplementsMethod(newMethod->methodToken))
							{

								newMethod = GetMethodByToken(metToken);
								if (newMethod == nullptr)
								{
									throw ClrException("Implementation for token not found", SystemException::MissingMethod, metToken);
								}
								goto outer;

							}
						}

						// Our metadata does not include virtual methods that are not overridden in the current class (but only in the base), therefore we have to go down
						cls = _classes.GetClassWithToken(cls->ParentToken);
					}
					// We didn't find another method to call - we'd better already point to the right one
				}
				outer:
				// Call to an abstract base class or an interface method - if this happens,
				// we've probably not done the virtual function resolution correctly
				if ((int)newMethod->MethodFlags() & (int)MethodFlags::Abstract)
				{
					throw ClrException("Call to abstract method", SystemException::MissingMethod, newMethod->methodToken);
				}

				if (instr == CEE_NEWOBJ)
				{
					cls = ResolveClassFromCtorToken(newMethod->methodToken);

					if (cls->IsValueType())
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
					else if (cls->ClassToken == (int)KnownTypeTokens::String)
					{
						// Strings are rarely created using NEWOBJ, but there are some ctors that are actually called directly
						if (newMethod->MethodFlags() & (int)MethodFlags::SpecialMethod && newMethod->NativeMethodNumber() == NativeMethod::StringCtorSpan)
						{
							// The ctor MiniString(ReadOnlySpan<char>) was called
							Variable& span = stack->top();
							int length = *AddBytes(&span.Int32, 4);
							newObjInstance = CreateInstanceOfClass(cls->ClassToken, length + 1);
							*AddBytes((int*)newObjInstance, 4) = length;
							short* dataPtr = (short*)*AddBytes(&span.Object, 0);
							// Copy data from span to string
							for (int c = 0; c < length; c++)
							{
								*AddBytes((short*)newObjInstance, 8 + c * sizeof(short)) = *dataPtr;
								dataPtr++;
							}
						}
						else if (newMethod->MethodFlags() & (int)MethodFlags::SpecialMethod && newMethod->NativeMethodNumber() == NativeMethod::StringCtorCharCount)
						{
							// The ctor MiniString(char c, int count) was called
							int length = stack->top().Int32;
							int c = stack->nth(1).Int32;
							newObjInstance = CreateInstanceOfClass(cls->ClassToken, length + 1);
							*AddBytes((int*)newObjInstance, 4) = length;
							for (int i = 0; i < length; i++)
							{
								*AddBytes((uint16_t*)newObjInstance, STRING_DATA_START + i * SIZEOF_CHAR) = (uint16_t)c;
							}
						}
						else if (newMethod->MethodFlags() & (int)MethodFlags::SpecialMethod && newMethod->NativeMethodNumber() == NativeMethod::StringCtorCharArray)
						{
							// The ctor MiniString(char[] value, int startIndex, int length) was called
							int length = stack->top().Int32;
							int startIndex = stack->nth(1).Int32;
							uint16_t* value = (uint16_t*)stack->nth(2).Object;
							int sourceLen = *AddBytes((int*)value, 4);
							if (sourceLen < length + startIndex)
							{
								throw ClrException("Array size out of bounds", SystemException::IndexOutOfRange, newMethod->methodToken);
							}
							
							value = AddBytes(value, ARRAY_DATA_START);
							newObjInstance = CreateInstanceOfClass(cls->ClassToken, length + 1);
							*AddBytes((int*)newObjInstance, 4) = length;
							for (int i = 0; i < length; i++)
							{
								*AddBytes((uint16_t*)newObjInstance, STRING_DATA_START + i * SIZEOF_CHAR) = value[startIndex + i];
							}
						}
						else if (newMethod->MethodFlags() & (int)MethodFlags::SpecialMethod && newMethod->NativeMethodNumber() == NativeMethod::StringCtorCharPtr3)
						{
							// The ctor MiniString(char* value, int startIndex, int length) was called
							int length = stack->top().Int32;
							int startIndex = stack->nth(1).Int32;
							uint16_t* value = (uint16_t*)stack->nth(2).Object;
							newObjInstance = CreateInstanceOfClass(cls->ClassToken, length + 1);
							*AddBytes((int*)newObjInstance, 4) = length;
							for (int i = 0; i < length; i++)
							{
								*AddBytes((uint16_t*)newObjInstance, STRING_DATA_START + i * SIZEOF_CHAR) = value[startIndex + i];
							}
						}
						else if (newMethod->MethodFlags() & (int)MethodFlags::SpecialMethod && newMethod->NativeMethodNumber() == NativeMethod::StringCtorCharPtr)
						{
							// The ctor MiniString(char* value) was called
							uint16_t* value = (uint16_t*)stack->top().Object;
							size_t length = wcslen((wchar_t*)value);
							newObjInstance = CreateInstanceOfClass(cls->ClassToken, length + 1);
							*AddBytes((int*)newObjInstance, 4) = length;
							for (size_t i = 0; i < length; i++)
							{
								*AddBytes((uint16_t*)newObjInstance, STRING_DATA_START + i * SIZEOF_CHAR) = value[i];
							}
						}
						else
						{
							throw ClrException("Unsupported string ctor called", SystemException::MissingMethod, newMethod->methodToken);
						}
					}
					else
					{
						newObjInstance = CreateInstance(cls);
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

				if (instr == CEE_LDFTN)
				{
					Variable ftptr;
					ftptr.setSize(sizeof(void*));
					ftptr.Marker = VARIABLE_DEFAULT_MARKER;
					ftptr.Type = VariableKind::FunctionPointer;
					ftptr.Uint64 = 0; // We may later need the top word for the object reference (CEE_LDVIRTFTN instruction)
					ftptr.Object = newMethod;
					stack->push(ftptr);
					break;
				}
								
				uint16_t argumentCount = newMethod->NumberOfArguments();
				// While generating locals, assign their types (or a value used as out parameter will never be correctly typed, causing attempts
				// to calculate on void types)
				ExecutionState* newState = new ExecutionState(currentFrame->TaskId(), newMethod->MaxExecutionStack(), newMethod);
				if (newState == nullptr)
				{
					// Could also send a stack overflow exception here, but the reason is the same
					OutOfMemoryException::Throw("Out of memory to create stack frame");
				}
				currentFrame->_next = newState;
				
				VariableDynamicStack* oldStack = stack;
				// Start of the called method
				currentFrame = newState;
				currentFrame->ActivateState(&PC, &stack, &locals, &arguments);

            	// Load data pointer for the new method
				currentMethod = newMethod;
				pCode = newMethod->_methodIl;

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
					if (cls->IsValueType())
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
					if (instr == CEE_CALLI && newMethod->MethodFlags() & (int)MethodFlags::Static)
					{
						// If the target method of a calli instruction is static, we drop argument 0
						while (argumentCount > 1)
						{
							argumentCount--;
							Variable& v = oldStack->top();
							// This calls operator =, potentially copying more than sizeof(Variable)
							arguments->at(argumentCount - 1) = v;
							
							oldStack->pop();
						}

						// drop the last element off the stack (is likely only a nullreference anyway)
						oldStack->pop();
					}
					else
					{
						while (argumentCount > 0)
						{
							argumentCount--;
							Variable& v = oldStack->top();
							////if (argumentCount == 0 && instr == CEE_CALLVIRT && v.Type == VariableKind::Object && cls != nullptr && cls->IsValueType())
							////{
							////	// If we're calling into a value type method with an interface (boxed object) that has not been converted to an address, we need to do this now
							////	// I cannot find where this should happen, but string.Format() on built-in value types is failing without this
							////	Variable v2;
							////	v2.Object = &v.Object;
							////	v2.Type = VariableKind::AddressOfVariable;
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
				}

				target = nullptr;
				constrainedTypeToken = 0;
				TRACE(Firmata.sendStringf(F("Pushed stack to method 0x%x"), currentMethod->methodToken));
				break;
            }
			case InlineType:
			{
				int token = ReadUint32FromArbitraryAddress(pCode + PC);
				PC += 4;
				int size;
				switch(instr)
				{
				case CEE_NEWARR:
				{
					size = stack->top().Int32;
					stack->pop();

					Variable v1;
					AllocateArrayInstance(token, size, v1);
					if (v1.Type == VariableKind::Void)
					{
						OutOfMemoryException::Throw("Not enough memory to allocate array");
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
							throw ClrException("Array type mismatch - Type of array does not match element to store", SystemException::ArrayTypeMismatch, currentFrame->_executingMethod->methodToken);
						}

						ClassDeclaration* elemTy = _classes.GetClassWithToken(token);
						int32_t index = value2.Int32;
						if (index < 0 || index >= arraysize)
						{
							throw ClrException("Array index out of range", SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
						}

						int sizeOfElement = elemTy->ClassDynamicSize;
						if (!elemTy->IsValueType())
						{
							// If the element to store is a reference, we expect a reference array. If the class around us is generic, the compiler might have inserted STELEM, even though STELEM.ref would be better
							if (value1.Type == VariableKind::ValueArray)
							{
								throw ClrException("Array type mismatch - storing reference type in value array?", SystemException::ArrayTypeMismatch, elemTy->ClassToken);
							}
							sizeOfElement = 4;
						}
						
						switch(sizeOfElement)
						{
						case 1:
						{
							byte* dataptr = (byte*)data;
							*(dataptr + ARRAY_DATA_START + index) = (byte)value3.Int32;
							break;
						}
						case 2:
						{
							short* dataptr = (short*)data;
							*(dataptr + ARRAY_DATA_START / 2 + index) = (short)value3.Int32;
							break;
						}
						case 4:
						{
							*(data + ARRAY_DATA_START / 4 + index) = value3.Int32;
							break;
						}
						case 8:
						{
							uint64_t* dataptr = (uint64_t*)data;
							memcpy(AddBytes(dataptr, ARRAY_DATA_START + 8 * index), &value3.Int64, 8);
							break;
						}
						default: // Arbitrary size of the elements in the array
						{
							byte* dataptr = (byte*)data;
							byte* targetPtr = AddBytes(dataptr, ARRAY_DATA_START + (elemTy->ClassDynamicSize * index));
							memcpy(targetPtr, &value3.Int32, elemTy->ClassDynamicSize);
							break;
						}
						case 0: // That's fishy
							throw ClrException("Cannot address array with element size 0", SystemException::ArrayTypeMismatch, token);
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

					uint32_t* data = (uint32_t*)value1.Object;
					int32_t arraysize = *(data + 1);
					int32_t targetType = *(data + 2);
					if (token != targetType)
					{
						throw ClrException("Array element type does not match type to add", SystemException::ArrayTypeMismatch, currentFrame->_executingMethod->methodToken);
					}
					int32_t index = value2.Int32;
					if (index < 0 || index >= arraysize)
					{
						throw ClrException("Array index out of range", SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
					}

					// This should always exist
					ClassDeclaration* elemTy = _classes.GetClassWithToken(token);
					if (value1.Type == VariableKind::ValueArray)
					{
						EnsureStackVarSize(elemTy->ClassDynamicSize);
						tempVariable->Marker = VARIABLE_DEFAULT_MARKER;
						switch (elemTy->ClassDynamicSize)
						{
						case 1:
						{
							byte* dataptr = (byte*)data;
							tempVariable->Type = VariableKind::Int32;
							tempVariable->Int32 = *(dataptr + ARRAY_DATA_START + index);
							tempVariable->setSize(4);
							break;
						}
						case 2:
						{
							short* dataptr = (short*)data;
							tempVariable->Type = VariableKind::Int32;
							tempVariable->Int32 = *(dataptr + ARRAY_DATA_START/2 + index);
							tempVariable->setSize(4);
							break;
						}
						case 4:
						{
							tempVariable->Type = VariableKind::Int32;
							tempVariable->Int32 = *(data + ARRAY_DATA_START/4 + index);
							tempVariable->setSize(4);
							break;
						}
						case 8:
						{
							tempVariable->Type = VariableKind::Int64;
							memcpy(&tempVariable->Int64, AddBytes(data, ARRAY_DATA_START + 8 * index), 8);
							tempVariable->setSize(8);
							break;
						}
						default:
							byte* dataptr = (byte*)data;
							byte* srcPtr = AddBytes(dataptr, ARRAY_DATA_START + (elemTy->ClassDynamicSize * index));
							tempVariable->setSize(elemTy->ClassDynamicSize);
							tempVariable->Type = elemTy->ClassDynamicSize <= 8 ? VariableKind::Int64 : VariableKind::LargeValueType;
							memcpy(&tempVariable->Int32, srcPtr, elemTy->ClassDynamicSize);
							break;
						}
						stack->push(*tempVariable);
						
					}
					else
					{
						// can only be a reference type now (either an object, or another array)
						Variable v1;
						v1.Marker = VARIABLE_DEFAULT_MARKER;
						v1.Object = (void*)*(data + ARRAY_DATA_START/4 + index);
						v1.Type = VariableKind::Object;
						v1.setSize(4);
						if (elemTy->IsArray())
						{
							// If the inner type is an array, we need to find wether it's a value or a reference array (or any further operations on this inner array don't
							// see the right variable type)
							int arrayTypeToken = *AddBytes((int*)v1.Object, 8);
							ClassDeclaration* innerArrayType = GetClassWithToken(arrayTypeToken);
							if (innerArrayType->IsValueType())
							{
								v1.Type = VariableKind::ValueArray;
							}
							else
							{
								v1.Type = VariableKind::ReferenceArray;
							}
						}
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
						throw ClrException("Array element type does not match type to add", SystemException::ArrayTypeMismatch, currentFrame->_executingMethod->methodToken);
					}
					int32_t index = value2.Int32;
					if (index < 0 || index >= arraysize)
					{
						throw ClrException("Array index out of range", SystemException::IndexOutOfRange, currentFrame->_executingMethod->methodToken);
					}

					Variable v1;
					if (value1.Type == VariableKind::ValueArray)
					{
						// This should always exist
						ClassDeclaration* elemTy = _classes.GetClassWithToken(token);
						switch (elemTy->ClassDynamicSize)
						{
						case 1:
						{
							byte* dataptr = (byte*)data;
							v1.Object = (dataptr + ARRAY_DATA_START + index);
							break;
						}
						case 2:
						{
							short* dataptr = (short*)data;
							v1.Object = (dataptr + ARRAY_DATA_START/2 + index);
							break;
						}
						case 4:
						{
							v1.Object = (data + ARRAY_DATA_START/4 + index);
							break;
						}
						case 8:
						{
							uint64_t* dataptr = (uint64_t*)data;
							v1.Object = (AddBytes(dataptr, ARRAY_DATA_START) + index);
							break;
						}
						default:
							byte* dataptr = (byte*)data;
							v1.Object = AddBytes(dataptr, ARRAY_DATA_START + (elemTy->ClassDynamicSize * index));
							break;
						}
						
						v1.Type = VariableKind::AddressOfVariable;
					}
					else
					{
						// can only be an object now
						v1.Object = (data + ARRAY_DATA_START/4 + index);
						v1.Type = VariableKind::AddressOfVariable;
					}
					stack->push(v1);
					break;
				}
				case CEE_BOX:
				{
					Variable& value1 = stack->top();
					stack->pop();
					ClassDeclaration* ty = _classes.GetClassWithToken(token);
					if (ty->IsValueType())
					{
						Variable r = Box(value1, ty);
						stack->push(r);
					}
					else
					{
						// If ty is a reference type, box does nothing
						Variable r;
						r.Marker = VARIABLE_DEFAULT_MARKER;
						r.setSize(4);
						r.Object = value1.Object;
						r.Type = value1.Type;
						stack->push(r);
					}
						
					break;
				}
				case CEE_UNBOX_ANY:
				{
					// Note: UNBOX and UNBOX.any are quite different
					Variable& value1 = stack->top();
					stack->pop();
					ClassDeclaration* ty = _classes.GetClassWithToken(token);
					if (ty->IsValueType())
					{
						// TODO: This requires special handling for types derived from Nullable<T>

						uint32_t offset = sizeof(void*);
						// Get the beginning of the data part of the object
						byte* o = (byte*)value1.Object;
						size = ty->ClassDynamicSize;
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
					ClassDeclaration* ty = _classes.GetClassWithToken(token);
					if (ty->IsValueType())
					{
						size = ty->ClassDynamicSize;
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

					ClassDeclaration* ty = _classes.GetClassWithToken(token);
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

					ClassDeclaration* ty = _classes.GetClassWithToken(token);

					// Our metadata list does not contain full information about array[] types, therefore we assume this matches for now
					if ((value1.Type == VariableKind::ReferenceArray || value1.Type == VariableKind::ValueArray) && ty->IsArray())
					{
						// TODO: Verification possible when looking inside?
						stack->push(value1);
						break;
					}
					
					if (IsAssignableFrom(ty, value1) == MethodState::Running)
					{
						// if the cast is fine, just return the original object
						stack->push(value1);
						break;
					}
					// The cast fails. Throw a InvalidCastException
					throw ClrException("Invalid cast", SystemException::InvalidCast, ty->ClassToken);
				}
				case CEE_CONSTRAINED_:
					constrainedTypeToken = token; // This is always immediately followed by a callvirt
					goto immediatellyContinue;

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
						throw ClrException("LDOBJ with invalid argument", SystemException::InvalidOperation, token);
						return MethodState::Aborted;
					}
					
					ClassDeclaration* ty = _classes.GetClassWithToken(token);
					size = ty->ClassDynamicSize;
					EnsureStackVarSize(size);
					if (ty->IsValueType())
					{
						if (size > 8)
						{
							tempVariable->Type = VariableKind::LargeValueType;
						}
						else if (ty->ClassToken == (int)KnownTypeTokens::Int32)
						{
							tempVariable->Type = VariableKind::Int32;
						}
						else if (ty->ClassToken == (int)KnownTypeTokens::Uint32)
						{
							tempVariable->Type = VariableKind::Uint32;
						}
						else if (ty->ClassToken == (int)KnownTypeTokens::Int64)
						{
							tempVariable->Type = VariableKind::Int64;
						}
						else if (ty->ClassToken == (int)KnownTypeTokens::Uint64)
						{
							tempVariable->Type = VariableKind::Uint64;
						}
						// Fallback cases
						else if (size >= 4)
						{
							tempVariable->Type = VariableKind::Int64;
						}
						else
						{
							tempVariable->Type = VariableKind::Int32;
						}
					}
					else
					{
						tempVariable->Type = VariableKind::Object;
					}
					tempVariable->setSize((uint16_t)size);
					tempVariable->Marker = 0x37;
					memcpy(&tempVariable->Int32, value1.Object, size);
					stack->push(*tempVariable);
				}
				break;
				case CEE_STOBJ:
				{
					Variable& src = stack->top();
					stack->pop();
					Variable dest = stack->top();
					stack->pop();
					if (dest.Object == nullptr)
					{
						throw ClrException(SystemException::NullReference, currentFrame->_executingMethod->methodToken);
					}
					if (dest.Type != VariableKind::AddressOfVariable)
					{
						throw ClrException("LDOBJ with invalid argument", SystemException::InvalidOperation, token);
						return MethodState::Aborted;
					}

					ClassDeclaration* ty = _classes.GetClassWithToken(token);
					size = ty->ClassDynamicSize;
					memcpy(dest.Object, &src.Int32, size);
				}
				break;
				case CEE_SIZEOF:
					{
					ClassDeclaration* ty = _classes.GetClassWithToken(token);
						if (!ty->IsValueType())
						{
							// Reference types return sizeof(void*)
							intermediate.setSize(4);
							intermediate.Type = VariableKind::Uint32;
							intermediate.Int32 = sizeof(void*);
							stack->push(intermediate);
						}
						else
						{
							intermediate.setSize(4);
							intermediate.Type = VariableKind::Uint32;
							intermediate.Int32 = ty->ClassDynamicSize;
							stack->push(intermediate);
						}
					break;
					}
				default:
					InvalidOpCode(PC, instr);
					return MethodState::Aborted;
				}
				break;
			}
			case InlineTok:
				{
				int token = ReadUint32FromArbitraryAddress(pCode + PC);
					PC += 4;
					switch(instr)
					{
					case CEE_LDTOKEN:
						{
							// constants above 0x10000 are string tokens, but they're not used with LDTOKEN, but with LDSTR
							byte* data = GetConstant(token);
							if (token < 0x10000 && data != nullptr)
							{
								intermediate.Object = data;
								intermediate.Type = VariableKind::RuntimeFieldHandle;
								stack->push(intermediate);
							}
							else if (_classes.GetClassWithToken(token, false))
							{
								intermediate.Int32 = token;
								intermediate.Type = VariableKind::RuntimeTypeHandle;
								stack->push(intermediate);
							}
							else
							{
								// Unsupported case
								throw ClrException("Argument to LDTOKEN is unknown", SystemException::ClassNotFound, token);
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
					int token = static_cast<int32_t>(((uint32_t)pCode[PC]) + (((uint32_t)pCode[PC + 1]) << 8) + (((uint32_t)pCode[PC + 2]) << 16) + (((uint32_t)pCode[PC + 3]) << 24));
					PC += 4;
					bool emptyString = (token & 0xFFFF) == 0;
					int length = 0;
					char* string = nullptr;
					if (!emptyString)
					{
						string = GetString(token, length);
					}

					// This creates an unicode string (16 bits per letter) from the UTF-8 encoded constant retrieved above
					Variable stringVariable = CreateStringInstance(length, string);
					stack->push(stringVariable);
				}
				break;
			case InlineSwitch:
				{
					Variable& targetIndex = stack->top();
					stack->pop();
					uint32_t size = static_cast<uint32_t>(((uint32_t)pCode[PC]) + (((uint32_t)pCode[PC + 1]) << 8) + (((uint32_t)pCode[PC + 2]) << 16) + (((uint32_t)pCode[PC + 3]) << 24));
					int* targets = (int*)AddBytes(pCode, PC + 4);
					PC += (uint16_t)(4 * (size + 1)); // Point to instruction beyond end of switch
					if (targetIndex.Uint32 < size) // If the index is out of bounds, we fall trough
					{
						int offset = targets[targetIndex.Uint32];
						int temp = offset + PC;
						PC = (uint16_t)temp;
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
		_instructionsExecuted += instructionsExecutedThisLoop;
		currentFrame->UpdatePc(PC);
		Firmata.sendString(STRING_DATA, ox.Message());
		_gc.PrintStatistics();
		Variable v(VariableKind::Object);
		CreateFatalException(SystemException::OutOfMemory, v, 0);
		return MethodState::Aborted;
	}
	catch(ClrException& cx)
	{
		if (_breakOnException)
		{
			_nextStepBehavior.Kind = BreakpointType::Once;
			_commandsToSkip = 0;
		}

		// On an exception, send the state prior to the stack unwinding once
		if (_debuggerEnabled)
		{
			Firmata.sendString(F("Exception caught. First follows the state before stack unwinding, then after: "));
			SendDebugState(rootState);
		}

		_instructionsExecuted += instructionsExecutedThisLoop;
		currentFrame->UpdatePc(PC);
		Variable v(VariableKind::Object);
		v.Object = cx.ExceptionObject(this);
		ExceptionClause* c = nullptr;

		// v.Object is only null if we were unable to convert a system exception into a managed exception.
		if (v.Object != nullptr && LocateCatchHandler(currentFrame, PC, v, &c))
		{
			currentFrame->UpdatePc(CreateExceptionFrame(currentFrame, 0, c, v));
			currentFrame->ActivateState(&PC, &stack, &locals, &arguments);
			// When entering a catch frame, the execution stack is cleared and the exception pushed to it (for a catch handler)
			ClearExecutionStack(stack);
			if (c->ClauseType == ExceptionHandlingClauseOptions::Clause)
			{
				stack->push(v);
			}

			if (c->ClauseType == ExceptionHandlingClauseOptions::Clause)
			{
				// Drop the top element of the exception stack. We can't throw it away entirely, because we might be inside an outer finally clause
				ExceptionFrame* frame = currentFrame->_exceptionFrame;
				ExceptionFrame* previous = nullptr;
				// This chain is only required for the rare case where another exception (including handler) sits inside a finally clause.
				while (frame->Next != nullptr)
				{
					previous = frame;
					frame = frame->Next;
				}

				if (previous)
				{
					delete previous->Next;
					previous->Next = nullptr;
				}
				else
				{
					delete currentFrame->_exceptionFrame;
					currentFrame->_exceptionFrame = nullptr;
				}
			}

			return MethodState::Running; // And go to start
		}
		else
		{
			// No suitable handler found
			CreateFatalException(cx.ExceptionType(), v, cx.ExceptionToken());
			return MethodState::Aborted;
		}
	}
	catch(ExecutionEngineException& ee)
	{
		_instructionsExecuted += instructionsExecutedThisLoop;
		currentFrame->UpdatePc(PC);
		Firmata.sendString(STRING_DATA, ee.Message());
		Variable v(VariableKind::Object);
		CreateFatalException(SystemException::ExecutionEngine, v, 0);
		return MethodState::Aborted;
	}
	
	_instructionsExecuted += instructionsExecutedThisLoop;
	currentFrame->UpdatePc(PC);

	// This performs a GC every 50'th instruction. We should find something better (hint: When an OutOfMemoryException is about to be
	// thrown is a good idea, with every third mouse click not)
	_gc.Collect(2, this);
	
	TRACE(startTime = (micros() - startTime) / NUM_INSTRUCTIONS_AT_ONCE);
	TRACE(Firmata.sendString(F("Interrupting method at 0x"), PC));
	TRACE(Firmata.sendStringf(F("Average time per IL instruction: %ld microseconds"), startTime));

	// We interrupted execution to not waste to much time here - the parent will return to us asap
	return MethodState::Running;
}

/// <summary>
/// Locates an exception handler for a given exception
/// </summary>
/// <param name="state">The execution state of the calling method. Note that upon return, this may have changed to another method!</param>
/// <param name="tryBlockOffset">The PC at which a handler is needed (this shall be within the handlers "try" block)</param>
/// <param name="exceptionToHandle">The Exception that needs handling</param>
/// <param name="clauseThatMatches">The clause that best fits</param>
/// <returns>True if a handler was found, false otherwise</returns>
bool FirmataIlExecutor::LocateCatchHandler(ExecutionState*& state, int tryBlockOffset, Variable& exceptionToHandle, ExceptionClause** clauseThatMatches)
{
	ExecutionState* newState = state;
	while (newState != nullptr)
	{
		*clauseThatMatches = nullptr;
		uint32_t indexOfClause = 0;
		uint32_t key = newState->_executingMethod->GetKey();
		// This may return null if we don't have any clauses in this method
		ExceptionClause* c = _clauses.BinarySearchKey(key, indexOfClause);

		// TODO: This probably also needs to check whether we are in a frame already
		ExceptionClause* bestClause = nullptr;
		while (c && c->GetKey() == key)
		{
			// exceptionToHandle is void (empty) if we're only looking for finally handlers
			if (c->ClauseType == ExceptionHandlingClauseOptions::Clause && exceptionToHandle.Type != VariableKind::Void)
			{
				ClassDeclaration* catchType = GetClassWithToken(c->FilterToken, true);
				// if the block offset where whe left the protected block (the try{} part) is within this handler's try block, it is a possible candidate
				if (tryBlockOffset >= c->TryOffset && tryBlockOffset <= c->TryOffset + c->TryLength
					&& IsAssignableFrom(catchType, exceptionToHandle) == MethodState::Running)
				{
					if (bestClause == nullptr)
					{
						bestClause = c;
					}
					else
					{
						// If multiple try blocks surround the code location in question, we use the shortest one
						if (c->TryLength < bestClause->TryLength)
						{
							bestClause = c;
						}
					}
				}
			}
			else if (c->ClauseType == ExceptionHandlingClauseOptions::Finally && tryBlockOffset >= c->TryOffset && tryBlockOffset <= c->TryOffset + c->TryLength)
			{
				if (bestClause == nullptr)
				{
					bestClause = c;
				}
				else
				{
					// If multiple try blocks surround the code location in question, we use the shortest one
					if (c->TryLength < bestClause->TryLength)
					{
						bestClause = c;
					}
				}
			}
			indexOfClause++;
			if (indexOfClause < _clauses.size())
			{
				c = _clauses.at(indexOfClause);
			}
			else
			{
				c = nullptr;
			}
		}

		if (bestClause != nullptr)
		{
			if (bestClause->ClauseType == ExceptionHandlingClauseOptions::Clause)
			{
				TRACE(Firmata.sendStringf(F("Found a catch clause at 0x%x in 0x%x"), bestClause->HandlerOffset, bestClause->GetKey()));
			}
			else if (bestClause->ClauseType == ExceptionHandlingClauseOptions::Finally)
			{
				TRACE(Firmata.sendStringf(F("Found a finally clause at 0x%x in 0x%x"), bestClause->HandlerOffset, bestClause->GetKey()));
			}
			else
			{
				throw ExecutionEngineException("Unsupported exception handler type found");
			}

			// We'll be using this handler. Now clean any stack frames below the one we are in
			CleanStack(newState->_next);
			newState->_next = nullptr;
			state = newState;
			*clauseThatMatches = bestClause;
			// We found an exception handler in a different function than where we ended up unwinding the stack. That's bad.
			ASSERT(state->_executingMethod->GetKey() == bestClause->GetKey(), "Found an exception handler, but the methods don't match");
			return true;
		}

		if (exceptionToHandle.Type == VariableKind::Void)
		{
			// If we have no exception to handle, don't look for finally handlers further up the stack (but directly go to the target of the leave instruction)
			return false;
		}

		ExecutionState* tempState = newState;
		// Go up one stack frame
		newState = _methodCurrentlyExecuting;
		tryBlockOffset = newState->CurrentPc(); // search from previous call location
		while (newState != nullptr && newState->_next != tempState)
		{
			newState = newState->_next;
		}
	}
	return false;
};

Variable FirmataIlExecutor::GetExceptionObjectFromToken(SystemException exceptionType, const char* errorMessage)
{
	Variable message = CreateStringInstance(strlen(errorMessage), errorMessage);
	KnownTypeTokens typeToInstantiate = KnownTypeTokens::None;
	// Note: Do not add cases that cause a fatal error, such as MissingMethodException below, as it will cause improper error reporting
	switch(exceptionType)
	{
	case SystemException::DivideByZero:
		typeToInstantiate = KnownTypeTokens::DivideByZeroException;
		break;
	case SystemException::Arithmetic:
		typeToInstantiate = KnownTypeTokens::ArithmeticException;
		break;
	case SystemException::ArrayTypeMismatch:
		typeToInstantiate = KnownTypeTokens::ArrayTypeMismatchException;
		break;
	case SystemException::IndexOutOfRange:
		typeToInstantiate = KnownTypeTokens::IndexOutOfRangeException;
		break;
	case SystemException::InvalidCast:
		typeToInstantiate = KnownTypeTokens::InvalidCastException;
		break;
	case SystemException::InvalidOperation:
		typeToInstantiate = KnownTypeTokens::InvalidOperationException;
		break;
	case SystemException::NullReference:
		typeToInstantiate = KnownTypeTokens::NullReferenceException;
		break;
	case SystemException::Io:
		typeToInstantiate = KnownTypeTokens::IoException;
		break;
	case SystemException::NotSupported:
		typeToInstantiate = KnownTypeTokens::NotSupportedException;
		break;
	case SystemException::Overflow:
		typeToInstantiate = KnownTypeTokens::OverflowException;
		break;
	}
	if (typeToInstantiate == KnownTypeTokens::None)
	{
		return Variable();
	}

	try
	{
		void* exceptionPtr = CreateInstanceOfClass((int)typeToInstantiate, 0);
		Variable exception(VariableKind::Object);
		exception.Object = exceptionPtr;
		ClassDeclaration* cls = GetClassDeclaration(exception);
		SetField4(cls, message, exception, 0);
		return exception;
	}
	catch(ClrException&)
	{
		// The exception type we wanted to instantiate is not loaded, this implies that (unless we have a bug in our code), there's
		// no handler for it and we can directly throw a fatal error.
		return Variable();
	}
}

/// <summary>
/// Check whether we should break at this position.
/// </summary>
/// <param name="state">Current stack frame</param>
/// <param name="pc">Current PC</param>
///	<param name="code">Pointer to code of current method (or null for internal methods)</param>
/// <returns>True if we should break, false if execution should continue</returns>
bool FirmataIlExecutor::CheckForBreakCondition(ExecutionState* state, uint16_t pc, byte* code)
{
	if (!_debuggerEnabled)
	{
		return false;
	}

	if (_commandsToSkip > 0)
	{
		_commandsToSkip--;
		return false;
	}

	// Attempt to break before the exception is actually thrown
	if (_breakOnException && code != nullptr && (code[pc] == CEE_THROW))
	{
		return true;
	}

	if (_nextStepBehavior.Kind == BreakpointType::Once || _nextStepBehavior.Kind == BreakpointType::StepInto)
	{
		_nextStepBehavior.Kind = BreakpointType::None;
		return true;
	}

	if (_nextStepBehavior.Kind == BreakpointType::StepOver)
	{
		// TODO: This steps over until the same method is executed again. This will be unexpected in case of a recursive function call.
		if (state->_executingMethod->GetKey() == _nextStepBehavior.MethodToken)
		{
			_nextStepBehavior.Kind = BreakpointType::None;
			return true;
		}
	}
	if (_nextStepBehavior.Kind == BreakpointType::StepOut)
	{
		// TODO: This steps over until the same method is executed again. This will be unexpected in case of a recursive function call.
		if (state->_executingMethod->GetKey() == _nextStepBehavior.MethodToken && code != nullptr && (code[pc] == CEE_RET || code[pc] == CEE_THROW))
		{
			// Still execute the "ret" instruction itself, so do one more loop
			_nextStepBehavior.Kind = BreakpointType::Once;
			return false;
		}
	}

	for (size_t i = 0; i < _breakpoints.size(); i++)
	{
		auto bp = _breakpoints.at(i);
		if (bp.MethodToken == state->_executingMethod->methodToken && pc == bp.Pc)
		{
			_nextStepBehavior.Kind = BreakpointType::None; // If we single-stepped here, stop that
			return true;
		}
	}

	return false;
}

/// <summary>
/// Sends the current task state
/// </summary>
/// <param name="state">State of the stack to report. This should typically be the root of the stack</param>
/// <param name="fullInfo"></param>
void FirmataIlExecutor::SendDebugState(ExecutionState* state)
{
	// The "default" breakpoints using CEE_BREAK instructions will probably not be supported, but we leave that name open for now
	SendReplyHeader(ExecutorCommand::ConditionalBreakpointHit);
	Firmata.sendPackedUInt14((uint16_t)state->TaskId());

	while (state != nullptr)
	{
		Firmata.sendPackedUInt32(state->_executingMethod->methodToken);
		Firmata.sendPackedUInt32(state->CurrentPc());
		state = state->_next;
	}

	Firmata.endSysex();
}

/// <summary>
/// Returns the given variable list
/// </summary>
/// <param name="stackFrame">The stack frame to consider</param>
/// <param name="variableType">The variable type (0 = locals, 1 = arguments, 2 = evaluation stack)</param>
void FirmataIlExecutor::SendVariables(ExecutionState* stackFrame, uint32_t frameNo, int variableType)
{
	SendReplyHeader(ExecutorCommand::Variables);
	Firmata.sendPackedUInt14((uint16_t)stackFrame->TaskId());
	uint16_t pc;
	VariableDynamicStack* stack;
	VariableVector* locals;
	VariableVector* arguments;
	stackFrame->ActivateState(&pc, &stack, &locals, &arguments);
	Firmata.write((byte)variableType);
	Firmata.sendPackedUInt32(frameNo);
	Firmata.sendPackedUInt32(stackFrame->_executingMethod->GetKey());
	int idx = 0;
	if (variableType == 2)
	{
		VariableDynamicStack::Iterator stackIterator = stack->GetIterator();
		Variable* var;
		while ((var = stackIterator.next()) != nullptr)
		{
			SendVariable(*var, idx);
		}
	}
	else if (variableType == 0)
	{
		for (int i = 0; i < locals->size(); i++)
		{
			Variable& v = locals->at(i);
			SendVariable(v, idx);
		}
	}
	else if (variableType == 1)
	{
		for (int i = 0; i < arguments->size(); i++)
		{
			Variable& v = arguments->at(i);
			SendVariable(v, idx);
		}
	}

	Firmata.endSysex();
}

void FirmataIlExecutor::SendVariable(const Variable& variable, int& idx)
{
	Firmata.sendPackedUInt32(idx);
	idx++;
	Firmata.sendPackedUInt14((uint16_t)variable.Type);
	Firmata.sendPackedUInt64(variable.Int64);
	// Todo: Send content for object type variables
}



void FirmataIlExecutor::SignExtend(Variable& variable, int inputSize)
{
	// If we are already at long, don't do anything
	if (inputSize > 4)
	{
		return;
	}
	// For int64, both paths may be taken, so that extension from i1 to i8 is possible
	if (variable.Type == VariableKind::Int32 || variable.Type == VariableKind::Int64)
	{
		if (inputSize == 1 && variable.Int32 >= 0x80)
		{
			variable.Int32 |= 0xFFFFFF00;
		}
		if (inputSize == 2 && variable.Int32 >= 0x8000)
		{
			variable.Int32 |= 0xFFFF0000;
		}
	}
	if (variable.Type == VariableKind::Int64 && variable.Int64 >= 0x80000000)
	{
		variable.Int64 |= 0xFFFFFFFF00000000;
	}
}

void FirmataIlExecutor::CreateFatalException(SystemException exception, Variable& managedException, int hintToken)
{
	if (managedException.Type != VariableKind::Object && managedException.Type != VariableKind::Void)
	{
		throw ExecutionEngineException("Double fault: Illegal exception type");
	}
	_currentException.ExceptionType = exception;
	_currentException.TokenOfException = hintToken;
	_currentException.ExceptionObject = managedException; // Must be of type object

	ExecutionState* currentFrame = _methodCurrentlyExecuting;
	int idx = 0;
	memset(_currentException.StackTokens, 0, RuntimeException::MaxStackTokens * sizeof(int));
	memset(_currentException.PerStackPc, 0, RuntimeException::MaxStackTokens * sizeof(uint16_t));
	while (currentFrame != NULL)
	{
		if (idx >= RuntimeException::MaxStackTokens)
		{
			// If we have more than MaxStackTokens elements, shift the list and drop the first (lowest) elements
			memmove(_currentException.StackTokens, &_currentException.StackTokens[1], sizeof(int) * (RuntimeException::MaxStackTokens - 1));
			memmove(_currentException.PerStackPc, &_currentException.PerStackPc[1], sizeof(uint16_t) * (RuntimeException::MaxStackTokens - 1));
			idx--;
		}
		
		_currentException.StackTokens[idx] = currentFrame->_executingMethod->methodToken;
		_currentException.PerStackPc[idx++] = currentFrame->CurrentPc();
		currentFrame = currentFrame->_next;
	}
}

/// <summary>
/// Returns MethodState::Running if true, MethodState::Aborted otherwise. The caller must decide on context whether
/// that should throw an exception
/// </summary>
/// <param name="typeToAssignTo">The type that should be the assignment target</param>
/// <param name="object">The value that should be assigned</param>
/// <returns>See above</returns>
MethodState FirmataIlExecutor::IsAssignableFrom(ClassDeclaration* typeToAssignTo, const Variable& object)
{
	byte* o = (byte*)object.Object;
	ClassDeclaration* sourceType = (ClassDeclaration*)(*(int32_t*)o);
	// If the types are the same, they're assignable
#if GC_DEBUG_LEVEL >= 2
	if ((int)sourceType == 0xaaaaaaaa)
	{
		throw ExecutionEngineException("Type test on deleted object - this is a GC error");
	}
#endif
	if (sourceType->ClassToken == typeToAssignTo->ClassToken)
	{
		return MethodState::Running;
	}

	// Special handling for types derived from "System.Type", because this runtime implements a subset of the type library only
	if (sourceType->ClassToken == 2 && (typeToAssignTo->ClassToken == 5 || typeToAssignTo->ClassToken == 6))
	{
		// Casting System.Type to System.RuntimeType or System.Reflection.TypeInfo is fine
		return MethodState::Running;
	}

	// If sourceType derives from typeToAssign, that works as well
	ClassDeclaration* parent = _classes.GetClassWithToken(sourceType->ParentToken, false);
	while (parent != nullptr)
	{
		if (parent->ClassToken == typeToAssignTo->ClassToken)
		{
			return MethodState::Running;
		}
		
		parent = _classes.GetClassWithToken(parent->ParentToken, false);
	}

	// If the assignment target implements the source interface, that's fine
	if (typeToAssignTo->ImplementsInterface(sourceType->ClassToken))
	{
		return MethodState::Running;
	}

	// if the assignment target is an interface implemented by source, that's fine
	if (sourceType->ImplementsInterface(typeToAssignTo->ClassToken))
	{
		return MethodState::Running;
	}

	return MethodState::Stopped;
}

/// <summary>
/// Creates a class directly by its type (used i.e. to create instances of System::Type or System::String)
/// </summary>
void* FirmataIlExecutor::CreateInstanceOfClass(int32_t typeToken, uint32_t length /* for string */)
{
	ClassDeclaration* cls = _classes.GetClassWithToken(typeToken);
	TRACE(Firmata.sendString(F("Class to create is 0x"), cls->ClassToken));
	// Compute sizeof(class)
	size_t sizeOfClass = SizeOfClass(cls);
	if (cls->ClassToken == (int)KnownTypeTokens::String)
	{
		sizeOfClass = sizeof(void*) + 4 + length * SIZEOF_CHAR;
	}

	TRACE(Firmata.sendString(F("Class size is 0x"), sizeOfClass));
	void* ret = AllocGcInstance(sizeOfClass);
	if (ret == nullptr)
	{
		OutOfMemoryException::Throw("Out of memory creating class instance internally");
		return nullptr;
	}

	// Save a reference to the class declaration in the first entry of the newly created instance.
	// this will serve as vtable.
	ClassDeclaration** vtable = (ClassDeclaration**)ret;
	*vtable = cls;
	return ret;
}

ClassDeclaration* FirmataIlExecutor::ResolveClassFromCtorToken(int32_t ctorToken)
{
	TRACE(Firmata.sendString(F("Creating instance via .ctor 0x"), ctorToken));
	for (auto iterator = _classes.GetIterator(); iterator.Next();)
	{
		// TRACE(Firmata.sendString(F("Class "), cls.ClassToken));
		int idx = 0;
		ClassDeclaration* current = iterator.Current();
		for (auto method = current->GetMethodByIndex(idx); method != nullptr; method = current->GetMethodByIndex(++idx))
		{
			if (method->MethodToken() == ctorToken)
			{
				return current;
			}
		}
	}

	throw ClrException(SystemException::MissingMethod, ctorToken);
}

ClassDeclaration* FirmataIlExecutor::ResolveClassFromFieldToken(int32_t fieldToken)
{
	for (auto iterator = _classes.GetIterator(); iterator.Next();)
	{
		// TRACE(Firmata.sendString(F("Class "), cls.ClassToken));
		int idx = 0;
		ClassDeclaration* current = iterator.Current();
		for (auto field = current->GetFieldByIndex(idx); field != nullptr; field = current->GetFieldByIndex(++idx))
		{
			// TRACE(Firmata.sendString(F("Member "), member.Uint32));
			if (field->Int32 == fieldToken)
			{
				return current;
			}
		}
	}

	throw ClrException(SystemException::FieldAccess, fieldToken);
}

/// <summary>
/// Creates an instance of the given type.
/// System.String needs special handling, since its instances have a dynamic length (the string is coded inline). This method shall therefore not be used to create instances of string
/// (Use CreateInstanceOfClass instead)
/// </summary>
void* FirmataIlExecutor::CreateInstance(ClassDeclaration* cls)
{
	TRACE(Firmata.sendString(F("Class to create is 0x"), cls->ClassToken));
	// The constructor that was called belongs to this class
	// Compute sizeof(class)
	size_t sizeOfClass = SizeOfClass(cls);

	TRACE(Firmata.sendString(F("Class size is 0x"), sizeOfClass));
	void* ret = AllocGcInstance(sizeOfClass);
	if (ret == nullptr)
	{
		OutOfMemoryException::Throw("Out of memory creating class instance ");
		return nullptr;
	}

	// Save a reference to the class declaration in the first entry of the newly created instance.
	// this will serve as vtable.
	ClassDeclaration** vtable = (ClassDeclaration**)ret;
	*vtable = cls;
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

ExecutionError FirmataIlExecutor::LoadClassSignature(bool isLastPart, int32_t classToken, uint32_t parent, uint16_t dynamicSize, uint16_t staticSize, uint16_t flags, uint16_t offset, byte argc, byte* argv)
{
	// TRACE(Firmata.sendStringf(F("Class %lx has parent %lx and size %d."), classToken, parent, dynamicSize));
	ClassDeclaration* elem = _classes.GetClassWithToken(classToken, false);

	ClassDeclarationDynamic* decl;
	if (elem != nullptr)
	{
		if (elem->GetType() == ClassDeclarationType::Dynamic)
		{
			decl = (ClassDeclarationDynamic*)(_classes.GetClassWithToken(classToken));
		}
		else
		{
			throw ExecutionEngineException("Internal error: Unknown class type");
		}
	}
	else
	{
		// The only flag is currently "isvaluetype"
		bool isValueType = flags & 1;
		if (!isValueType)
		{
			// For reference types, the class size given is shifted by two (because it's always a multiple of 4).
			// Value types are not expected to exceed more than a few words
			dynamicSize = dynamicSize << 2;
		}

		void* ptr = mallocEx(sizeof(ClassDeclarationDynamic));
		
		ClassDeclarationDynamic* newType = new(ptr) ClassDeclarationDynamic(classToken, parent, dynamicSize, staticSize, (ClassProperties)flags);
		_classes.Insert(newType);
		decl = newType;
	}
	
	// Reinit
	if (offset == 0)
	{
		// If the class is not new, throw. Handling this right would be needlessly complicated.
		if (decl->fieldTypes.size() > 0 || decl->methodTypes.size() > 0 || decl->interfaceTokens.size() > 0)
		{
			Firmata.sendString(F("Definition for class already seen. Duplicates not permited"));
			return ExecutionError::InvalidArguments;
		}
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
		if (isLastPart)
		{
			decl->fieldTypes.truncate();
			decl->methodTypes.truncate();
		}
		return ExecutionError::None;
	}

	// if there are more arguments, these are the method tokens that point back to this implementation
	
	int methodToken = v.Int32;
	vector<int> baseTokens;
	for (;i < argc - 4; i += 5)
	{
		// These are tokens of possible base implementations of this method - that means methods whose dynamic invocation target will be this method.
		baseTokens.push_back(DecodePackedUint32(argv + i));
	}

	if (baseTokens.size() > 0)
	{
		static int n = 0;
		int* convertedBaseTokens = (int*)mallocEx(sizeof(int) * (baseTokens.size() + 1));
		for (size_t j = 0; j < baseTokens.size(); j++)
		{
			convertedBaseTokens[j] = baseTokens[j];
		}

		convertedBaseTokens[baseTokens.size()] = methodToken + (++n << 16);

		decl->methodTypes.push_back(Method(methodToken, baseTokens.size(), convertedBaseTokens));
	}
	else
	{
		decl->methodTypes.push_back(Method(methodToken, 0, nullptr));
	}
	
	if (isLastPart)
	{
		decl->fieldTypes.truncate();
		decl->methodTypes.truncate();
	}
	return ExecutionError::None;
}

ExecutionError FirmataIlExecutor::LoadInterfaces(int32_t classToken, byte argc, byte* argv)
{
	auto elem = _classes.GetClassWithToken(classToken);
	if (elem == nullptr)
	{
		return ExecutionError::InvalidArguments;
	}

	if (elem->GetType() != ClassDeclarationType::Dynamic)
	{
		return ExecutionError::InternalError;
	}

	ClassDeclarationDynamic* ty = (ClassDeclarationDynamic*)(_classes.GetClassWithToken(classToken));
	for (int i = 0; i < argc;)
	{
		int token = DecodePackedUint32(argv + i);
		ty->interfaceTokens.push_back(token);
		i += 5;
	}
	return ExecutionError::None;
}

void FirmataIlExecutor::SendReplyHeader(ExecutorCommand subCommand)
{
	// Same command start as the SendAckOrNack
	Firmata.startSysex();
	Firmata.write(SCHEDULER_DATA);
	Firmata.write((byte)ExecutorCommand::Reply);
	Firmata.write((byte)subCommand);
	Firmata.write((byte)0);
}

void FirmataIlExecutor::SendAckOrNack(ExecutorCommand subCommand, byte sequenceNo, ExecutionError errorCode)
{
	Firmata.startSysex();
	Firmata.write(SCHEDULER_DATA);
	if (errorCode == ExecutionError::None)
	{
		Firmata.write((byte)ExecutorCommand::Ack);
	}
	else
	{
		Firmata.write((byte)ExecutorCommand::Nack);

		FirmataStatusLed::FirmataStatusLedInstance->setStatus(STATUS_ERROR, 1000);
	}
	Firmata.write((byte)subCommand);
	Firmata.write((byte)errorCode);
	Firmata.write(sequenceNo);
	Firmata.endSysex();
}

MethodBody* FirmataIlExecutor::GetMethodByToken(int32_t token)
{
	MethodBody* ret = _methods.BinarySearchKey(token);
	if (ret != nullptr)
	{
		return ret;
	}

	TRACE(Firmata.sendString(F("Reference not found: "), token));
	return nullptr;
}

/// <summary>
/// Returns the n'th method on the stack (where 0 is the oldest and n is the currently executing method)
/// </summary>
/// <param name="n">Number of method to return, 0-based (will return the actual stack number used)</param>
/// <returns>The stack frame for the given number. If there are not as many stack frames as specified, the last one is returned</returns>
ExecutionState* FirmataIlExecutor::GetNthMethodOnStack(uint32_t& n)
{
	auto currentMethod = _methodCurrentlyExecuting;
	int realn = 0;
	while (currentMethod->_next != nullptr && realn != n) // iterate zero times if n is zero
	{
		currentMethod = currentMethod->_next;
		realn++;
	}

	n = realn;
	return currentMethod;
}

ExecutionError FirmataIlExecutor::ExecuteDebuggerCommand(DebuggerCommand cmd, uint32_t arg1, uint32_t arg2)
{
	if (_methodCurrentlyExecuting != nullptr)
	{
		uint32_t idx = INT32_MAX;
		auto currentMethod = GetNthMethodOnStack(idx);
		_nextStepBehavior.MethodToken = currentMethod->_executingMethod->GetKey();
		_nextStepBehavior.Pc = currentMethod->CurrentPc();
	}
	else
	{
		_nextStepBehavior.MethodToken = 0;
		_nextStepBehavior.Pc = 0;
	}

	switch(cmd)
	{
	case DebuggerCommand::Break:
		_debugBreakActive = false;
		_nextStepBehavior.Kind = BreakpointType::Once;
		break;
	case DebuggerCommand::StepInto:
		_debugBreakActive = false;
		_nextStepBehavior.Kind = BreakpointType::StepInto;
		_commandsToSkip = 1;
		break;
	case DebuggerCommand::StepOver:
		_debugBreakActive = false;
		_nextStepBehavior.Kind = BreakpointType::StepOver;
		_commandsToSkip = 1;
		break;
	case DebuggerCommand::StepOut:
		_debugBreakActive = false;
		_nextStepBehavior.Kind = BreakpointType::StepOut;
		_commandsToSkip = 1;
		break;
	case DebuggerCommand::Continue:
		_commandsToSkip = 1; // Execute at least one command now
		_debugBreakActive = false;
		break;
	case DebuggerCommand::EnableDebugging:
		_debugBreakActive = false;
		_debuggerEnabled = true;
		break;
	case DebuggerCommand::DisableDebugging:
		_debugBreakActive = false;
		_debuggerEnabled = false;
		_nextStepBehavior.Kind = BreakpointType::None;
		_breakpoints.clear();
		break;
	case DebuggerCommand::BreakPoint:
	{
		Breakpoint bp;
		bp.Kind = BreakpointType::CodeLine;
		bp.MethodToken = arg1;
		bp.Pc = arg2;
		_breakpoints.push_back(bp);
	}
		break;
	case DebuggerCommand::BreakOnExceptions:
		_breakOnException = !_breakOnException;
		if (_breakOnException)
		{
			Firmata.sendString(F("Breaking execution when an exception occurs"));
		}
		else
		{
			Firmata.sendString(F("Not breaking execution when an exception occurs"));
		}
		break;
	case DebuggerCommand::SendLocals:
	{
		auto stackFrame = GetNthMethodOnStack(arg1);
		SendVariables(stackFrame, arg1, 0);
		break;
	}
	case DebuggerCommand::SendEvaluationStack:
	{
		auto stackFrame = GetNthMethodOnStack(arg1);
		SendVariables(stackFrame, arg1, 2);
		break;
	}
	case DebuggerCommand::SendArguments:
		{
		auto stackFrame = GetNthMethodOnStack(arg1);
		SendVariables(stackFrame, arg1, 1);
		break;
		}
	default:
		return ExecutionError::InvalidArguments;
	}

	return ExecutionError::None;
}


OPCODE DecodeOpcode(const BYTE *pCode, uint16_t *pdwLen)
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
	KillCurrentTask(); // TODO: Or skip if running? At least we must not clear the memory while the task runs
	if (_stringHeapRam != nullptr)
	{
		// TODO: Find consecutive junks
		freeEx(_stringHeapRam);
		_stringHeapRam = nullptr;
		_stringHeapRamSize = 0;
	}
	
	_methods.clear(false);
	_classes.clear(false);
	_constants.clear(false);
	_clauses.clear(false);

	_currentException.ExceptionObject.Type = VariableKind::Void;
	_currentException.TokenOfException = 0;
	_currentException.ExceptionType = SystemException::None;

	freeEx(_staticVector);
	
	_specialTypeListRamLength = 0;
	freeEx(_specialTypeListRam);

	_gc.Clear(true);
	_weakDependencies.clear(true);
	
	TRACE(Firmata.sendStringf(F("Execution memory cleared. Free bytes: %d"), freeMemory()));
}
