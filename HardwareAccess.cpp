// 
// 
// 
#include "ConfigurableFirmata.h"
#include "HardwareAccess.h"
#include "SelfTest.h"

// this pin is used as input for the random number generator
const int OPEN_ANALOG_PIN = A0;

/// <summary>
/// This method contains the low-level implementations for hardware dependent methods.
/// These might need replacement on different hardware
/// </summary>
/// <param name="currentFrame">Frame pointer</param>
/// <param name="method">Method to call</param>
/// <param name="args">Argument list</param>
/// <param name="result">Method return value</param>
/// <returns>True if the method was found and handled, false otherwise</returns>
bool HardwareAccess::ExecuteHardwareAccess(ExecutionState* currentFrame, NativeMethod method, const VariableVector& args, Variable& result)
{
	switch(method)
	{
	case NativeMethod::HardwareLevelAccessSetPinMode: // PinMode(int pin, PinMode mode)
	{
		byte pin = (byte)args[1].Int32;

		if (args[2].Int32 == 0)
		{
			// Input
			Firmata.setPinMode(pin, INPUT);
			Firmata.setPinState(pin, 0);
		}
		if (args[2].Int32 == 1) // Must match PullMode enum on C# side
		{
			Firmata.setPinMode(pin, OUTPUT);
		}
		if (args[2].Int32 == 3)
		{
			Firmata.setPinMode(pin, INPUT);
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
		if (mode == INPUT)
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
	default:
		return false;
	}
	return true;
}

