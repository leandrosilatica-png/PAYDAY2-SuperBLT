#include "Datastore.h"
#include "util/util.h"

#include <assert.h>
#include <limits>
#include <stdlib.h>
#include <string.h>

#include <Windows.h>

// BLTAbstractDataStore

size_t BLTAbstractDataStore::write(uint64_t position_in_file, uint8_t const* data, size_t length)
{
	// Writing is unsupported
	RAIDHOOK_LOG_ERROR("BLTAbstractDataStore::write called - writing is not supported!");
	abort();
}

void BLTAbstractDataStore::set_asynchronous_completion_callback(void* /*dsl::LuaRef*/)
{
	RAIDHOOK_LOG_ERROR("BLTAbstractDataStore::set_asynchronous_completion_callback called - async unimplemented!");
	abort();
}

uint64_t BLTAbstractDataStore::state()
{
	RAIDHOOK_LOG_ERROR("BLTAbstractDataStore::state called - unimplemented!");
	abort();
}

// BLTFileDataStore

BLTFileDataStore* BLTFileDataStore::Open(std::string filePath)
{
	HANDLE fileHandle = CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
	                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);

	// Make sure the file opened correctly
	if (fileHandle == INVALID_HANDLE_VALUE)
	{
		return nullptr;
	}

	LARGE_INTEGER fileSize;
	if (!GetFileSizeEx(fileHandle, &fileSize) || fileSize.QuadPart < 0 ||
	    static_cast<unsigned long long>(fileSize.QuadPart) > std::numeric_limits<size_t>::max())
	{
		CloseHandle(fileHandle);
		return nullptr;
	}

	auto obj = new BLTFileDataStore();
	obj->file_handle = fileHandle;
	obj->file_size = static_cast<size_t>(fileSize.QuadPart);

	return obj;
}

BLTFileDataStore::~BLTFileDataStore()
{
	CloseHandle(static_cast<HANDLE>(file_handle));
}

size_t BLTFileDataStore::read(uint64_t position_in_file, uint8_t* data, size_t length)
{
	if (length == 0)
		return 0;

	HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	assert(event != nullptr);
	if (event == nullptr)
		return 0;

	size_t count = 0;
	while (count < length)
	{
		if (count > std::numeric_limits<uint64_t>::max() - position_in_file)
			break;

		const uint64_t offset = position_in_file + count;
		const size_t remaining = length - count;
		const DWORD chunk = remaining > std::numeric_limits<DWORD>::max() ? std::numeric_limits<DWORD>::max()
		                                                                  : static_cast<DWORD>(remaining);

		OVERLAPPED request{};
		request.Offset = static_cast<DWORD>(offset);
		request.OffsetHigh = static_cast<DWORD>(offset >> 32);
		request.hEvent = event;

		if (!ResetEvent(event))
			break;

		if (!ReadFile(static_cast<HANDLE>(file_handle), data + count, chunk, nullptr, &request) &&
		    GetLastError() != ERROR_IO_PENDING)
		{
			break;
		}

		DWORD bytesRead = 0;
		if (!GetOverlappedResult(static_cast<HANDLE>(file_handle), &request, &bytesRead, TRUE))
			break;

		count += bytesRead;
		if (bytesRead < chunk)
			break;
	}

	CloseHandle(event);
	assert(count == length);

	return count;
}

bool BLTFileDataStore::close()
{
	RAIDHOOK_LOG_ERROR("BLTAbstractDataStore::close called - unimplemented!");
	abort();
}

size_t BLTFileDataStore::size() const
{
	return file_size;
}

bool BLTFileDataStore::is_asynchronous() const
{
	// TODO this would probably be good to implement if possible
	return false;
}

bool BLTFileDataStore::good() const
{
	RAIDHOOK_LOG_ERROR("BLTAbstractDataStore::good called - unimplemented!");
	abort();
}

// BLTStringDataStore

BLTStringDataStore::BLTStringDataStore(std::vector<uint8_t> contents) : contents(std::move(contents))
{
}

size_t BLTStringDataStore::read(uint64_t position_in_file, uint8_t* data, size_t length)
{
	// If the start of the read is past the end, stop here
	if (position_in_file >= contents.size())
		return 0;

	// If the end of the read is past the end, shrink it down so it'll fit
	size_t remaining = contents.size() - position_in_file;
	if (remaining < length)
		length = remaining;

	memcpy(data, contents.data() + position_in_file, length);
	return length;
}

bool BLTStringDataStore::close()
{
	RAIDHOOK_LOG_ERROR("BLTStringDataStore::close called - unimplemented!");
	abort();
	// What are we supposed to return?
}

size_t BLTStringDataStore::size() const
{
	return contents.size();
}

bool BLTStringDataStore::is_asynchronous() const
{
	return false;
}

bool BLTStringDataStore::good() const
{
	return true;
}

void DeleteDatastore(BLTAbstractDataStore* datastore, int refcountId)
{
	// Do the same thing as an Archive would
	// Datastores use this big global reference count system. Objects have an ID, which you can then
	// use to increment and decrement their reference count.
	// If we're the last one to use this object - which we almost certainly are - then delete it.

	int datastoreRefCount = DecreaseRefCountById(refcountId);
	if (datastoreRefCount != 0)
		return;

	using DtorFn = void (*)(void* thisPtr, bool freeMemory);
	void* vtable = *(void***)datastore;
	DtorFn dtor = *(DtorFn*)vtable;
	dtor(datastore, true);
}
