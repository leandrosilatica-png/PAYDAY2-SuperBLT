//
// Created by HW12Dev on 30/07/2026
//

#include "convert.h"

#include "dbutil/Datastore.h"
#include "util/util.h"

#include <diesel/modern/massunit.h>

#include <cstring>

struct MassunitHeader
{
	uint64_t types_size;
	uint64_t types_capacity;
	uint64_t types_data;
	uint64_t types_allocator;
};

bool CheckMassunitRequiresConversion(BLTAbstractDataStore* datastore)
{
	if (datastore->size() < sizeof(MassunitHeader))
		return true;

	MassunitHeader header;
	if (datastore->read(0, reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header))
		return true;

	return header.types_allocator != 0;
}

std::vector<uint8_t> ConvertMassunit(std::vector<uint8_t>&& data, const std::string& path)
{
	if (data.size() < sizeof(MassunitHeader))
		return data;

	MassunitHeader* header = (MassunitHeader*)data.data();

	if (header->types_allocator == 0)
	{
		return data; // is already 64bit
	}

	// Parse the contents in 32-bit format
	diesel::modern::MassUnitResource mu;
	Reader reader((char*)data.data(), data.size(), false);

	if (!mu.Read(reader, diesel::DieselFormatsLoadingParameters(diesel::EngineVersion::PAYDAY_2_LATEST,
	                                                            diesel::Renderer::UNSPECIFIED,
	                                                            diesel::FileSourcePlatform::WINDOWS_32)))
	{
		char msg[512];
		snprintf(msg, sizeof(msg), "32-bit massunit conversion failed for '%s'; the file is invalid or unsupported.",
		         path.c_str());
		RAIDHOOK_LOG_ERROR(msg);

		return data;
	}

	reader.Close();

	// Now write it back out to our data vector

	Writer writer;
	MemoryWriterContainer* container = (MemoryWriterContainer*)writer.GetContainer();

	mu.Write(writer,
	         diesel::DieselFormatsLoadingParameters(diesel::EngineVersion::DIESEL_V3, diesel::Renderer::UNSPECIFIED,
	                                                diesel::FileSourcePlatform::WINDOWS_64));

	writer.Close();

	const std::vector<char>& convertedData = container->GetData();
	std::vector<uint8_t> result(convertedData.size());
	if (!result.empty())
		memcpy(result.data(), convertedData.data(), result.size());
	return result;
}
