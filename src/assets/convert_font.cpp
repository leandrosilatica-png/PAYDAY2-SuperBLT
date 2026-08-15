//
// Created by HW12Dev on 27/07/2026
//

#include "convert.h"

#include "dbutil/Datastore.h"
#include "util/util.h"

#include <diesel/font.h>

#include <cstring>

struct FontHeader
{
	size_t glyphs_size;
	size_t glyphs_capacity;
	size_t glyphs_data;
	size_t glyphs_allocator;
	// there's more, but above is all we need to determine bitness
};

bool CheckFontRequiresConversion(BLTAbstractDataStore* datastore)
{
	if (datastore->size() < sizeof(FontHeader))
		return true;

	FontHeader header;
	if (datastore->read(0, reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header))
		return true;

	return header.glyphs_allocator != 0 || header.glyphs_data == 0;
}

std::vector<uint8_t> ConvertFont(std::vector<uint8_t>&& data, const std::string& path)
{
	if (data.size() < sizeof(FontHeader))
		return data;

	FontHeader* header = (FontHeader*)data.data();

	if (header->glyphs_allocator == 0 && header->glyphs_data != 0)
	{
		return data;
	}

	diesel::AngelCodeFont font;
	Reader reader((char*)data.data(), data.size(), false);

	if (!font.Read(reader, diesel::DieselFormatsLoadingParameters(diesel::EngineVersion::PAYDAY_2_LATEST,
	                                                              diesel::Renderer::UNSPECIFIED,
	                                                              diesel::FileSourcePlatform::WINDOWS_32)))
	{
		char msg[512];
		snprintf(msg, sizeof(msg), "32-bit font conversion failed for '%s'; the file is invalid or unsupported.",
		         path.c_str());
		RAIDHOOK_LOG_ERROR(msg);

		return data;
	}

	reader.Close();

	// Now write it back out to our data vector

	Writer writer;
	MemoryWriterContainer* container = (MemoryWriterContainer*)writer.GetContainer();

	font.Write(writer,
	           diesel::DieselFormatsLoadingParameters(diesel::EngineVersion::DIESEL_V3, diesel::Renderer::UNSPECIFIED,
	                                                  diesel::FileSourcePlatform::WINDOWS_64));

	writer.Close();

	const std::vector<char>& convertedData = container->GetData();
	std::vector<uint8_t> result(convertedData.size());
	if (!result.empty())
		memcpy(result.data(), convertedData.data(), result.size());
	return result;
}
