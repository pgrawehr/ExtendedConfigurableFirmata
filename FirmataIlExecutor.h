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
#include "ClassDeclaration.h"
#include "MethodBody.h"
#include "GarbageCollector.h"

#include "interface/NativeMethod.h"
#include "interface/SystemException.h"
#include "interface/MethodFlags.h"
#include "interface/KnownTypeTokens.h"
#include "interface/ExecutorCommand.h"
#include "interface/VariableKind.h"
#include "interface/DebuggerCommand.h"
#include "interface/BreakpointType.h"

class LowlevelInterface;
using namespace stdSimple;

#ifndef NO_DEBUGGER_SUPPORT
#define DEBUGGER 1
#endif

#define IL_EXECUTOR_SCHEDULER_COMMAND 0xFF

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

enum class TriStateBool
{
	False = 0,
	True = 1,
	Neither = -1,
};

#define GENERIC_TOKEN_MASK 0xFF800000
#define SPECIAL_TOKEN_MASK 0xFF000000
#define NULLABLE_TOKEN_MASK 0x00800000
#define ARRAY_DATA_START 12 /* Array type token, array length (in elements), and array content type token */
#define STRING_DATA_START 8 /* String type token, string length (in chars) */
#define SIZEOF_VOID (sizeof(void*))
#define SIZEOF_CHAR (sizeof(uint16_t))
#define STANDARD_INPUT_HANDLE 0xCEED
#define STANDARD_OUTPUT_HANDLE 0xCEEE
#define STANDARD_ERROR_HANDLE 0xCEEF

#define MAX_THREADS 10
#define MAX_LOCKS 10
#define MAX_HANDLES 10
#define CACHE_LINES 4

const int NUM_INSTRUCTIONS_AT_ONCE = 50;

// The function prototype for critical finalizer functions (closing file handles, releasing mutexes etc.)
typedef void (*FinalizerFunction)(void*);

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

/// <summary>
/// Represents the current exception handler stack
/// This is used to track returning to the correct position within a handler, particularly if
/// multiple handlers are used within each other
/// </summary>
class ExceptionFrame
{
public:
	ExceptionFrame(ExceptionClause* clause)
	{
		Clause = clause;
		ContinuationPc = 0;
		Next = nullptr;
	}
	ExceptionClause* Clause;
	// The PC to load when this handler is left with endfinally. 0 if we're going to rethrow then
	uint16_t ContinuationPc;
	ExceptionFrame* Next;
	Variable Exception; // Is always of type object, therefore can be stored inline
};

class VariableIterator
{
public:
	VariableIterator()
	{
		CurrentClass = nullptr;
		BottomClass = nullptr;
		CurrentIndex = 0;
	}

	int CurrentIndex;
	/// <summary>
	/// The class being iterated (a parent of the one below)
	/// </summary>
	ClassDeclaration* CurrentClass;
	/// <summary>
	/// The class on which the iteration is being performed (the most derived in the chain)
	/// </summary>
	ClassDeclaration* BottomClass;
};


class ExecutionState
{
	private:
	uint16_t _pc;
	VariableDynamicStack _executionStack;
	VariableVector _locals;
	VariableVector _arguments;
	int _taskId;
	
	public:
	// Next inner execution frame (the innermost frame is being executed) 
	ExecutionState* _next;
	MethodBody* _executingMethod;
	VariableList _localStorage; // Memory allocated by localloc
	ExceptionFrame* _exceptionFrame;

	ExecutionState(int taskId, uint16_t maxStack, MethodBody* executingMethod) :
		_pc(0), _executionStack(MAX(maxStack, 10)),
		_locals(), _arguments(), _exceptionFrame(nullptr)
	{
		// Firmata.sendString(F("ExecutionState ctor"));
		_locals.InitFrom(executingMethod->NumberOfLocals(), executingMethod->GetLocalsIterator());
		_arguments.InitFrom(executingMethod->NumberOfArguments(), executingMethod->GetArgumentTypesIterator());
		_taskId = taskId;
		_next = nullptr;
		_executingMethod = executingMethod;
	}
	~ExecutionState()
	{
		_next = nullptr;
		while (_exceptionFrame)
		{
			ExceptionFrame *temp = _exceptionFrame->Next;
			delete _exceptionFrame->Next;
			_exceptionFrame = temp;
		}

		_exceptionFrame = nullptr;
	}
	
