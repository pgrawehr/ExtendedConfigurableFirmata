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
#include "ObjectStack.h"
#include "ObjectVector.h"
#include "ObjectMap.h"
#include "openum.h"
#include "Variable.h"
#include "VariableVector.h"
#include "VariableDynamicStack.h"
#include "VariableList.h"
#include "NativeMethod.h"
#include "SystemException.h"
#include "MethodFlags.h"
#include "KnownTypeTokens.h"
#include "ClassDeclaration.h"

using namespace stdSimple;

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
	ClassDeclaration = 8,
	ClassDeclarationEnd = 9,
	ConstantData = 10,
	Interfaces = 11,
	
	Nack = 0x7e,
	Ack = 0x7f,
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
	InternalError = 4,
};

#define GENERIC_TOKEN_MASK 0xFF800000
#define NULLABLE_TOKEN_MASK 0x00800000


class RuntimeException
{
public:
	RuntimeException(SystemException type, Variable arg0)
		: ExceptionArgs(1, 1)
	{
		TokenOfException = 0;
		ExceptionType = type;
		ExceptionArgs.at(0) = arg0;
	}
	
	int TokenOfException;
	SystemException ExceptionType;
	vector<Variable> ExceptionArgs;
	vector<int> StackTokens;
};


class MethodBody
{
public:
	MethodBody(byte flags, byte numArgs, byte maxStack)
	{
		methodToken = 0;
		_methodFlags = flags;
		methodLength = 0;
		methodIl = nullptr;
		_numArgs = numArgs;
		_maxStack = maxStack;
		next = nullptr;
		codeReference = -1;
		nativeMethod = NativeMethod::None;
	}

	~MethodBody()
	{
		Clear();
	}

private:
	/// <summary>
	/// Clear the current entry, so it can be reused.
	/// </summary>
	void Clear()
	{
		methodToken = 0;
		if (methodIl != nullptr)
		{
			free(methodIl);
			methodIl = nullptr;
			methodLength = 0;
		}

		codeReference = -1;
	}

public:
	byte NumberOfArguments() const
	{
		return _numArgs;
	}

	byte MaxExecutionStack() const
	{
		return _maxStack;
	}

	byte MethodFlags() const
	{
		return _methodFlags;
	}

	int32_t methodToken; // Primary method token (a methodDef token)
	u16 methodLength;
	u16 codeReference;
	
	vector<VariableDescription, byte> localTypes;
	vector<VariableDescription, byte> argumentTypes;
	byte* methodIl;
	// Native method number
	NativeMethod nativeMethod;
	MethodBody* next;

private:
	byte _numArgs;
	byte _maxStack;
	byte _methodFlags;
};

class ExecutionState
{
	private:
	u16 _pc;
	VariableDynamicStack _executionStack;
	VariableVector _locals;
	VariableVector _arguments;
	u16 _codeReference;
	
	public:
	// Next inner execution frame (the innermost frame is being executed) 
	ExecutionState* _next;
	MethodBody* _executingMethod;
	RuntimeException* _runtimeException;
	VariableList _localStorage; // Memory allocated by localloc

	u32 _memoryGuard;
	
	ExecutionState(u16 codeReference, u16 maxStack, MethodBody* executingMethod) :
		_pc(0), _executionStack(10),
		_locals(), _arguments(),
		_runtimeException(nullptr)
	{
		_locals.InitFrom(executingMethod->localTypes);
		_arguments.InitFrom(executingMethod->argumentTypes);
		_codeReference = codeReference;
		_next = nullptr;
		_runtimeException = nullptr;
		_executingMethod = executingMethod;
		_memoryGuard = 0xCCCCCCCC;
	}
	~ExecutionState()
	{
		_next = nullptr;
		if (_runtimeException != nullptr)
		{
			delete _runtimeException;
			_runtimeException = nullptr;
		}
		_memoryGuard = 0xDEADBEEF;
	}
	
	void ActivateState(u16* pc, VariableDynamicStack** stack, VariableVector** locals, VariableVector** arguments)
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
	
	void SetArgumentValue(int argNo, uint32_t value, VariableKind type)
	{
		// Doesn't matter which actual value it is - we're just byte-copying here
		_arguments[argNo].Uint32 = value;
		_arguments[argNo].Type = type;
	}

