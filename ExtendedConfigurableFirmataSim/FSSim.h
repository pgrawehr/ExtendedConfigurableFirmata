#pragma once
#include <ConfigurableFirmata.h>

#define FILE_READ       "r"
#define FILE_WRITE      "w"
#define FILE_APPEND     "a"

namespace fs
{
	enum SeekMode {
		SeekSet = 0,
		SeekCur = 1,
		SeekEnd = 2
	};

	class File : Stream
	{
	public:
		File(FILE* p = nullptr) : _p(p)
		{
		}

		size_t write(uint8_t) override;
		size_t write(const uint8_t* buf, size_t size) override;
		int available() override;
		int read() override;
		int peek() override;
		void flush() override;
		size_t read(uint8_t* buf, size_t size);
		size_t readBytes(char* buffer, size_t length)
		{
			return read((uint8_t*)buffer, length);
		}

		bool seek(uint32_t pos, SeekMode mode);
		bool seek(uint32_t pos)
		{
			return seek(pos, SeekSet);
		}
		size_t position() const;
		size_t size() const;
		void close();
		operator bool() const;
		time_t getLastWrite();
		const char* path() const;
		const char* name() const;

		boolean isDirectory(void);
		File openNextFile(const char* mode = FILE_READ);
		void rewindDirectory(void);

	protected:
		FILE* _p;
	};

	class FS
	{
	public:
		File open(const char* path, const char* mode = FILE_READ, const bool create = false);

		bool exists(const char* path);

		bool remove(const char* path);

		bool rename(const char* pathFrom, const char* pathTo);

		bool mkdir(const char* path);

		bool rmdir(const char* path);
	};

	class F_Fat : public FS
	{
		friend class FS;
	public:
		F_Fat()
		{
			_rootPath[0] = 0;
		}
		bool begin(bool formatOnFail = false, const char* basePath = "/ffat", uint8_t maxOpenFiles = 10, const char* partitionLabel = (char*)"DATA");
		bool format(bool full_wipe = false, char* partitionLabel = (char*)"DATA");
		size_t totalBytes();
		size_t usedBytes();
		size_t freeBytes();
		void end();
		const char* rootPath()
		{
			return _rootPath;
		}
	private:

		char _rootPath[256]{}; // Simulated root path
	};

}

// With these definitions, the namespace is a bit pointless, but it's equivalent to the original FS declarations in FS.h and FFat.h
using fs::FS;
using fs::File;
extern fs::F_Fat FFat;