	void ActivateState(uint16_t* pc, VariableDynamicStack** stack, VariableVector** locals, VariableVector** arguments)
	{
		*pc = _pc;
		*stack = &_executionStack;
		*locals = &_locals;
		*arguments = &_arguments;
	}

	void SetArgumentValue(int argNo, Variable& variable)
	{
		_arguments[argNo].Type = variable.Type;
		_arguments[argNo].setSize(variable.fieldSize());
		_arguments[argNo].Int64 = variable.Int64;
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
		
	void UpdatePc(uint16_t pc)
	{
		_pc = pc;
	}

	uint16_t CurrentPc()
	{
		return _pc;
	}
	
	int TaskId() const
	{
		return _taskId;
	}

	int MethodToken() const
	{
		if (_executingMethod != nullptr)
		{
			return _executingMethod->methodToken;
		}

		return 0;
	}
};

class ThreadState
{
public:
	ThreadState(int id)
		: threadStatics(), managedThreadInstance(VariableKind::Object)
	{
		rootOfExecutionStack = nullptr;
		threadId = id;
		threadFlags = 0;
		waitTimeout = -2; // Not set
	}

	int threadId;
	ExecutionState* rootOfExecutionStack;
	RuntimeException currentException;
	VariableList threadStatics;
	Variable managedThreadInstance;
	// Bit 0: Thread pool thread (for IsThreadPoolThread property)
	// Bit 1: Background thread (for IsBackground property)
	int threadFlags;
	int waitTimeout; // global per-thread variable for timeouts in wait functions
};

class MonitorLock
{
public:
	MonitorLock()
	{
		object = nullptr;
		owningThread = nullptr;
		lockCount = 0;
		endTime = 0;
	}

	void* object;
	ThreadState* owningThread;
	int lockCount;
	int endTime;
};

class EventWaitHandle
{
public:
	EventWaitHandle()
	{
		handle = -1;
		signaled = false;
		flags = 0;
	}

	int handle;
	bool signaled;
	byte flags; // 1 = Manual reset 2 = Initially signaled
};

class Breakpoint
{
public:
	Breakpoint()
	{
		Kind = BreakpointType::None;
		MethodToken = 0;
		Pc = 0;
	}

	BreakpointType Kind;
	int MethodToken;
	int Pc;
};

class ScopeLock
{
public:
	ScopeLock()
	{
		// TODO: Take a global mutex, so that really only one CPU core can be in a scope lock
		noInterrupts();
	}

	~ScopeLock()
	{
		interrupts();
	}
};

class FieldLookupCacheEntry
{
public:
	FieldLookupCacheEntry()
	{
		FieldToken = 0;
		Cls = nullptr;
	}

	int FieldToken;
	ClassDeclaration* Cls;
};


class FirmataIlExecutor: public FirmataFeature
{
	// Because we need to get all variables.
	friend class GarbageCollector;
 public:
    FirmataIlExecutor();
	void ClearHandles();
	bool AutoStartProgram();
    boolean handlePinMode(byte pin, int mode) override;
    void handleCapability(byte pin) override;

	boolean handleSysex(byte command, byte argc, byte* argv) override;
	void reset() override;
	int ThreadToSchedule();
	void report(bool elapsed) override;

	void Init();

	// These are used by HardwareAccess methods
	static ClassDeclaration* GetClassDeclaration(Variable& obj);
	Variable GetField(ClassDeclaration* type, const Variable& instancePtr, int fieldNo);
	ClassDeclaration* GetClassWithToken(int token, bool throwIfNotFound = true)
	{
		return _classes.GetClassWithToken(token, throwIfNotFound);
	}

	Variable GetExceptionObjectFromToken(SystemException exceptionType, const char* errorMessage);

	static char* GetAsUtf8String(Variable& string);
	static char* GetAsUtf8String(const wchar_t* stringData, int length);
	TriStateBool MonitorTryEnter(ThreadState* currentThread, void* object, int timeout);
	bool MonitorExit(ThreadState* currentThread, void* object, bool throwIfNotOwned);
	ThreadState* FindThread(Variable& threadVar) const;
	TriStateBool MonitorWait(ThreadState* currentThread, void* object, int timeout);

	void SetLastError(int error)
	{
		_lastError = error;
	}

	int GetLastError()
	{
		return _lastError;
	}

	bool IsExecutingCode();

