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
#include "VariableKind.h"
#include "MethodBody.h"
#include "GarbageCollector.h"

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
	CopyToFlash = 12,
	WriteFlashHeader = 13,
	CheckFlashVersion = 14,
	EraseFlash = 15,
	SetConstantMemorySize = 16,
	SpecialTokenList = 17,
	
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
#define SPECIAL_TOKEN_MASK 0xFF000000
#define NULLABLE_TOKEN_MASK 0x00800000
#define ARRAY_DATA_START 12 /* Array type token, array length (in elements), and array content type token */
#define STRING_DATA_START 8 /* String type token, string length (in chars) */
#define SIZEOF_CHAR (sizeof(uint16_t))

class RuntimeException
{
public:
	static const int MaxStackTokens = 10;
	RuntimeException()
		: ExceptionObject(VariableKind::Void)
	{
		TokenOfException = 0;
		ExceptionType = SystemException::None;
		for (int i = 0; i < MaxStackTokens; i++)
		{
			StackTokens[i] = 0;
			PerStackPc[i] = 0;
		}
	}
	RuntimeException(SystemException type, Variable exceptionObject, int tokenOfException = 0)
	{
		TokenOfException = tokenOfException;
		ExceptionType = type;
		ExceptionObject = exceptionObject;
		for (int i = 0; i < MaxStackTokens; i++)
		{
			StackTokens[i] = 0;
			PerStackPc[i] = 0;
		}
	}
	
	int TokenOfException;
	SystemException ExceptionType;
	Variable ExceptionObject;
	// List of method tokens for the stack list
	int StackTokens[MaxStackTokens];
	// For each of the above, the PC in the corresponding frame
	short PerStackPc[MaxStackTokens];
};


class ExecutionState
{
	private:
	u16 _pc;
	VariableDynamicStack _executionStack;
	VariableVector _locals;
	VariableVector _arguments;
	u16 _taskId;
	
	public:
	// Next inner execution frame (the innermost frame is being executed) 
	ExecutionState* _next;
	MethodBody* _executingMethod;
	VariableList _localStorage; // Memory allocated by localloc

	u32 _memoryGuard;
	
	ExecutionState(u16 taskId, u16 maxStack, MethodBody* executingMethod) :
		_pc(0), _executionStack(10),
		_locals(), _arguments()
	{
		_locals.InitFrom(executingMethod->NumberOfLocals(), executingMethod->GetLocalsIterator());
		_arguments.InitFrom(executingMethod->NumberOfArguments(), executingMethod->GetArgumentTypesIterator());
		_taskId = taskId;
		_next = nullptr;
		_executingMethod = executingMethod;
	}
	~ExecutionState()
	{
		_next = nullptr;
	}
	
	void ActivateState(u16* pc, VariableDynamicStack** stack, VariableVector** locals, VariableVector** arguments)
	{
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
		_pc = pc;
	}

	u16 CurrentPc()
	{
		return _pc;
	}
	
	int TaskId()
	{
		return _taskId;
	}
};


class FirmataIlExecutor: public FirmataFeature
{
	// Because we need to get all variables.
	friend class GarbageCollector;
  public:
    FirmataIlExecutor();
    bool AutoStartProgram();
    boolean handlePinMode(byte pin, int mode) override;
    void handleCapability(byte pin) override;
    
    boolean handleSysex(byte command, byte argc, byte* argv) override;
    void reset() override;
	void report(bool elapsed) override;

	void Init();
public:

	// These are used by HardwareAccess methods
	static ClassDeclaration* GetClassDeclaration(Variable& obj);
	Variable GetField(ClassDeclaration* type, const Variable& instancePtr, int fieldNo);
	ClassDeclaration* GetClassWithToken(int token, bool throwIfNotFound = true)
	{
		return _classes.GetClassWithToken(token, throwIfNotFound);
	}

  private:
	ExecutionError LoadInterfaces(int32_t classToken, byte argc, byte* argv);
	ExecutionError LoadIlDataStream(int token, u16 codeLength, u16 offset, byte argc, byte* argv);
	ExecutionError LoadIlDeclaration(int token, int flags, byte maxLocals, byte argCount, NativeMethod nativeMethod);
	ExecutionError LoadMethodSignature(int methodToken, byte signatureType, byte argc, byte* argv);
	ExecutionError LoadClassSignature(bool isLastPart, u32 classToken, u32 parent, u16 dynamicSize, u16 staticSize, u16 flags, u16 offset, byte argc, byte* argv);
	ExecutionError PrepareStringLoad(uint32_t constantSize, uint32_t stringListSize);
	ExecutionError LoadConstant(ExecutorCommand executorCommand, uint32_t constantToken, uint32_t currentEntryLength, uint32_t offset, byte argc, byte* argv);
	ExecutionError LoadSpecialTokens(uint32_t totalListLength, uint32_t offset, byte argc, byte* argv);

