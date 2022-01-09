
#include <ConfigurableFirmata.h>
#include "HardwareAccess.h"
#include "SelfTest.h"
#include "I2CFirmata.h"
#include <Wire.h>

#include "RtcBase.h"
#include "Ds1307.h"
#include "Esp32Rtc.h"
#include "SimulatorClock.h"
#include "ArduinoDueSupport.h"
#include "PinUsage.h"

// Enable if a DS1307 hardware real-time-clock is attached via I2C
#if ESP32
Esp32Rtc TheClock;
#elif !SIM
Ds1307 TheClock;
#else
SimulatorClock TheClock;
#endif

// Reference the instance of the firmata i2c driver
extern I2CFirmata i2c;

// this pin is used as input for the random number generator
const int OPEN_ANALOG_PIN = A0;

const int IO_COMPLETIONPORT_DUMMY = 0x00DDDBAD;

int64_t HardwareAccess::_tickCountFrequency = 1000000;
int64_t HardwareAccess::_tickCount64 = 0; // Only upper 32 bits used
uint32_t HardwareAccess::_lastTickCount = 0;



void HardwareAccess::Init()
{
	// A place to put hardware-related initialization code.
	TheClock.Init();
}

void HardwareAccess::Update()
{
	uint32_t microSeconds = micros();
	if (microSeconds < _lastTickCount)
	{
		_tickCount64 += 0x100000000; // increase bit 32
	}
	_lastTickCount = microSeconds;
}

int64_t HardwareAccess::TickCount64()
{
	int64_t value = _tickCount64;
	value += (uint32_t)micros();
	return value;
}

void HardwareAccess::Reboot()
{
#if ARDUINO_DUE
	// This function is only available when compiling against the real Due SDK
	rstc_start_software_reset(RSTC);
#endif
}