 private:
	ExecutionError LoadInterfaces(int32_t classToken, byte argc, byte* argv);
	void SendReplyHeader(ExecutorCommand subCommand);
	ExecutionError LoadIlDataStream(int token, uint16_t codeLength, uint16_t offset, byte argc, byte* argv);
	ExecutionError LoadIlDeclaration(int token, int flags, byte maxLocals, byte argCount, NativeMethod nativeMethod);
	ExecutionError LoadMethodSignature(int methodToken, byte signatureType, byte argc, byte* argv);
	ExecutionError LoadClassSignature(bool isLastPart, int32_t classToken, uint32_t parent, uint16_t dynamicSize, uint16_t staticSize, uint16_t flags, uint16_t offset, byte argc, byte* argv);
	ExecutionError PrepareStringLoad(uint32_t constantSize, uint32_t stringListSize);
	ExecutionError LoadConstant(ExecutorCommand executorCommand, uint32_t constantToken, uint32_t currentEntryLength, uint32_t offset, byte argc, byte* argv);
	ExecutionError LoadSpecialTokens(uint32_t totalListLength, uint32_t offset, byte argc, byte* argv);
	ExecutionError LoadExceptionClause(int methodToken, int clauseType, int tryOffset, int tryLength, int handlerOffset, int handlerLength, int exceptionFilterToken);
	ExecutionError ExecuteDebuggerCommand(ExecutionState* state, DebuggerCommand cmd, uint32_t arg1, uint32_t arg2);
	ExecutionError LoadGlobalMetadata(uint32_t staticVectorMemorySize);

	int ReverseSearchSpecialTypeList(int32_t genericToken, bool tokenListContainsTypes, void* tokenList);
	int ReverseSearchSpecialTypeList(int mainToken, void* tokenList, bool tokenListContainsTypes, const int* searchList);
    int* GetSpecialTokenListEntryCore(int* tokenList, int token, bool searchWithMainToken);
    int* GetSpecialTokenListEntry(int token, bool searchWithMainToken);
	bool ExecuteSpecialMethod(ThreadState* currentThread, ExecutionState* currentFrame, NativeMethod method,
	                          const VariableVector& args, Variable&
	                          result);

	byte* Ldsfld(ThreadState* thread, int token, VariableDescription& description);
	Variable* FindStaticField(int32_t token) const;
	Variable Ldsflda(ThreadState* thread, int token);
    void Stsfld(ThreadState* thread, int token, Variable& value);
	Variable* CollectFields(ClassDeclaration* vtable, VariableIterator& iterator);

	byte* Ldfld(Variable& obj, int32_t token, VariableDescription& description);
	Variable Ldflda(Variable& obj, int32_t token);
	void* Stfld(MethodBody* currentMethod, Variable& obj, int32_t token, Variable& var);
	Variable Box(Variable& value, ClassDeclaration* ty);

	void ClearExecutionStack(VariableDynamicStack* stack);
    MethodState BasicStackInstructions(ExecutionState* state, uint16_t PC, VariableDynamicStack* stack, VariableVector* locals, VariableVector* arguments,
	                                   OPCODE instr, Variable& value1, Variable& value2, Variable& value3);
	int AllocateArrayInstance(int tokenOfArrayType, int numberOfElements, Variable& result);

