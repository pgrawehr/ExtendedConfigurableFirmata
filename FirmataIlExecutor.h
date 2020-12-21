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

using namespace stdSimple;

#define IL_EXECUTOR_SCHEDULER_COMMAND 0xFF
#define MAX(a, b) ((a) > (b) ? (a) : (b))

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
	SendObject = 9,
	ConstantData = 10,
	Interfaces = 11,
	
	Nack = 0x7e,
	Ack = 0x7f,
};

enum class MethodFlags
{
	Static = 1,
	Virtual = 2,
	Special = 4,
	VoidOrCtor = 8,
	Abstract = 16
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

enum class KnownTypeTokens
{
	None = 0,
	Object = 1,
	Type = 2,
	ValueType = 3,
	String = 4,
	TypeInfo = 5,
	RuntimeType = 6,
	Nullable = 7,
	Enum = 8,
	LargestKnownTypeToken = 20,
};

#define GENERIC_TOKEN_MASK 0xFF800000
#define NULLABLE_TOKEN_MASK 0x00800000

enum class VariableKind : byte
{
	Void = 0, // The slot contains no data
	Uint32 = 1, // The slot contains unsigned integer data
	Int32 = 2, // The slot contains signed integer data
	Boolean = 3, // The slot contains true or false
	Object = 4, // The slot contains an object reference
	Method = 5,
	ValueArray = 6, // The slot contains a reference to an array of value types (inline)
	ReferenceArray = 7, // The slot contains a reference to an array of reference types
	Float = 8,
	Int64 = 16 + 1,
	Uint64 = 16 + 2,
	Double = 16 + 4,
	Reference = 32, // Address of a variable
	RuntimeFieldHandle = 33, // So far this is a pointer to a constant initializer
	RuntimeTypeHandle = 34, // A type handle. The value is a type token
	AddressOfVariable = 35, // An address pointing to a variable slot on another method's stack or arglist
	StaticMember = 128, // type is defined by the first value it gets
};

enum class NativeMethod
{
	None = 0,
	SetPinMode,
	WritePin,
	ReadPin,
	EnvironmentTickCount,
	SleepMicroseconds,
	GetMicroseconds,
	Debug,
	ObjectEquals,
	ObjectGetHashCode,
	ObjectReferenceEquals,
	ObjectToString,
	GetType,
	GetHashCode,
	ArrayCopy5,
	StringCtor0,
	StringLength,
	MonitorEnter1,
	MonitorEnter2,
	MonitorExit,
	StringIndexer,
	StringFormat2,
	StringFormat2b,
	DefaultEqualityComparer,
	ArrayCopy3,
	StringFormat3,
	ArrayClone,
	GetPinMode,
	IsPinModeSupported,
	GetPinCount,
	RuntimeHelpersInitializeArray,
	RuntimeHelpersRunClassConstructor,
	FailFast1,
	FailFast2,

	TypeGetTypeFromHandle,
	TypeEquals,
	TypeIsAssignableTo,
	TypeIsEnum,
	TypeTypeHandle,
	TypeIsValueType,
	TypeIsSubclassOf,
	TypeIsAssignableFrom,
	TypeCtor,
	TypeMakeGenericType,
	TypeGetHashCode,
	TypeGetGenericTypeDefinition,
	TypeGetGenericArguments,

	CreateInstanceForAnotherGenericParameter,
	ValueTypeGetHashCode,
	ValueTypeEquals,
	ValueTypeToString,
	ArrayClear,
	StringEquals,
	StringToString,
	StringGetHashCode,
	StringConcat2,
	StringConcat3,
	StringConcat4,
	StringCtor2,
	StringSetElem,
	StringGetElem,
	StringGetPinnableReference,
	BitConverterSingleToInt32Bits,
	StringEqualsStatic,
	BitOperationsLog2SoftwareFallback,
	BitOperationsTrailingZeroCount,
	StringFastAllocateString,
	EnumGetHashCode,
	EumToUInt64,
	UnsafeNullRef,
	UnsafeAs2,
};

enum class SystemException
{
	None = 0,
	StackOverflow = 1,
	NullReference = 2,
	MissingMethod = 3,
	InvalidOpCode = 4,
	DivideByZero = 5,
	IndexOutOfRange = 6,
	OutOfMemory = 7,
	ArrayTypeMismatch = 8,
	InvalidOperation = 9,
	ClassNotFound = 10,
	InvalidCast = 11,
	NotSupported = 12,
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
		uint64_t Uint64;
		int64_t Int64;
		float Float;
		double Double;
	};

