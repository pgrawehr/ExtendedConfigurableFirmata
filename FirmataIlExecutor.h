/*
  FirmataIlExecutor

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  See file LICENSE.txt for further informations on licensing terms.

*/

#ifndef FirmataIlExecutor_h
#define FirmataIlExecutor_h

#include <ConfigurableFirmata.h>
#include <FirmataFeature.h>
#include <ObjectStack.h>
#include <ObjectList.h>

#define IL_EXECUTOR_SCHEDULER_COMMAND 0xFF

enum class ExecutorCommand : byte
{
	None = 0,
	DeclareMethod = 1,
	SetMethodTokens = 2,
	LoadIl = 3,
	StartTask = 4,
	ResetExecutor = 5,
	KillTask = 6,

	Nack = 0x7e,
	Ack = 0x7f,
};

enum class MethodFlags
{
	Static = 1,
	Virtual = 2,
	Special = 4,
	Void = 8
};

enum class MethodState
{
	Stopped = 0,
	Aborted = 1,
	Running = 2,
	Killed = 3,
};

enum class ExecutionError : byte
{
	None = 0,
	EngineBusy = 1,
	InvalidArguments,
};

class IlCode
{
public:
	IlCode()
	{
		methodToken = 0;
		methodFlags = 0;
		methodLength = 0;
		methodIl = nullptr;
		tokenMap = nullptr;
		tokenMapEntries = 0;
		maxLocals = 0;
		numArgs = 0;
		next = nullptr;
		codeReference = -1;
	}

	~IlCode()
	{
		Clear();
	}

	/// <summary>
	/// Clear the current entry, so it can be reused.
	/// </summary>
	void Clear()
	{
		methodToken = 0;
		if (tokenMap != nullptr)
		{
			free(tokenMap);
			tokenMap = nullptr;
			tokenMapEntries = 0;
		}
		if (methodIl != nullptr)
		{
			free(methodIl);
			methodIl = nullptr;
			methodLength = 0;
		}

		codeReference = -1;
	}
	
	uint32_t methodToken; // Primary method token (a methodDef token)
	byte methodFlags;
	byte methodLength;
	byte codeReference;
	// the maximum of (number of local variables, execution stack size)
	// For special methods (see methodFlags field), this contains the method number
	byte maxLocals; 
	byte numArgs;
	byte* methodIl;
	// this contains alternate tokens for the methods called from this method.
	// Typically, these will be mappings from 0x0A (memberRef tokens) to 0x06 (methodDef tokens)
	// for methods defined in another assembly than this method. 
	uint32_t* tokenMap;
	byte tokenMapEntries;
	IlCode* next;
};

class ExecutionState
{
	private:
	short _pc;
	ObjectStack _executionStack;
	ObjectList _locals;
	ObjectList _arguments;
	int _codeReference;
	
	public:
	// Next inner execution frame (the innermost frame is being executed) 
	ExecutionState* _next;

	uint32_t _memoryGuard;
	ExecutionState(int codeReference, int maxLocals, int argCount) : _pc(0), _executionStack(maxLocals),
	_locals(maxLocals), _arguments(argCount)
	{
		_codeReference = codeReference;
		_next = nullptr;
		_memoryGuard = 0xCCCCCCCC;
	}
	~ExecutionState()
	{
		_next = nullptr;
		_memoryGuard = 0xDEADBEEF;
	}
	
	void ActivateState(short* pc, ObjectStack** stack, ObjectList** locals, ObjectList** arguments)
	{
		if (_memoryGuard != 0xCCCCCCCC)
		{
			Firmata.sendString(F("FATAL: MEMORY CORRUPTED: (should be 0xCCCCCCCC): "), _memoryGuard);
		}
		*pc = _pc;
		*stack = &_executionStack;
		*locals = &_locals;
		*arguments = &_arguments;
	}
	
	void UpdateArg(int argNo, uint32_t value)
	{
		_arguments.Set(argNo, value);
	}
	
	void UpdatePc(short pc)
	{
		if (_memoryGuard != 0xCCCCCCCC)
		{
			Firmata.sendString(F("FATAL: MEMORY CORRUPTED2: (should be 0xCCCCCCCC): "), _memoryGuard);
		}
		
		_pc = pc;
	}
	
	int MethodIndex()
	{
		return _codeReference;
	}
};


class FirmataIlExecutor: public FirmataFeature
{
  public:
    FirmataIlExecutor();
    boolean handlePinMode(byte pin, int mode) override;
    void handleCapability(byte pin) override;
    boolean handleSysex(byte command, byte argc, byte* argv) override;
    void reset() override;
	void runStep();
 
  private:
    void LoadIlDataStream(byte codeReference, byte codeLength, byte offset, byte argc, byte* argv);
	void LoadIlDeclaration(byte codeReference, int flags, byte maxLocals, byte argc, byte* argv);
	void LoadMetadataTokenMapping(byte codeReference, byte argc, byte* argv);
	void DecodeParametersAndExecute(byte codeReference, byte argc, byte* argv);
	bool IsExecutingCode();
	void KillCurrentTask();
	void SendAck(ExecutorCommand subCommand);
	void SendNack(ExecutorCommand subCommand, ExecutionError errorCode);
	MethodState ExecuteIlCode(ExecutionState *state, uint32_t* returnValue);
	IlCode* ResolveToken(byte codeReference, uint32_t token);
	uint32_t DecodeUint32(byte* argv);
    void SendExecutionResult(byte codeReference, uint32_t result, MethodState execResult);
	IlCode* GetMethodByToken(uint32_t token);
	IlCode* GetMethodByCodeReference(byte codeReference);
	void AttachToMethodList(IlCode* newCode);
	IlCode* _firstMethod;

	// Note: To prevent heap fragmentation, only one method can be running at a time. This will be non-null while running
	// and everything will be disposed afterwards.
	ExecutionState* _methodCurrentlyExecuting;
};


#endif 