    ExecutionError DecodeParametersAndExecute(int methodToken, int taskId, byte argc, byte* argv);
	uint32_t DecodePackedUint32(byte* argv);
	uint64_t DecodePackedUint64(byte* argv);
	byte* AllocGcInstance(size_t bytes);
	void KillCurrentTask();
	void TerminateAllThreads();
	void CleanStack(ExecutionState* state);
	void CleanStack(int activeThreadId);
	void SendAckOrNack(ExecutorCommand subCommand, byte sequenceNo, ExecutionError errorCode);
	void InvalidOpCode(uint16_t pc, OPCODE opCode);
	void GetTypeFromHandle(ExecutionState* currentFrame, Variable& result, Variable type);
    int GetHandleFromType(Variable& object) const;
    MethodState IsAssignableFrom(ClassDeclaration* typeToAssignTo, const Variable& object);
    void SetField4(ClassDeclaration* type, const Variable& data, Variable& instance, int fieldNo);
    Variable& GetVariableDescription(ClassDeclaration* vtable, int32_t token);
    int MethodMatchesArgumentTypes(MethodBody* declaration, Variable& argumentArray);
	bool LocateCatchHandler(ThreadState* threadState, ExecutionState*& state, int tryBlockOffset,
	                        Variable& exceptionToHandle, ExceptionClause** clauseThatMatches);
	bool CheckForBreakCondition(ExecutionState* state, uint16_t pc, byte* code);
	void SendDebugState(ExecutionState* executionState);
	void SendVariables(ExecutionState* stackFrame, uint32_t frameNo, int variableType);
	void SendVariable(const Variable& variable, int& idx);
	MethodState ExecuteIlCode(ThreadState* threadState, Variable* returnValue);
	ExecutionState* PreviousStackFrame(ThreadState* thread, ExecutionState* currentFrame) const;
	void SignExtend(Variable& variable, int inputSize);
	ClassDeclaration* GetTypeFromTypeInstance(Variable& ownTypeInstance);
	bool StringEquals(const VariableVector& args, int stringComparison);
	bool StringEquals(const VariableVector& args);
	void CreateFatalException(ThreadState* threadWithException, SystemException exception, Variable& managedException, int hintToken);
    void* CreateInstance(ClassDeclaration* cls);
	void* CreateInstanceOfClass(int32_t typeToken, uint32_t length, bool throwIfNotFound = true);
    ClassDeclaration* ResolveClassFromCtorToken(int32_t ctorToken);
    ClassDeclaration* ResolveClassFromFieldToken(int32_t fieldToken);
    static uint16_t SizeOfClass(ClassDeclaration* cls);
	uint32_t DecodeUint32(byte* argv);
	void SendUint32(uint32_t value);
	uint16_t DecodePackedUint14(byte* argv);
    void SendExecutionResult(int32_t codeReference, RuntimeException& lastState, Variable returnValue, MethodState execResult);
	MethodBody* GetMethodByToken(int32_t token);
	ExecutionState* GetNthMethodOnStack(ExecutionState* state, uint32_t& n);
	void SendPackedUInt32(uint32_t value);
	void SendPackedUInt64(uint64_t value);
	bool InitializeMainThread(ExecutionState* rootState);
	uint32_t ReadUint32FromArbitraryAddress(byte* pCode);
	uint16_t CreateExceptionFrame(ExecutionState* currentFrame, uint16_t continuationAddress, ExceptionClause* c, Variable &exception);
	Variable CreateStringInstance(size_t length, const char* string);
	void SendQueryHardwareReply();

	char* GetString(int stringToken, int& length);
	byte* GetString(byte* heap, int stringToken, int& length);
	byte* GetConstant(int token);

	void* CopyStringsToFlash();
	int* CopySpecialTokenListToFlash();

	void InitStaticVector();
	static bool IsThreadSpecific(ThreadState* thread, Variable* variable);

	GarbageCollector _gc;
	uint32_t _instructionsExecuted;
	uint32_t _taskStartTime;
	ThreadState* _threads[MAX_THREADS];
	MonitorLock _activeLocks[MAX_LOCKS]; // Monitor locks - assume a constant maximum number of simultaneous locks
	EventWaitHandle _waitHandles[MAX_HANDLES];
	int _lastThreadRun;

	SortedClassList _classes;
	SortedMethodList _methods;
	SortedClauseList _clauses;

	uint32_t _staticVectorMemorySize;
	byte* _staticVector;

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
	FlashMemoryManager* _flashMemoryManager;
	stdSimple::vector<LowlevelInterface*> _lowLevelLibraries;
	uint32_t _lastError;

	// The debugger is active
	bool _debuggerEnabled;

	// We're sitting on a break point
	bool _debugBreakActive;

	bool _breakOnException;
	// The number of commands to skip until we check for breakpoints again.
	// This is used to make sure a continue or single step command executes at least one command.
	int _commandsToSkip;
	int _debuggingThread; // The thread that is currently being single-stepped trough (-1 for all)

	stdSimple::vector<Breakpoint> _breakpoints;
	Breakpoint _nextStepBehavior;

	FieldLookupCacheEntry _fieldLookupCache[CACHE_LINES];
	bool _nextLookup;
public:
	// Currently public, because DependentHandle is separate
	stdSimple::vector<pair<void*, void*>> _weakDependencies; // Garbage collector weak dependencies (created using DependentObject)
	stdSimple::vector<pair<FinalizerFunction, void*>> _criticalFinalizers;
};


#endif 