	VariableKind Type;

	Variable(uint32_t value, VariableKind type)
	{
		Int64 = 0;
		Uint32 = value;
		Type = type;
	}

	Variable(int32_t value, VariableKind type)
	{
		Int64 = 0;
		Int32 = value;
		Type = type;
	}

	Variable(VariableKind type)
	{
		Int64 = 0;
		Type = type;
		Object = nullptr;
	}

	Variable()
	{
		Uint64 = 0;
		Type = VariableKind::Void;
	}

	size_t fieldSize() const
	{
		// 64 bit types have bit 4 set
		if (((int)Type & 16) != 0)
		{
			return 8;
		}
		return 4;
	}

	static size_t datasize()
	{
		return MAX(sizeof(void*), sizeof(uint64_t));
	}
};

struct Method
{
	// Our own token
	int32_t token;
	// Other method tokens that could be seen meaning this method (i.e. from virtual base implementations)
	vector<int> declarationTokens;
};

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

class ClassDeclaration
{
public:
	ClassDeclaration(int32_t token, int32_t parent, int16_t dynamicSize, int16_t staticSize, bool valueType)
	{
		ClassToken = token;
		ParentToken = parent;
		ClassDynamicSize = dynamicSize;
		ClassStaticSize = staticSize;
		ValueType = valueType;
		memberSize = 4;
	}

	~ClassDeclaration()
	{
		fieldTypes.clear();
		methodTypes.clear();
	}

	bool ValueType;
	short memberSize; // Size of the members in an instance (either 4 or 8, if the class has 64 bit members)
	int32_t ClassToken;
	int32_t ParentToken;
	uint16_t ClassDynamicSize; // Including superclasses, but without vtable
	uint16_t ClassStaticSize; // Size of static members 

	// Here, the value is the metadata token
	vector<Variable> fieldTypes;
	// List of indirectly callable methods of this class (ctors, virtual methods and interface implementations)
	vector<Method> methodTypes;
	// List of interfaces implemented by this class
	vector<int> interfaceTokens;
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
		maxLocals = 0;
		numArgs = 0;
		next = nullptr;
		codeReference = -1;
		nativeMethod = NativeMethod::None;
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
		if (methodIl != nullptr)
		{
			free(methodIl);
			methodIl = nullptr;
			methodLength = 0;
		}

		codeReference = -1;
	}

	int32_t methodToken; // Primary method token (a methodDef token)
	byte methodFlags;
	u16 methodLength;
	u16 codeReference;
	byte maxLocals;
	vector<VariableKind> localTypes;
	byte numArgs;
	vector<VariableKind> argumentTypes;
	byte* methodIl;
	// Native method number
	NativeMethod nativeMethod;
	IlCode* next;
};

class ExecutionState
{
	private:
	u16 _pc;
	stack<Variable> _executionStack;
	vector<Variable> _locals;
	vector<Variable> _arguments;
	u16 _codeReference;
	
	public:
	// Next inner execution frame (the innermost frame is being executed) 
	ExecutionState* _next;
	IlCode* _executingMethod;
	RuntimeException* _runtimeException;

