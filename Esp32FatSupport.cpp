// 
// 
//

#include <ConfigurableFirmata.h>
#include "FirmataIlExecutor.h"
#include "Esp32FatSupport.h"
#include "Exceptions.h"

#ifdef ESP32
#include <FS.h>
#include <FFat.h>

std::vector<File> fileHandles;

Esp32FatSupport::Esp32FatSupport()
{
	_lastError = 0;
}

void Esp32FatSupport::Init()
{
	if (!FFat.begin())
	{
		Firmata.sendString(F("Unable to mount FFat partition. Attempting format"));
		FFat.format();
		if (!FFat.begin())
		{
			throw stdSimple::ExecutionEngineException("Unable to mount FAT partition. Aborting");
		}
	}

	// Make sure the temp directory exists
	FFat.mkdir("/tmp");
	
	Firmata.sendStringf(F("Total space on data partition: %10u\n"), 8, FFat.totalBytes());
	Firmata.sendStringf(F("Free space on data partition: %10u\n"), 8, FFat.freeBytes());
}

void Esp32FatSupport::SetLastError(int error)
{
	_lastError = error;
	if (_lastError != 0)
	{
		Firmata.sendStringf(F("SetLastError called with error code %d."), 4, error);
	}
}

bool Esp32FatSupport::ExecuteHardwareAccess(FirmataIlExecutor* executor, ExecutionState* currentFrame, NativeMethod method, const VariableVector& args, Variable& result)
{
	switch (method)
	{
	case NativeMethod::FileSystemCreateDirectory:
	{
		char* path = FirmataIlExecutor::GetAsUtf8String(args[0]);
		FFat.mkdir(path);
		free(path);
	}
		break;
	case NativeMethod::FileSystemFileExists:
	{
		result.Type = VariableKind::Boolean;
		char* path = FirmataIlExecutor::GetAsUtf8String(args[0]);
		result.Boolean = FFat.exists(path);
		free(path);
	}
		break;
	case NativeMethod::Interop_Kernel32CreateFile:
	{
			// Argument list according to https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea
		result.Type = VariableKind::NativeHandle;
		char* path = FirmataIlExecutor::GetAsUtf8String(args[0]);
		bool exists = FFat.exists(path);
		const char* mode = 0;
		int dwCreationDisposition = args[4].Int32;

		_lastError = 0;
		switch (dwCreationDisposition)
		{
		case 2: // CreateNew
			FFat.remove(path);
			mode = "w+b";
			break;
		case 1: // Create
			if (exists)
			{
				SetLastError(80); // file exists
				result.Object = nullptr;
				break;
			}
			mode = "w+b";
			break;
		case 3:  // Open:
			if (!exists)
			{
				SetLastError(2); // file does not exist
				result.Object = nullptr;
				break;
			}
			mode = "r+b";
			break;
		case 4: // OpenOrCreate
			if (exists)
			{
				SetLastError(183); // Already exists
				mode = "r+b";
			}
			else
			{
				mode = "w+b";
			}
			break;
		case 5: // Truncate
			mode = "w+b";
			break;
		default:
			throw ClrException("Unknown file mode", SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);
		}
		Firmata.sendStringf(F("Opening file %s in mode %s"), 8, path, mode);
		File f = FFat.open(path, mode);
		if (!f)
		{
			result.Object = nullptr;
			SetLastError(2); // File not found
		}
		else
		{
			fileHandles.push_back(f);
			result.Int32 = fileHandles.size(); // Index + 1, because 0 is not a valid handle
		}
		Firmata.sendStringf(F("Opened file %s at handle %d."), 8, path, result.Int32);
		break;
	}
	case NativeMethod::Interop_Kernel32GetLastError:
		result.Type = VariableKind::Uint32;
		result.Uint32 = _lastError;
		break;
	case NativeMethod::Interop_Kernel32SetLastError:
		_lastError = args[0].Int32;
		break;
	case NativeMethod::Interop_Kernel32WriteFile:
		{
		result.Type = VariableKind::Int32; // Number of bytes written
		int index = args[0].Int32 - 1;
		const uint8_t* buffer = (uint8_t*)args[1].Object;
		int32_t len = args[2].Int32;
		Firmata.sendStringf(F("Writing %d bytes to handle %d"), 8, len, args[0].Int32);
		if (index < 0 || index >= fileHandles.size())
		{
			SetLastError(6); // Invalid handle
			result.Int32 = -1;
			break;
		}
		_lastError = 0;
		result.Int32 = fileHandles[index].write(buffer, len);
		// TODO: Remove the next line
		fileHandles[index].close();
		break;
		}
	case NativeMethod::Interop_Kernel32ReadFile:
	{
		result.Type = VariableKind::Int32; // Number of bytes written
		int index = args[0].Int32 - 1;
		char* buffer = (char*)args[1].Object;
		int len = args[2].Int32;
		Firmata.sendStringf(F("Reading %d bytes from handle %d"), 8, len, args[0].Int32);
		if (index < 0 || index >= fileHandles.size())
		{
			SetLastError(6); // Invalid handle
			result.Int32 = -1;
			break;
		}
		_lastError = 0;
		result.Int32 = fileHandles[index].readBytes(buffer, len);
		Firmata.sendStringf(F("Successfuly read %d bytes"), 4, result.Int32);
		break;
	}
	case NativeMethod::Interop_Kernel32CloseHandle:
	{
		result.Type = VariableKind::Boolean;
		int index = args[0].Int32 - 1;
		Firmata.sendStringf(F("Closing handle %d"), 4, args[0].Int32);
		if (index < 0 || index >= fileHandles.size())
		{
			SetLastError(6); // Invalid handle
			result.Boolean = false;
			break;
		}
		fileHandles[index].close();
		result.Boolean = true;
		break;
	}
	default:
		return false;
	}
	return true;
}


void Esp32FatSupport::Update()
{
	// Nothing to do here
}


Esp32FatSupport::~Esp32FatSupport()
{
	// Nothing to do
}

#endif