    int ReverseSearchSpecialTypeList(int mainToken, Variable& tokenList, const int* searchList);
    int* GetSpecialTokenListEntryCore(int* tokenList, int token, bool searchWithMainToken);
    int* GetSpecialTokenListEntry(int token, bool searchWithMainToken);
	void ExecuteSpecialMethod(ExecutionState* state, NativeMethod method, const VariableVector &args, Variable& result);

	Variable& Ldsfld(int token);
	Variable Ldsflda(int token);
    void Stsfld(int token, Variable& value);
    void CollectFields(ClassDeclaration* vtable, vector<Variable*>& vector);

	byte* Ldfld(Variable& obj, int32_t token, VariableDescription& description);
	Variable Ldflda(Variable& obj, int32_t token);
	void* Stfld(MethodBody* currentMethod, Variable& obj, int32_t token, Variable& var);
	Variable Box(Variable& value, ClassDeclaration* ty);
	
    MethodState BasicStackInstructions(ExecutionState* state, u16 PC, VariableDynamicStack* stack, VariableVector* locals, VariableVector* arguments,
                                       OPCODE instr, Variable& value1, Variable& value2, Variable& value3);
    int AllocateArrayInstance(int token, int size, Variable& v1);

    ExecutionError DecodeParametersAndExecute(int methodToken, u16 taskId, byte argc, byte* argv);
	uint32_t DecodePackedUint32(byte* argv);
	uint64_t DecodePackedUint64(byte* argv);
	byte* AllocGcInstance(size_t bytes);
	bool IsExecutingCode();
	void KillCurrentTask();
	void CleanStack(ExecutionState* state);
	void SendAckOrNack(ExecutorCommand subCommand, ExecutionError errorCode);
	void InvalidOpCode(u16 pc, OPCODE opCode);
	void GetTypeFromHandle(ExecutionState* currentFrame, Variable& result, Variable type);
    int GetHandleFromType(Variable& object) const;
    MethodState IsAssignableFrom(ClassDeclaration* typeToAssignTo, const Variable& object);
    void SetField4(ClassDeclaration* type, const Variable& data, Variable& instance, int fieldNo);
    Variable& GetVariableDescription(ClassDeclaration* vtable, int32_t token);
    int MethodMatchesArgumentTypes(MethodBody* declaration, Variable& argumentArray);
    MethodState ExecuteIlCode(ExecutionState *state, Variable* returnValue);
	ClassDeclaration* GetTypeFromTypeInstance(Variable& ownTypeInstance);
    void CreateException(SystemException exception, Variable& managedException, int hintToken);
    void* CreateInstance(ClassDeclaration* cls);
	void* CreateInstanceOfClass(int32_t typeToken, u32 length);
    ClassDeclaration* ResolveClassFromCtorToken(int32_t ctorToken);
    ClassDeclaration* ResolveClassFromFieldToken(int32_t fieldToken);
    static uint16_t SizeOfClass(ClassDeclaration* cls);
	uint32_t DecodeUint32(byte* argv);
	void SendUint32(uint32_t value);
	uint16_t DecodePackedUint14(byte* argv);
    void SendExecutionResult(u16 codeReference, RuntimeException& lastState, Variable returnValue, MethodState execResult);
	MethodBody* GetMethodByToken(int32_t token);
	void SendPackedInt32(int32_t value);
	void SendPackedInt64(int64_t value);

	byte* GetString(int stringToken, int& length);
	byte* GetString(byte* heap, int stringToken, int& length);
	byte* GetConstant(int token);

	void* CopyStringsToFlash();
	int* CopySpecialTokenListToFlash();

	GarbageCollector _gc;
	uint32_t _instructionsExecuted;
	uint32_t _taskStartTime;

	// Note: To prevent heap fragmentation, only one method can be running at a time. This will be non-null while running
	// and everything will be disposed afterwards.
	ExecutionState* _methodCurrentlyExecuting;
	RuntimeException _currentException;

	SortedClassList _classes;
	SortedMethodList _methods;

	// The list of static variables (global)
	stdSimple::map<u32, Variable> _statics;

	VariableList _largeStatics;

	// Constant data fields (such as array initializers). Does not include the string heap
	SortedConstantList _constants;

	// The string heap. Just a bunch of strings. The token/length field is used to iterate trough it
	byte* _stringHeapRam;
	byte* _stringHeapFlash;
	
	uint32_t _stringHeapRamSize;

	int* _specialTypeListRam;
	int* _specialTypeListFlash;
	uint32_t _specialTypeListRamLength;

	int _startupToken;
	int _startupFlags;
	bool _startedFromFlash;

	// An empty instance of type Variable, used for error returns where a "null reference" would be required
	Variable _clearVariable;
};


#endif 
