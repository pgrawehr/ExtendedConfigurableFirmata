
#include <ConfigurableFirmata.h>
#include "HardwareAccess.h"
#include "SelfTest.h"
#include "I2CFirmata.h"
#include <Wire.h>

// Reference the instance of the firmata i2c driver
extern I2CFirmata i2c;

// this pin is used as input for the random number generator
const int OPEN_ANALOG_PIN = A0;

int64_t HardwareAccess::_tickCountFrequency = 1000000;
int64_t HardwareAccess::_tickCount64 = 0; // Only upper 32 bits used
uint32_t HardwareAccess::_lastTickCount = 0;

void HardwareAccess::UpdateClocks()
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
			Firmata.setPinMode(pin, MODE_INPUT);
			Firmata.setPinState(pin, 0);
		}
		if (args[2].Int32 == 1) // Must match PullMode enum on C# side
		{
			Firmata.setPinMode(pin, OUTPUT);
		}
		if (args[2].Int32 == 3)
		{
			Firmata.setPinMode(pin, MODE_INPUT);
			Firmata.setPinState(pin, 1);
		}

		break;
	}
	case NativeMethod::HardwareLevelAccessWritePin: // Write(int pin, int value)
			// Firmata.sendStringf(F("Write pin %ld value %ld"), 8, args->Get(1), args->Get(2));
		digitalWrite(args[1].Int32, args[2].Int32 != 0);
		break;
	case NativeMethod::HardwareLevelAccessReadPin:
		result = { (int32_t)digitalRead(args[1].Int32), VariableKind::Int32 };
		break;
	case NativeMethod::EnvironmentProcessorCount:
		result.Type = VariableKind::Int32;
		result.Int32 = 1;
		break;
	case NativeMethod::EnvironmentTickCount: // TickCount
	{
		int mil = millis();
		// this one returns signed, because it replaces a standard library function
		result = { (int32_t)mil, VariableKind::Int32 };
		break;
	}
	case NativeMethod::ArduinoNativeHelpersSleepMicroseconds:
		delayMicroseconds(args[0].Uint32);
		break;
	case NativeMethod::ArduinoNativeHelpersGetMicroseconds:
		result = { (uint32_t)micros(), VariableKind::Uint32 };
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
		if (mode == MODE_INPUT)
		{
			result.Int32 = 0;
			if (Firmata.getPinState((byte)args[1].Int32) == 1)
			{
				result.Int32 = 3; // INPUT_PULLUP instead of input
			}
		}
		else if (mode == OUTPUT)
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
	default:
		return false;
	}
	return true;
}

