//
// Created by HW12Dev on 14/07/2026
//

#include "convert.h"

// Please forgive me, writing a zlib compression routine from scratch is so painful
#include "dbutil/Datastore.h"
#include "fileio/zlibcompression.h"
#include "util/util.h"

#include <diesel/animation.h>

#include <cstring>

struct AnimationHeader // 32bit, 64bit has extra padding here on purpose
{
	uint32_t type_id;
	uint32_t version;
	uint32_t original_location;
	uint32_t file_size;
};

bool CheckAnimationRequiresConversion(BLTAbstractDataStore* datastore)
{
	if (datastore->size() < sizeof(AnimationHeader))
		return true;

	AnimationHeader header;
	if (datastore->read(0, reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header))
		return true;

	// Compressed files still need to go through the converter so the inner header can be checked.
	if ((header.type_id & 0xFF) == 0x78)
		return true;

	return static_cast<size_t>(header.file_size) == datastore->size();
}

std::vector<uint8_t> ConvertAnimation(std::vector<uint8_t>&& data, const std::string& path)
{
	if (data.size() < sizeof(AnimationHeader))
		return data;

	if (data[0] == 0x78)
	{
		// We are zlib compressed

		uint32_t uncompressed_size;
		memcpy(&uncompressed_size, data.data() + data.size() - sizeof(uncompressed_size), sizeof(uncompressed_size));

		std::vector<uint8_t> uncompressed(uncompressed_size);
		compression::ZlibDecompression::DecompressBuffer(reinterpret_cast<char*>(data.data()), data.size() - 4,
		                                                 reinterpret_cast<char*>(uncompressed.data()),
		                                                 uncompressed.size());
		data = std::move(uncompressed);
	}

	AnimationHeader* header = (AnimationHeader*)data.data();

	if ((size_t)header->file_size != data.size()) // size field doesn't align up to have the right data, must be 64bit
	{
		return data;
	}

	// Parse the contents in 32-bit format
	diesel::Animation animation;
	Reader reader((char*)data.data(), data.size(), false);

	if (!animation.ReadUncompressed(reader, diesel::DieselFormatsLoadingParameters(
												diesel::EngineVersion::PAYDAY_2_LATEST, diesel::Renderer::UNSPECIFIED,
												diesel::FileSourcePlatform::WINDOWS_32)))
	{
		char msg[512];
		snprintf(msg, sizeof(msg), "32-bit animation conversion failed for '%s'; the file is invalid or unsupported.",
		         path.c_str());
		RAIDHOOK_LOG_ERROR(msg);

		return data;
	}

	reader.Close();

	// Now write it back out to our data vector

	Writer writer;
	MemoryWriterContainer* container = (MemoryWriterContainer*)writer.GetContainer();

	animation.Write(writer, diesel::DieselFormatsLoadingParameters(diesel::EngineVersion::DIESEL_V3,
	                                                               diesel::Renderer::UNSPECIFIED,
	                                                               diesel::FileSourcePlatform::WINDOWS_64));

	writer.Close();

	const std::vector<char>& convertedData = container->GetData();
	std::vector<uint8_t> result(convertedData.size());
	if (!result.empty())
		memcpy(result.data(), convertedData.data(), result.size());
	return result;
}