/// <summary>
/// This method contains the low-level implementations for hardware dependent methods.
/// These might need replacement on different hardware
/// </summary>
/// <param name="executor">Pointer to main execution engine</param>
/// <param name="currentFrame">Frame pointer</param>
/// <param name="method">Method to call</param>
/// <param name="args">Argument list</param>
/// <param name="result">Method return value</param>
/// <returns>True if the method was found and handled, false otherwise</returns>
bool HardwareAccess::ExecuteHardwareAccess(FirmataIlExecutor* executor, ExecutionState* currentFrame, NativeMethod method, const VariableVector& args, Variable& result)
{
	switch(method)
	{
	case NativeMethod::HardwareLevelAccessSetPinMode: // PinMode(int pin, PinMode mode)
	{
		byte pin = (byte)args[1].Int32;

		if (args[2].Int32 == 0)
		{
			// Input
			Firmata.setPinMode(pin, PIN_MODE_INPUT);
			Firmata.setPinState(pin, 0);
		}
		else if (args[2].Int32 == 1) // Must match PullMode enum on C# side
		{
			Firmata.setPinMode(pin, PIN_MODE_OUTPUT);
		}
		else if (args[2].Int32 == 3)
		{
			Firmata.setPinMode(pin, PIN_MODE_INPUT);
			Firmata.setPinState(pin, 1);
		}

		break;
	}
	case NativeMethod::HardwareLevelAccessWritePin: // Write(int pin, int value)
			// Firmata.sendStringf(F("Write pin %ld value %ld"), 8, args->Get(1), args->Get(2));
		digitalWrite(args[1].Int32, args[2].Int32 != 0);
		break;
	case NativeMethod::HardwareLevelAccessReadPin:
		result.Int32 = digitalRead(args[1].Int32);
		result.Type = VariableKind::Int32;
		break;
	case NativeMethod::EnvironmentProcessorCount:
		result.Type = VariableKind::Int32;
		result.Int32 = 1;
		break;
	case NativeMethod::EnvironmentTickCount: // TickCount
	{
		int mil = millis();
		// this one returns signed, because it replaces a standard library function
		result.Int32 = mil;
		result.Type = VariableKind::Int32;
		break;
	}
	case NativeMethod::ArduinoNativeHelpersSleepMicroseconds:
		delayMicroseconds(args[0].Uint32);
		break;
	case NativeMethod::ArduinoNativeHelpersGetMicroseconds:
		result.Uint32 = micros();
		result.Type = VariableKind::Uint32;
		break;
	case NativeMethod::HardwareLevelAccessGetPinCount:
		ASSERT(args.size() == 1); // unused this pointer
		result.Int32 = TOTAL_PINS;
		result.Type = VariableKind::Int32;
		break;
	case NativeMethod::HardwareLevelAccessIsPinModeSupported:
		ASSERT(args.size() == 3);
		// TODO: Ask firmata (but for simplicity, we can assume the Digital I/O module is always present)
		// We support Output, Input and PullUp
		result.Boolean = (args[2].Int32 == 0 || args[2].Int32 == 1 || args[2].Int32 == 3) && IS_PIN_DIGITAL(args[1].Int32);
		result.Type = VariableKind::Boolean;
		break;
	case NativeMethod::HardwareLevelAccessGetPinMode:
	{
		ASSERT(args.size() == 2);
		byte mode = Firmata.getPinMode((byte)args[1].Int32);
		if (mode == PIN_MODE_INPUT)
		{
			result.Int32 = 0;
			if (Firmata.getPinState((byte)args[1].Int32) == 1)
			{
				result.Int32 = 3; // INPUT_PULLUP instead of input
			}
		}
		else if (mode == PIN_MODE_OUTPUT)
		{
			result.Int32 = 1;
		}
		else
		{
			// This is invalid for this method. GpioDriver.GetPinMode is only valid if the pin is in one of the GPIO modes
			throw ClrException(SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);
		}
		result.Type = VariableKind::Int32;
	}
		break;
	case NativeMethod::InteropGetRandomBytes:
	{
		ASSERT(args.size() == 2);
		byte* ptr = (byte*)args[0].Object; // This is an unmanaged pointer
		int size = args[1].Int32;
		for (int i = 0; i < size; i++)
		{
			byte b = (byte)analogRead(OPEN_ANALOG_PIN);
			*AddBytes(ptr, i) = b;
		}
		result.Type = VariableKind::Void;
	}
		break;

	case NativeMethod::ArduinoNativeI2cDeviceInit:
		ASSERT(args.size() == 3); // 0: this, 1: bus id, 2: device address
	{
		byte zero[2] = { 0, 0 }; // Delay (0)
		i2c.handleSysex(I2C_CONFIG, 2, zero); // Configure and enable the bus
	}
	break;
	case NativeMethod::ArduinoNativeBoardGetDefaultPinAssignmentForI2cInternal:
		{
			// This shall return an int with bits 0-7 SCL and bits 8-15 SDA (or vice-versa, doesn't really matter)
		result.Type = VariableKind::Int32;
		int pin1 = -1;
		int pin2 = -1;
			for (int i = 0; i < TOTAL_PINS; i++)
			{
				if (IS_PIN_I2C(i))
				{
					if (pin1 == -1)
					{
						pin1 = i;
					}
					else
					{
						pin2 = i;
						break;
					}
				}
			}

			result.Int32 = pin1 << 8 | pin2;
		}
		break;
	case NativeMethod::ArduinoNativeBoardActivatePinModeInternal:
		{
		ASSERT(args.size() == 3);
		byte pin = (byte)args[1].Int32;
		PinUsage usage = (PinUsage)args[2].Int32;
			switch(usage)
			{
			case PinUsage::AnalogIn:
				{
					Firmata.setPinMode(pin, PIN_MODE_ANALOG);
					break;
				}
			case PinUsage::Gpio:
				{
					Firmata.setPinMode(pin, PIN_MODE_INPUT);
					break;
				}
			case PinUsage::I2c:
				{
				Firmata.setPinMode(pin, PIN_MODE_I2C);
				break;
				}
			case PinUsage::Pwm:
				{
				Firmata.setPinMode(pin, PIN_MODE_PWM);
				break;
				}
			default:
				Firmata.sendString(F("Unknown pin mode requested"));
				break;
			}
		}
		break;
	case NativeMethod::ArduinoNativeI2cDeviceWriteByte:
		{
		ASSERT(args.size() == 2);
		Variable& self = args.at(0);
		Variable& data = args.at(1);
		ClassDeclaration* cls = FirmataIlExecutor::GetClassDeclaration(self);
		Variable address = executor->GetField(cls, self, 1);
		// Since the implementation of I2CFirmata::handleI2CRequest is stateless, we can as well directly call the wire library. It's easier here.
		// Note that this works only after an init
		Wire.beginTransmission((byte)address.Int32);
		Wire.write((byte)data.Int32);
		Wire.endTransmission();
		}
		break;
	case NativeMethod::ArduinoNativeI2cDeviceReadByte:
		{
			ASSERT(args.size() == 1);
			Variable& self = args.at(0);
			ClassDeclaration* cls = FirmataIlExecutor::GetClassDeclaration(self);
			Variable address = executor->GetField(cls, self, 1);
			// Since the implementation of I2CFirmata::handleI2CRequest is stateless, we can as well directly call the wire library. It's easier here.
			// Note that this works only after an init
			Wire.requestFrom((byte)address.Int32, (byte)1);
			if (Wire.available() != 1)
			{
				throw ClrException(SystemException::Io, currentFrame->_executingMethod->methodToken);
			}
			result.Type = VariableKind::Int32;
			result.Int32 = Wire.read();
		}
		break;
	case NativeMethod::ArduinoNativeI2cDeviceReadSpan:
		{
			ASSERT(args.size() == 2);
			Variable& self = args.at(0);
			ClassDeclaration* cls = FirmataIlExecutor::GetClassDeclaration(self);
			Variable address = executor->GetField(cls, self, 1);
			Variable& span = args.at(1);
			
			// Span is a value type that contains a pointer and a length field (both as 32 bit fields). Let's hope the order matches
			int targetLength = *AddBytes(&span.Int32, 4);
			byte* tgt = (byte*)*AddBytes(&span.Int32, 0);
			Wire.requestFrom((byte)address.Int32, (byte)targetLength);
			if (Wire.available() != targetLength)
			{
				// Drop anything left
				while (Wire.available())
				{
					Wire.read();
				}
				throw ClrException(SystemException::Io, currentFrame->_executingMethod->methodToken);
			}

			while (targetLength > 0)
			{
				*tgt = (byte)Wire.read();

				tgt++;
				targetLength--;
			}
			
			result.Type = VariableKind::Void;
		}
		break;
	case NativeMethod::ArduinoNativeI2cDeviceWriteSpan:
	{
		ASSERT(args.size() == 2);
		Variable& self = args.at(0);
		ClassDeclaration* cls = FirmataIlExecutor::GetClassDeclaration(self);
		Variable address = executor->GetField(cls, self, 1);
		Variable& span = args.at(1);

		// Span is a value type that contains a pointer and a length field (both as 32 bit fields). Let's hope the order matches
		int srcLength = *AddBytes(&span.Int32, 4);
		byte* src = (byte*)*AddBytes(&span.Int32, 0);

		Wire.beginTransmission((byte)address.Int32);
		while (srcLength > 0)
		{
			Wire.write(*src);
			src++;
			srcLength--;
		}
		Wire.endTransmission();

		result.Type = VariableKind::Void;
	}
	break;
	case NativeMethod::InteropQueryPerformanceFrequency:
		{
		ASSERT(args.size() == 1);
		Variable& ptr = args.at(0); // long* lpFrequency
		int64_t* lpPtr = (int64_t*)ptr.Object;
		*lpPtr = _tickCountFrequency;
		result.Type = VariableKind::Boolean;
		result.setSize(4);
		result.Boolean = true;
		}
		break;
		// These two are quite equivalent, I think.
	case NativeMethod::Interop_Kernel32QueryUnbiasedInterruptTime:
	case NativeMethod::InteropQueryPerformanceCounter:
		{
		ASSERT(args.size() == 1);
		Variable& ptr = args.at(0); // long* lpFrequency
		int64_t* lpPtr = (int64_t*)ptr.Object;
		*lpPtr = TickCount64();
		result.Type = VariableKind::Boolean;
		result.setSize(4);
		result.Boolean = true;
		}
		break;
	case NativeMethod::Interop_Kernel32AllocHGlobal:
	{
		ASSERT(args.size() == 1);
		Variable& size = args.at(0);
		result.Type = VariableKind::AddressOfVariable;
		void* memory = mallocEx(size.Int32);
		memset(memory, 0, size.Int32);
		result.Object = memory;
	}
		break;
	case NativeMethod::Interop_Kernel32FreeHGlobal:
	{
		ASSERT(args.size() == 1);
		Variable& ptr = args.at(0);
		ASSERT(ptr.Type == VariableKind::AddressOfVariable);
		freeEx(ptr.Object);
		ptr.Object = nullptr;
	}
		break;
	case NativeMethod::Interop_Kernel32InitializeCriticalSection:
		ASSERT(args.size() == 1);
		{
			Variable& ptr = args.at(0);
			ASSERT(ptr.Type == VariableKind::AddressOfVariable);
			if (ptr.Object == nullptr)
			{
				throw ClrException("InitializeCriticalSection on a null reference.", SystemException::InvalidOperation, 0);
			}
			memset(ptr.Object, 0, 4); // Let's assume this minimum size - SIZEOF(CRITICAL_SECTION) should be 6x4.
		}
		break;
	case NativeMethod::Interop_Kernel32InitializeConditionVariable:
		ASSERT(args.size() == 1);
		{
			Variable& ptr = args.at(0);
			ASSERT(ptr.Type == VariableKind::AddressOfVariable);
			if (ptr.Object == nullptr)
			{
				throw ClrException("InitializeCriticalSection on a null reference.", SystemException::InvalidOperation, 0);
			}
			memset(ptr.Object, 0, 4); // Let's assume this minimum size - SIZEOF(CONDITION_VARIABLE) should be 4
		}
		break;
	case NativeMethod::Interop_Kernel32CreateEventEx:
		{
			ASSERT(args.size() == 3);
			result.Type = VariableKind::AddressOfVariable;
			result.Object = xSemaphoreCreateBinary();
			result.setSize(4);
			executor->SetLastError(0);
		}
		break;
	case NativeMethod::Interop_Kernel32GetLastError:
		result.Type = VariableKind::Uint32;
		result.Uint32 = executor->GetLastError();
		break;
	case NativeMethod::Interop_Kernel32SetLastError:
		executor->SetLastError(args[0].Int32);
		break;
	case NativeMethod::Interop_Kernel32CreateIoCompletionPort:
		result.Type = VariableKind::AddressOfVariable;
		result.Int32 = IO_COMPLETIONPORT_DUMMY; // For now, just return a dummy handle (we won't be using this just yet)
		result.setSize(4);
		break;
	case NativeMethod::Interop_Kernel32GetFileType:
		ASSERT(args.size() == 1);
	{
			result.Type = VariableKind::Int32;
			int handle = args[0].Int32;
			if (handle == STANDARD_OUTPUT_HANDLE || handle == STANDARD_ERROR_HANDLE || handle == STANDARD_INPUT_HANDLE) // Compare MiniKernel.Console.cs
			{
				result.Int32 = 2; // FILE_TYPE_CHAR
			}
			else if (handle < 0xCEEE)
			{
				result.Int32 = 1; // FILE_TYPE_DISK
			}
			else
			{
				result.Int32 = 0; // FILE_TYPE_UNKNOWN
			}
	}		
		break;
	case NativeMethod::InterlockedCompareExchange_Object:
		ASSERT(args.size() == 3);
	{
			result.Type = VariableKind::Object;
			result.setSize(4);
			Variable& ref = args[0]; // Arg0 is a reference to an object
			Variable& value = args[1];
			Variable& comparand = args[2];
			noInterrupts();
			void** refPtr = (void**)ref.Object;
			void* orig = *(refPtr);
			if (orig == comparand.Object)
			{
				// The other properties at refTgt should already be equal, since this method only operates if all 3 arguments are objects.
				*(refPtr) = value.Object; // Replace the object ref points to with the value if ref==comparand.
			}
			interrupts();
			result.Object = orig; // Return the original destination object
	}
		break;
	case NativeMethod::InterlockedExchangeAdd:
		{
			// Behavior is a bit confusing: Adds the two input values (first one given by-ref), updates the first one with the sum and returns the old value
		result.Type = VariableKind::Int32;
		noInterrupts();
		int firstValue = *AddBytes((int*)args[0].Object, 0);
		int sum = firstValue + args[1].Int32;
		*AddBytes((int*)args[0].Object, 0) = sum;
		interrupts();
		result.Int32 = firstValue;
		}
		break;
	case NativeMethod::InterlockedExchangeInt:
		{
		result.Type = VariableKind::Int32;
		noInterrupts();
		int firstValue = *AddBytes((int*)args[0].Object, 0);
		int newValue = args[1].Int32;
		*AddBytes((int*)args[0].Object, 0) = newValue;
		interrupts();
		result.Int32 = firstValue;
		}
		break;
	case NativeMethod::InterlockedCompareExchange_Int32:
	{
		result.Type = VariableKind::Int32;
		Variable& ref = args[0]; // Arg0 is a reference to an int
		Variable& value = args[1];
		Variable& comparand = args[2];
		noInterrupts();
		int* refPtr = (int*)ref.Object;
		int orig = *(refPtr);
		if (orig == comparand.Int32)
		{
			*(refPtr) = value.Int32; // Replace the object ref points to with the value if ref==comparand.
		}
		interrupts();
		result.Int32 = orig; // Return the original destination value
	}
		break;
	case NativeMethod::DateTimeUtcNow:
		result.Type = VariableKind::Int64;
		result.setSize(8);
		result.Int64 = TheClock.ReadTime();
		break;

		// As long as we're running only one task, this is a no-op
	case NativeMethod::MonitorEnter:
		result.Type = VariableKind::Void;
		break;
	case NativeMethod::MonitorExit:
		result.Type = VariableKind::Void;
		break;
	
	default:
		return false;
	}
	return true;
}
