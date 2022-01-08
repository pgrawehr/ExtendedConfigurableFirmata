#include <ConfigurableFirmata.h>
#include "FSSim.h"
#undef INPUT
#include <Windows.h>

fs::F_Fat FFat;


bool fs::F_Fat::begin(bool formatOnFail, const char* basePath, uint8_t maxOpenFiles, const char* partitionLabel)
{
	size_t length = sizeof(_rootPath) / sizeof(char);
	if (GetCurrentDirectoryA(length, _rootPath) > length / 2)
	{
		return false;
	}

	strcat_s(_rootPath, length, "/image/");

	CreateDirectoryA(_rootPath, nullptr);

	return true;
}

bool fs::F_Fat::format(bool full_wipe, char* partitionLabel)
{
	// Simulation doesn't support formatting (it doesn't have to, because begin() should never fail)
	return false;
}

void fs::F_Fat::end()
{
	// Nothing to do (will also not usually be called)
}

size_t fs::F_Fat::freeBytes()
{
	// For our simulation, this disk is never full
	return UINT32_MAX;
}

size_t fs::F_Fat::totalBytes()
{
	return UINT32_MAX;
}

size_t fs::F_Fat::usedBytes()
{
	return 0;
}



bool FS::exists(const char* path)
{
	HANDLE file = CreateFileA(path, 0, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	CloseHandle(file);
	return true;
}

bool FS::mkdir(const char* path)
{
	char fullPath[MAX_PATH];
	strcpy_s(fullPath, MAX_PATH, FFat.rootPath());
	strcat_s(fullPath, MAX_PATH, path);

	return CreateDirectoryA(fullPath, nullptr);
}

bool FS::remove(const char* path)
{
	char fullPath[MAX_PATH];
	strcpy_s(fullPath, MAX_PATH, FFat.rootPath());
	strcat_s(fullPath, MAX_PATH, path);

	return DeleteFileA(fullPath);
}


File FS::open(const char* path, const char* mode, const bool create)
{
	char fullPath[MAX_PATH];
	strcpy_s(fullPath, MAX_PATH, FFat.rootPath());
	strcat_s(fullPath, MAX_PATH, path);
	FILE* f = nullptr;
	errno_t error = fopen_s(&f, fullPath, mode);
	if (error != 0)
	{
		return File(f);
	}

	return File();
}

size_t File::write(const uint8_t* buf, size_t size)
{
	return fwrite(buf, 1, size, _p);
}

size_t File::write(uint8_t b)
{
	return fwrite(&b, 1, 1, _p);
}

int File::read()
{
	uint8_t b = 0;
	if (fread(&b, 1, 1, _p))
	{
		return b;
	}
	return -1;
}

size_t File::read(uint8_t* buf, size_t size)
{
	return fread(buf, 1, size, _p);
}


int File::peek()
{
	uint8_t b = 0;
	if (fread(&b, 1, 1, _p))
	{
		fseek(_p, -1, SEEK_CUR);
		return b;
	}
	return -1;
}

int File::available()
{
	uint8_t b = 0;
	if (fread(&b, 1, 1, _p))
	{
		fseek(_p, -1, SEEK_CUR);
		return 1;
	}
	return 0;
}

void File::flush()
{
	fflush(_p);
}

File::operator bool() const
{
	return _p != nullptr;
}

void File::close()
{
	fclose(_p);
	_p = nullptr;
}




