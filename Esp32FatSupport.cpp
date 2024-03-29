// 
// 
//

#include <ConfigurableFirmata.h>
#include "FirmataIlExecutor.h"
#include "Esp32FatSupport.h"
#include "Exceptions.h"
#include "StandardErrorCodes.h"
#include "ArduinoDueSupport.h"
#ifdef SIM
#include "ExtendedConfigurableFirmataSim/SimulatorImpl.h"
#endif

// Not on Arduino Due (has no built-in file system support)
// Implementation would be possible when using a library for a SD card
#ifndef ARDUINO_DUE

#ifdef ESP32
#include <FS.h>
#include <FFat.h>
#endif
#include <vector>
#if SIM
#include <FSSim.h>
#endif

std::vector<File> fileHandles;

#pragma pack (push, 1)
// Own copy of definition (already exists on windows, but not on other compilers)
typedef struct _FILE_STANDARD_INFO_1 {
	uint64_t AllocationSize;
	uint64_t EndOfFile;
	int32_t       NumberOfLinks;
	int32_t       DeletePending; // Is BOOLEAN really 32 bits?
	int32_t       Directory;
} FILE_STANDARD_INFO_1, * PFILE_STANDARD_INFO_1;
#pragma pack (pop)

Esp32FatSupport::Esp32FatSupport()
{
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
	
	Firmata.sendStringf(F("Total space on data partition: %10u\n"), FFat.totalBytes());
	Firmata.sendStringf(F("Free space on data partition: %10u\n"), FFat.freeBytes());
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
		result.Int32 = 0;
		char* path = FirmataIlExecutor::GetAsUtf8String(args[0]);
		bool exists = FFat.exists(path);
		const char* mode = 0;
		int dwCreationDisposition = args[4].Int32;

		executor->SetLastError(0);
		switch (dwCreationDisposition)
		{
		case 2: // CreateNew
			FFat.remove(path);
			mode = "w+b";
			break;
		case 1: // Create
			if (exists)
			{
				executor->SetLastError(ERROR_FILE_EXISTS); // file exists
				result.Object = nullptr;
				break;
			}
			mode = "w+b";
			break;
		case 3:  // Open:
			if (!exists)
			{
				executor->SetLastError(ERROR_FILE_NOT_FOUND); // file does not exist
				result.Object = nullptr;
				break;
			}
			mode = "r+b";
			break;
		case 4: // OpenOrCreate
			if (exists)
			{
				executor->SetLastError(ERROR_ALREADY_EXISTS); // Already exists
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
			// This would be a bug in the CLR
			throw ClrException("Unknown file mode", SystemException::InvalidOperation, currentFrame->_executingMethod->methodToken);
		}
		Firmata.sendStringf(F("Opening file %s in mode %s"), path, mode);
		File f = FFat.open(path, mode);
		if (!f)
		{
			result.Object = nullptr;
			executor->SetLastError(ERROR_FILE_NOT_FOUND); // File not found
		}
		else
		{
			// Try finding closed files instead of growing the array further.
			for (size_t pos = 0; pos < fileHandles.size(); pos++)
			{
				if (!fileHandles[pos])
				{
					fileHandles[pos] = f;
					result.Int32 = pos;
					break;
				}
			}

			if (result.Int32 == 0) // Still unassigned?
			{
				if (fileHandles.size() > 0xBFFE)
				{
					f.close();
					executor->SetLastError(ERROR_TOO_MANY_OPEN_FILES);
				}
				else
				{
					fileHandles.push_back(f);
					result.Int32 = fileHandles.size(); // Index + 1, because 0 is not a valid handle
				}
			}
		}
		Firmata.sendStringf(F("Opened file %s at handle %d."), path, result.Int32);
		break;
	}
	case NativeMethod::Interop_Kernel32WriteFile:
		{
		result.Type = VariableKind::Int32; // Number of bytes written
		const uint8_t* buffer = (uint8_t*)args[1].Object;
		int32_t len = args[2].Int32;
		int handle = args[0].Int32;
			if (handle == 0 || handle == -1)
			{
				executor->SetLastError(ERROR_INVALID_HANDLE);
				result.Int32 = -1;
				break;
			}
			if (handle == STANDARD_INPUT_HANDLE)
			{
				// Writing to standard input?
				executor->SetLastError(ERROR_WRITE_PROTECT);
				result.Int32 = -1;
				break;
			}
			if (handle == STANDARD_OUTPUT_HANDLE || handle == STANDARD_ERROR_HANDLE)
			{
				// Write to console
				if (len == 0)
				{
					// The runtime tries to write a letter "A" with zero length to the console to check whether it's writable
					result.Int32 = 0; // our C# wrapper converts this into success, as it is equal to the desired length
				}
				else
				{
					Firmata.sendString(STRING_DATA, (char*)buffer);
					result.Int32 = len;
				}
				break;
			}
		int index = handle - 1;
		
		Firmata.sendStringf(F("Writing %d bytes to handle %d"), len, args[0].Int32);
		if (index < 0 || index >= (int)fileHandles.size())
		{
			executor->SetLastError(ERROR_INVALID_HANDLE); // Invalid handle
			result.Int32 = -1;
			break;
		}
		executor->SetLastError(ERROR_SUCCESS);
		result.Int32 = fileHandles[index].write(buffer, len);
		break;
		}
	case NativeMethod::Interop_Kernel32WriteFileOverlapped2:
		{
		result.Type = VariableKind::Int32; // Number of bytes written
		const uint8_t* buffer = (uint8_t*)args[1].Object;
		int32_t bytesToWrite = args[2].Int32;
		int handle = args[0].Int32 - 1;
		int* bytesWritten = (int*)args[3].Object;
		int startOffset = args[4].Int32;
			
		if (handle == -1)
		{
			executor->SetLastError(ERROR_INVALID_HANDLE);
			result.Int32 = 0;
			break;
		}
		
		if (handle < 0 || handle >= (int)fileHandles.size())
		{
			executor->SetLastError(ERROR_INVALID_HANDLE); // Invalid handle
			result.Int32 = -0;
			break;
		}

		if (startOffset == -1)
		{
			fileHandles[handle].seek(0, fs::SeekMode::SeekEnd);
		}
		else
		{
			fileHandles[handle].seek(startOffset, fs::SeekMode::SeekSet);
		}
		*bytesWritten = fileHandles[handle].write(buffer, bytesToWrite);
		if (*bytesWritten != bytesToWrite)
		{
			executor->SetLastError(ERROR_DISK_FULL); // Or what else could be the reason?
			result.Int32 = 0;
			break;
		}
		executor->SetLastError(ERROR_SUCCESS);

		result.Type = VariableKind::Int32;
		result.Int32 = 1;
		break;
		}
	case NativeMethod::Interop_Kernel32ReadFile:
	{
		result.Type = VariableKind::Int32; // Number of bytes written
		int index = args[0].Int32 - 1;
		char* buffer = (char*)args[1].Object;
		int len = args[2].Int32;
		Firmata.sendStringf(F("Reading %d bytes from handle %d"), len, args[0].Int32);
		if (index < 0 || index >= (int)fileHandles.size())
		{
			executor->SetLastError(ERROR_INVALID_HANDLE); // Invalid handle
			result.Int32 = -1;
			break;
		}

		executor->SetLastError(0);
		result.Int32 = fileHandles[index].readBytes(buffer, len);
		Firmata.sendStringf(F("Successfuly read %d bytes"), result.Int32);
		break;
	}
	case NativeMethod::Interop_Kernel32CloseHandle:
	{
		result.Type = VariableKind::Boolean;
		int index = args[0].Int32 - 1;
		Firmata.sendStringf(F("Closing handle %d"), args[0].Int32);
		if (index < 0 || index >= (int)fileHandles.size())
		{
			executor->SetLastError(6); // Invalid handle
			result.Boolean = false;
			break;
		}

		executor->SetLastError(0);

		fileHandles[index].close();
		fileHandles.erase(fileHandles.begin() + index);
		result.Boolean = true;
		break;
	}
	case NativeMethod::Interop_Kernel32GetFileInformationByHandleEx:
		{
		result.Type = VariableKind::Int32;
		result.Int32 = 1;
		int index = args[0].Int32 - 1;
		if (index < 0 || index >= (int)fileHandles.size())
		{
			executor->SetLastError(ERROR_INVALID_HANDLE); // Invalid handle
			result.Int32 = 0;
			break;
		}
		int fileInformationClass = args[1].Int32;
		int length = args[3].Int32;
			if (fileInformationClass == 1) // FileStandardInfo
			{
				PFILE_STANDARD_INFO_1 structurePtr = (PFILE_STANDARD_INFO_1)args[2].Int32;
				if (length != sizeof(FILE_STANDARD_INFO_1))
				{
					executor->SetLastError(ERROR_INVALID_PARAMETER);
					result.Int32 = 0;
					break;
				}
				structurePtr->DeletePending = false;
				structurePtr->Directory = false;
				fs::File h = fileHandles[index];
				structurePtr->EndOfFile = h.size();
				structurePtr->AllocationSize = h.size();
			}
			else
			{
				executor->SetLastError(ERROR_INVALID_PARAMETER); // Unknown argument type
				result.Int32 = 0;
				break;
			}

		executor->SetLastError(0);
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
