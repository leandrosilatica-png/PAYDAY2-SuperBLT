//
// Created by Campbell on 14/07/2026.
//

#include "convert.h"
#include "dbutil/Archive.h"
#include "dbutil/Datastore.h"
#include "subhook.h"
#include "util/util.h"

#include <diesel/modern/scriptdata.h>

#include <cstring>

struct DslVector
{
	uint64_t allocator;
	char padding[24];
};

struct ScriptdataHeader
{
	DslVector numbers;
	DslVector strings;
	DslVector vector3s;
	DslVector quaternions;
	DslVector idstrings;
	DslVector tables;
};
static_assert(sizeof(ScriptdataHeader) == 192);

struct DslVector32
{
	uint32_t allocator;
	uint32_t padding[3];
};

struct ScriptdataHeader32
{
	DslVector32 numbers;
	DslVector32 strings;
	DslVector32 vector3s;
	DslVector32 quaternions;
	DslVector32 idstrings;
	DslVector32 tables;
};
static_assert(sizeof(ScriptdataHeader32) == 96);

static bool Is64BitScriptData(const ScriptdataHeader& header)
{
	return header.numbers.allocator == 0 && header.strings.allocator == 0 && header.vector3s.allocator == 0 &&
	       header.quaternions.allocator == 0 && header.idstrings.allocator == 0 && header.tables.allocator == 0;
}

static bool Is64BitScriptData(const void* data, size_t size)
{
	if (size < sizeof(ScriptdataHeader))
		return false;

	ScriptdataHeader header;
	memcpy(&header, data, sizeof(header));
	return Is64BitScriptData(header);
}

bool CheckScriptDataRequiresConversion(BLTAbstractDataStore* datastore)
{
	if (datastore->size() < sizeof(ScriptdataHeader))
		return true;

	ScriptdataHeader header;
	if (datastore->read(0, reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header))
		return true;

	return !Is64BitScriptData(header);
}

std::vector<uint8_t> ConvertScriptData(std::vector<uint8_t>&& data, const std::string& path)
{
	/*
	char msg[100];
	snprintf(msg, sizeof(msg), "Script data: %d bytes", (int)data.size());
	RAIDHOOK_LOG_LOG(msg);
	*/

	if (data.size() < sizeof(ScriptdataHeader32))
		return data;

	// Check if this is a 32-bit file.
	//
	// Due to the pointer size differences, it's very likely the allocator pointers (which are null
	// in the files, and IIRC overwritten with an allocator at load time) will overlap with one of the
	// pointer/size values in a 32-bit file.
	if (Is64BitScriptData(data.data(), data.size()))
	{
		return data;
	}

	// Parse the contents in 32-bit format
	diesel::modern::ScriptData sd;
	Reader reader((char*)data.data(), data.size(), false);

	if (!sd.Read(reader, diesel::DieselFormatsLoadingParameters(diesel::EngineVersion::PAYDAY_2_LATEST,
	                                                            diesel::Renderer::UNSPECIFIED,
	                                                            diesel::FileSourcePlatform::WINDOWS_32)))
	{
		char msg[512];
		snprintf(msg, sizeof(msg), "32-bit scriptdata conversion failed for '%s'; the file is invalid or unsupported.",
		         path.c_str());
		RAIDHOOK_LOG_ERROR(msg);

		return data;
	}

	reader.Close();

	// Now write it back out to our data vector

	Writer writer;
	MemoryWriterContainer* container = (MemoryWriterContainer*)writer.GetContainer();

	sd.Write(writer,
	         diesel::DieselFormatsLoadingParameters(diesel::EngineVersion::DIESEL_V3, diesel::Renderer::UNSPECIFIED,
	                                                diesel::FileSourcePlatform::WINDOWS_64));

	writer.Close();

	const std::vector<char>& convertedData = container->GetData();
	std::vector<uint8_t> result(convertedData.size());
	if (!result.empty())
		memcpy(result.data(), convertedData.data(), result.size());
	return result;
}

static subhook::Hook ScriptSerializer__from_binary_hook;

void* ScriptSerializer__from_binary_h(void* this_, void* lua_arg_result, const PDString& data, void* metatable_registry)
{
	std::vector<uint8_t> converted_data;
	PDString converted;
	const PDString* load_data = &data;

	if (!Is64BitScriptData(data.data(), data.size()))
	{
		converted_data.assign(data.begin(), data.end());
		converted_data = ConvertScriptData(std::move(converted_data), "");
		converted.set_data(reinterpret_cast<char*>(converted_data.data()), converted_data.size());
		load_data = &converted;
	}

	ScriptSerializer__from_binary_hook.Remove();
	void* ret = ScriptSerializer__from_binary(this_, lua_arg_result, *load_data, metatable_registry);
	ScriptSerializer__from_binary_hook.Install();
	return ret;
}

void setup_scriptserializer_hooks()
{
	ScriptSerializer__from_binary_hook.Install(ScriptSerializer__from_binary, (void*)&ScriptSerializer__from_binary_h,
	                                           subhook::HookFlag64BitOffset);
}
