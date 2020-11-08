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
#include <avr_stl.h>
#include <basic_definitions>
#include <cstddef>
#include <new>
#include <vector>
#include <stack>

#include "openum.h"
using namespace std;

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
	MethodSignature = 7,

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
	InvalidArguments = 2,
	OutOfMemory = 3,
};

enum class VariableKind : byte
{
	Void = 0,
	Uint32 = 1,
	Int32 = 2,
	Boolean = 3,
	Object = 4,
};

struct Variable
{
	// Important: Data must come first (because we sometimes take the address of this)
	union
	{
		uint32_t Uint32;
		int32_t Int32;
		bool Boolean;
		void* Object;
	};

	VariableKind Type;

	Variable(uint32_t value, VariableKind type)
	{
		Uint32 = value;
		Type = type;
	}

	Variable(int32_t value, VariableKind type)
	{
		Int32 = value;
		Type = type;
	}

	Variable()
	{
		Uint32 = 0;
		Type = VariableKind::Void;
	}
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

	u32 methodToken; // Primary method token (a methodDef token)
	byte methodFlags;
	u16 methodLength;
	byte codeReference;
	// For special methods (see methodFlags field), this contains the method number
	byte maxLocals;
	vector<VariableKind> localTypes;
	byte numArgs;
	vector<VariableKind> argumentTypes;
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
	u16 _pc;
	std::stack<Variable> _executionStack;
	std::vector<Variable> _locals;
	std::vector<Variable> _arguments;
	int _codeReference;
	
	public:
	// Next inner execution frame (the innermost frame is being executed) 
	ExecutionState* _next;
	IlCode* _executingMethod;

	u32 _memoryGuard;
	ExecutionState(int codeReference, unsigned maxLocals, unsigned argCount, IlCode* executingMethod) :
	_pc(0), _executionStack(),
	_locals(maxLocals), _arguments(argCount)
	{
		_codeReference = codeReference;
		_next = nullptr;
		_executingMethod = executingMethod;
		for(unsigned i = 0; i < maxLocals && i < executingMethod->localTypes.size(); i++)
		{
			// Initialize locals with correct type
			_locals.at(i).Uint32 = 0;
			_locals.at(i).Type = executingMethod->localTypes.at(i);
		}

		for (unsigned i = 0; i < executingMethod->numArgs && i < executingMethod->argumentTypes.size(); i++)
		{
			// Initialize locals with correct type
			_arguments.at(i).Uint32 = 0;
			_arguments.at(i).Type = executingMethod->argumentTypes.at(i);
		}
		
		_memoryGuard = 0xCCCCCCCC;
	}
	~ExecutionState()
	{
		_next = nullptr;
		_memoryGuard = 0xDEADBEEF;
	}
	
	void ActivateState(u16* pc, stack<Variable>** stack, vector<Variable>** locals, vector<Variable>** arguments)
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
	
	void SetArgumentValue(int argNo, uint32_t value)
	{
		// Doesn't matter which actual value it is - we're just byte-copying here
		_arguments[argNo].Uint32 = value;
	}
	
	void UpdatePc(u16 pc)
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
	
	
	FlashString* name()
	{
		return F("IL");
	}
 
  private:
    ExecutionError LoadIlDataStream(byte codeReference, u16 codeLength, u16 offset, byte argc, byte* argv);
	ExecutionError LoadIlDeclaration(byte codeReference, int flags, byte maxLocals, byte argc, byte* argv);
	ExecutionError LoadMethodSignature(byte codeReference, byte signatureType, byte argc, byte* argv);
	ExecutionError LoadMetadataTokenMapping(byte codeReference, u16 tokens, u16 offset, byte argc, byte* argv);

	static Variable ExecuteSpecialMethod(byte method, const vector<Variable> &args);
    MethodState BasicStackInstructions(u16 PC, stack<Variable>* stack, vector<Variable>* locals, vector<Variable>* arguments,
                                OPCODE instr, Variable value1, Variable value2);

    void DecodeParametersAndExecute(byte codeReference, byte argc, byte* argv);
	bool IsExecutingCode();
	void KillCurrentTask();
	void SendAckOrNack(ExecutorCommand subCommand, ExecutionError errorCode);
	void InvalidOpCode(u16 pc, u16 opCode);
	MethodState ExecuteIlCode(ExecutionState *state, Variable* returnValue);
	IlCode* ResolveToken(byte codeReference, uint32_t token);
	uint32_t DecodeUint32(byte* argv);
    void SendExecutionResult(byte codeReference, Variable returnValue, MethodState execResult);
	IlCode* GetMethodByToken(uint32_t token);
	IlCode* GetMethodByCodeReference(byte codeReference);
	void AttachToMethodList(IlCode* newCode);
	IlCode* _firstMethod;

	// Note: To prevent heap fragmentation, only one method can be running at a time. This will be non-null while running
	// and everything will be disposed afterwards.
	ExecutionState* _methodCurrentlyExecuting;
};


#endif 