	void SetArgumentValue(int argNo, uint64_t value, VariableKind type)
	{
		// Doesn't matter which actual value it is - we're just byte-copying here
		_arguments[argNo].Uint64 = value;
		_arguments[argNo].Type = type;
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
	void report(bool elapsed) override;

	void Init();
public:

	// These are used by HardwareAccess methods
	static ClassDeclaration* GetClassDeclaration(Variable& obj);
	static Variable GetField(ClassDeclaration* type, const Variable& instancePtr, int fieldNo);
 
  private:
	ExecutionError LoadInterfaces(int32_t classToken, byte argc, byte* argv);
	ExecutionError LoadIlDataStream(u16 codeReference, u16 codeLength, u16 offset, byte argc, byte* argv);
	ExecutionError LoadIlDeclaration(u16 codeReference, int flags, byte maxLocals, byte argCount, NativeMethod nativeMethod, int token);
	ExecutionError LoadMethodSignature(u16 codeReference, byte signatureType, byte argc, byte* argv);
	ExecutionError LoadClassSignature(bool isLastPart, u32 classToken, u32 parent, u16 dynamicSize, u16 staticSize, u16 flags, u16 offset, byte argc, byte* argv);
	ExecutionError LoadConstant(ExecutorCommand executor_command, uint32_t constantToken, uint32_t totalLength, uint32_t offset, byte argc, byte* argv);
	ExecutionError ReserveMemory(uint32_t classes, uint32_t methods, uint32_t constants);

	void ExecuteSpecialMethod(ExecutionState* state, NativeMethod method, const VariableVector &args, Variable& result);
	void ExceptionOccurred(ExecutionState* state, SystemException error, int32_t errorLocationToken);
	
	Variable& Ldsfld(int token);
	Variable Ldsflda(int token);
    void Stsfld(int token, Variable& value);
    void CollectFields(ClassDeclaration* vtable, vector<Variable*>& vector);

	byte* Ldfld(MethodBody* currentMethod, Variable& obj, int32_t token, VariableDescription& description);
	Variable Ldflda(Variable& obj, int32_t token);
	void* Stfld(MethodBody* currentMethod, Variable& obj, int32_t token, Variable& var);
	Variable Box(Variable& value1, ClassDeclaration& ty);
	
    MethodState BasicStackInstructions(ExecutionState* state, u16 PC, VariableDynamicStack* stack, VariableVector* locals, VariableVector* arguments,
                                       OPCODE instr, Variable& value1, Variable& value2, Variable& value3);
    int AllocateArrayInstance(int token, int size, Variable& v1);

    void DecodeParametersAndExecute(u16 codeReference, byte argc, byte* argv);
	uint32_t DecodePackedUint32(byte* argv);
	uint64_t DecodePackedUint64(byte* argv);
	byte* AllocGcInstance(size_t bytes);
	bool IsExecutingCode();
	void KillCurrentTask();
    RuntimeException* UnrollExecutionStack();
    void SendAckOrNack(ExecutorCommand subCommand, ExecutionError errorCode);
	void InvalidOpCode(u16 pc, OPCODE opCode);
	void GetTypeFromHandle(ExecutionState* currentFrame, Variable& result, Variable type);
    int GetHandleFromType(Variable& object) const;
    MethodState IsAssignableFrom(ClassDeclaration& typeToAssignTo, const Variable& object);
    void SetField4(ClassDeclaration& type, const Variable& data, Variable& instance, int fieldNo);
    Variable GetVariableDescription(ClassDeclaration* vtable, int32_t token);
    MethodState ExecuteIlCode(ExecutionState *state, Variable* returnValue);
    void* CreateInstance(ClassDeclaration& cls);
	void* CreateInstanceOfClass(int32_t typeToken, u32 length);
    ClassDeclaration& ResolveClassFromCtorToken(int32_t ctorToken);
    ClassDeclaration& ResolveClassFromFieldToken(int32_t fieldToken);
    static uint16_t SizeOfClass(ClassDeclaration* cls);
    MethodBody* ResolveToken(MethodBody* code, int32_t token);
	uint32_t DecodeUint32(byte* argv);
	void SendUint32(uint32_t value);
	uint16_t DecodePackedUint14(byte* argv);
    void SendExecutionResult(u16 codeReference, RuntimeException* lastState, Variable returnValue, MethodState execResult);
	MethodBody* GetMethodByToken(MethodBody* code, int32_t token);
	MethodBody* GetMethodByCodeReference(u16 codeReference);
	void AttachToMethodList(MethodBody* newCode);
	void SendPackedInt32(int32_t value);
	void SendPackedInt64(int64_t value);

	stdSimple::vector<void*> _gcData;
	MethodBody* _firstMethod;

	// Note: To prevent heap fragmentation, only one method can be running at a time. This will be non-null while running
	// and everything will be disposed afterwards.
	ExecutionState* _methodCurrentlyExecuting;

	SortedClassList _classes;

	// The list of static variables (global)
	stdSimple::map<u32, Variable> _statics;

	VariableList _largeStatics;

	// Constant data fields (such as array initializers or strings)
	stdSimple::map<u32, byte*> _constants;
};


#endif 