	u32 _memoryGuard;
	ExecutionState(int codeReference, unsigned maxLocals, unsigned argCount, IlCode* executingMethod) :
	_pc(0), _executionStack(10),
	_locals(maxLocals, maxLocals), _arguments(argCount, argCount),
	_runtimeException(nullptr)
	{
		_codeReference = codeReference;
		_next = nullptr;
		_runtimeException = nullptr;
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
		if (_runtimeException != nullptr)
		{
			delete _runtimeException;
			_runtimeException = nullptr;
		}
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

	void SetArgumentValue(int argNo, uint64_t value)
	{
		// Doesn't matter which actual value it is - we're just byte-copying here
		_arguments[argNo].Uint64 = value;
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
	void report(bool elapsed);
 
  private:
	ExecutionError LoadInterfaces(int32_t classToken, byte argc, byte* argv);
	ExecutionError LoadIlDataStream(u16 codeReference, u16 codeLength, u16 offset, byte argc, byte* argv);
	ExecutionError LoadIlDeclaration(u16 codeReference, int flags, byte maxLocals, byte argCount, NativeMethod nativeMethod, int token);
	ExecutionError LoadMethodSignature(u16 codeReference, byte signatureType, byte argc, byte* argv);
	ExecutionError LoadClassSignature(u32 classToken, u32 parent, u16 dynamicSize, u16 staticSize, u16 flags, u16 offset, byte argc, byte* argv);
	ExecutionError ReceiveObjectData(byte argc, byte* argv);
	ExecutionError LoadConstant(ExecutorCommand executor_command, uint32_t constantToken, uint32_t totalLength, uint32_t offset, byte argc, byte* argv);

	MethodState ExecuteSpecialMethod(ExecutionState* state, NativeMethod method, const vector<Variable> &args, Variable& result);
	void ExceptionOccurred(ExecutionState* state, SystemException error, int32_t errorLocationToken);
	
    Variable Ldsfld(int token);
    void Stsfld(int token, Variable value);
	Variable Ldfld(IlCode* currentMethod, Variable& obj, int32_t token);
	void Stfld(IlCode* currentMethod, Variable& obj, int32_t token, Variable& var);
	
    MethodState BasicStackInstructions(ExecutionState* state, u16 PC, stack<Variable>* stack, vector<Variable>* locals, vector<Variable>* arguments,
                                       OPCODE instr, Variable value1, Variable value2, Variable value3);
    void AllocateArrayInstance(int token, int size, Variable& v1);

    void DecodeParametersAndExecute(u16 codeReference, byte argc, byte* argv);
	uint32_t DecodePackedUint32(byte* argv);
	uint64_t DecodePackedUint64(byte* argv);
	bool IsExecutingCode();
	void KillCurrentTask();
    RuntimeException* UnrollExecutionStack();
    void SendAckOrNack(ExecutorCommand subCommand, ExecutionError errorCode);
	void InvalidOpCode(u16 pc, OPCODE opCode);
	MethodState GetTypeFromHandle(ExecutionState* currentFrame, Variable& result, Variable type);
    int GetHandleFromType(Variable& object) const;
    MethodState IsAssignableFrom(ClassDeclaration& typeToAssignTo, const Variable& object);
    Variable GetField(ClassDeclaration& type, const Variable& instancePtr, int fieldNo);
    void SetField4(ClassDeclaration& type, const Variable& data, Variable& instance, int fieldNo);
    ClassDeclaration* GetClassDeclaration(Variable& obj);
    MethodState ExecuteIlCode(ExecutionState *state, Variable* returnValue);
    void* CreateInstance(int32_t ctorToken, SystemException* exception);
	void* CreateInstanceOfClass(int32_t typeToken, SystemException* exception);
	uint16_t SizeOfClass(ClassDeclaration* cls);
    IlCode* ResolveToken(IlCode* code, int32_t token);
	uint32_t DecodeUint32(byte* argv);
	void SendUint32(uint32_t value);
	uint16_t DecodePackedUint14(byte* argv);
    void SendExecutionResult(u16 codeReference, RuntimeException* lastState, Variable returnValue, MethodState execResult);
	IlCode* GetMethodByToken(IlCode* code, int32_t token);
	IlCode* GetMethodByCodeReference(u16 codeReference);
	void AttachToMethodList(IlCode* newCode);
	void SendPackedInt32(int32_t value);
	void SendPackedInt64(int64_t value);
	IlCode* _firstMethod;

	// Note: To prevent heap fragmentation, only one method can be running at a time. This will be non-null while running
	// and everything will be disposed afterwards.
	ExecutionState* _methodCurrentlyExecuting;

	stdSimple::map<u32, ClassDeclaration> _classes;

	// The list of static variables (global)
	stdSimple::map<u32, Variable> _statics;

	// Constant data fields (such as array initializers or strings)
	stdSimple::map<u32, byte*> _constants;
};


#endif 
