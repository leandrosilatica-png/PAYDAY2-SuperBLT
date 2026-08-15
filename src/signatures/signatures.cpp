// We specifically need the windows and psapi imports in this order
// clang-format off
#include <Windows.h>
#include <Psapi.h>
// clang-format on

#include "subhook.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <tlhelp32.h>
#include <unordered_map>
#include <utility>

#define SIG_INCLUDE_MAIN
#define INCLUDE_TRY_OPEN_FUNCTIONS
#include "sigdef.h"
#undef SIG_INCLUDE_MAIN

#include "signatures.h"
#include "util/util.h"

std::vector<void*> try_open_functions;

using std::string;
using std::to_string;

class SignatureCacheDB
{
  public:
	SignatureCacheDB(string filename) : filename(filename)
	{
		std::ifstream infile(filename, std::ios::binary);
		if (!infile.good())
		{
			RAIDHOOK_LOG_WARN("Could not open signature cache file");
			return;
		}

#define READ_BIN(var) infile.read((char*)&var, sizeof(var));

		uint32_t revision;
		READ_BIN(revision); // TODO if the file is EOF, exit
		if (revision != CACHEDB_REVISION)
		{
			// Using a different revision, can't safely use it.
			// Not a big deal, just search properly for signatures this time.
			RAIDHOOK_LOG_WARN("Discarding signature cache data, different revision");
			return;
		}

		uint32_t count;
		READ_BIN(count); // TODO if the file is EOF, exit

		for (size_t i = 0; i < count; i++)
		{
			uint32_t length;
			READ_BIN(length);
			if (length > BUFF_LEN)
			{
				RAIDHOOK_LOG_ERROR("Cannot read long signature name!");
				locations.clear();
				return;
			}

			char name[BUFF_LEN];
			infile.read(name, length);
			string name_str = string(name, length);

			size_t address;
			READ_BIN(address);

			locations[name_str] = address;
		}

#undef READ_BIN
	}

	size_t GetAddress(const string& name) const
	{
		auto location = locations.find(name);
		return location == locations.end() ? INVALID_OFFSET : location->second;
	}

	void UpdateAddress(string name, size_t address)
	{
		if (name.length() > BUFF_LEN)
		{
			string msg = "Cannot write long signature name!";
			RAIDHOOK_LOG_ERROR(msg);
			throw msg;
		}
		locations.insert_or_assign(std::move(name), address);
	}

	void Save()
	{
		std::ofstream outfile(filename, std::ios::binary);
		if (!outfile.good())
		{
			RAIDHOOK_LOG_ERROR("Could not open signature cachefile for saving");
			return;
		}

#define WRITE_BIN(var) outfile.write((char*)&var, sizeof(var))

		uint32_t revision = CACHEDB_REVISION;
		WRITE_BIN(revision);

		uint32_t count = locations.size();
		WRITE_BIN(count);

		RAIDHOOK_LOG_LOG(string("Saving ") + to_string(count) + string(" signatures"));

		for (auto const& sig : locations)
		{
			// name length
			uint32_t length = sig.first.length();
			WRITE_BIN(length);

			// name
			outfile.write(sig.first.c_str(), length);

			// address
			size_t address = sig.second;
			WRITE_BIN(address);
		}

		RAIDHOOK_LOG_LOG("Done saving signatures");

#undef READ_BIN
	}

  private:
	const string filename;
	std::unordered_map<string, size_t> locations;

	static const uint32_t CACHEDB_REVISION = 1;
	static const uint32_t BUFF_LEN = 1024;
	static constexpr size_t INVALID_OFFSET = std::numeric_limits<size_t>::max();
};

static constexpr size_t INVALID_OFFSET = std::numeric_limits<size_t>::max();

static MODULEINFO GetModuleInfo(const char* module)
{
	MODULEINFO modinfo = {0};
	HMODULE hModule = GetModuleHandle(module);
	if (hModule == 0)
		return modinfo;
	GetModuleInformation(GetCurrentProcess(), hModule, &modinfo, sizeof(MODULEINFO));
	return modinfo;
}

static bool CheckSignature(const unsigned char* candidate, const unsigned char* pattern, const char* mask,
                           size_t patternLength)
{
	for (size_t j = 0; j < patternLength; j++)
	{
		if (mask[j] != '?' && pattern[j] != candidate[j])
			return false;
	}

	return true;
}

static size_t FindNextPatternOffset(const unsigned char* image, size_t imageSize, const unsigned char* pattern,
                                    const char* mask, size_t patternLength, size_t start)
{
	if (patternLength == 0)
		return start <= imageSize ? start : INVALID_OFFSET;
	if (patternLength > imageSize || start > imageSize - patternLength)
		return INVALID_OFFSET;

	// Most x64 signatures start with the same prefix. The last fixed byte is
	// generally a much better anchor for memchr than the first one.
	size_t anchor = patternLength;
	while (anchor > 0)
	{
		--anchor;
		if (mask[anchor] != '?')
			break;
	}

	if (mask[anchor] == '?')
		return start;

	const unsigned char* cursor = image + start + anchor;
	const unsigned char* end = image + (imageSize - patternLength) + anchor;
	while (cursor <= end)
	{
		size_t remaining = static_cast<size_t>(end - cursor) + 1;
		auto* match = static_cast<const unsigned char*>(std::memchr(cursor, pattern[anchor], remaining));
		if (!match)
			break;

		size_t offset = static_cast<size_t>(match - image) - anchor;
		if (CheckSignature(image + offset, pattern, mask, patternLength))
			return offset;

		cursor = match + 1;
	}

	return INVALID_OFFSET;
}

static size_t FindPattern(const MODULEINFO& moduleInfo, const char* funcname, const char* pattern, const char* mask,
                          size_t hint, bool* hintCorrect, size_t* hintOut)
{
	*hintOut = INVALID_OFFSET;

	auto* image = static_cast<const unsigned char*>(moduleInfo.lpBaseOfDll);
	size_t imageSize = static_cast<size_t>(moduleInfo.SizeOfImage);
	size_t patternLength = strlen(mask);
	if (!image || patternLength > imageSize)
	{
		*hintCorrect = false;
		RAIDHOOK_LOG_WARN(string("Failed to locate function ") + funcname);
		return 0;
	}

	if (hint <= imageSize - patternLength &&
	    CheckSignature(image + hint, reinterpret_cast<const unsigned char*>(pattern), mask, patternLength))
	{
		*hintCorrect = true;
		return reinterpret_cast<size_t>(image + hint);
	}
	*hintCorrect = false;

	size_t offset = FindNextPatternOffset(image, imageSize, reinterpret_cast<const unsigned char*>(pattern), mask,
	                                      patternLength, 0);
	if (offset != INVALID_OFFSET)
	{
#ifdef CHECK_DUPLICATE_SIGNATURES
		size_t duplicate = FindNextPatternOffset(image, imageSize, reinterpret_cast<const unsigned char*>(pattern),
		                                         mask, patternLength, offset + 1);
		if (duplicate != INVALID_OFFSET)
		{
			string err = string("Found duplicate signature for ") + funcname + string(" at ") +
			             to_string(reinterpret_cast<size_t>(image + offset)) + string(",") +
			             to_string(reinterpret_cast<size_t>(image + duplicate));
			RAIDHOOK_LOG_WARN(err);
		}
		else
#endif
		{
			*hintOut = offset;
		}
		return reinterpret_cast<size_t>(image + offset);
	}

	RAIDHOOK_LOG_WARN(string("Failed to locate function ") + funcname);
	return 0;
}

static bool FindAssetLoadSignatures(const MODULEINFO& moduleInfo, SignatureCacheDB& cache, int* cache_misses)
{
	*cache_misses = 0;

	// Kinda hacky: look for the four different resolver functions
	// These are all identical bar calling a different function one time, which we have to mask off
	// to avoid breaking when an update comes out. Since we treat them all the same anyway - we hook them
	// and run the same custom asset loading code - we don't really care which one is which, we just need
	// all of them.
	const char* pattern = "\x48\x89\x54\x24\x10\x55\x53\x56\x57\x41\x54\x41\x56\x41\x57\x48\x8D"
						  "\x6C\x24\xE9\x48\x81\xEC\xE0\x00\x00\x00\x49";
	const char* mask = "xxxxxxxxxxxxxxxxxxxxxxxxxxxx";
	// There should be two copies of this function.
	const size_t target_count = 2;

	auto* image = static_cast<const unsigned char*>(moduleInfo.lpBaseOfDll);
	size_t imageSize = static_cast<size_t>(moduleInfo.SizeOfImage);
	size_t patternLength = strlen(mask);
	if (!image || patternLength > imageSize)
		return false;

	std::vector<void*>& results = try_open_functions;

	// Implement caching - if all the signatures are at the same place, assume it's still working
	size_t cache_count = cache.GetAddress("asset_load_signatures_count");
	if (cache_count == target_count)
	{
		for (size_t i = 0; i < cache_count; i++)
		{
			size_t target = cache.GetAddress("asset_load_signatures_id_" + to_string(i));

			// Make sure this signature is in-bounds
			if (target > imageSize - patternLength)
				goto cache_fail;

			if (!CheckSignature(image + target, reinterpret_cast<const unsigned char*>(pattern), mask, patternLength))
				goto cache_fail;
			results.push_back(const_cast<unsigned char*>(image + target));
		}
		return true; // cache was good

	cache_fail:
		results.clear();
	}

	// Make sure the cache gets updated afterwards
	(*cache_misses)++;

	for (size_t offset = FindNextPatternOffset(image, imageSize, reinterpret_cast<const unsigned char*>(pattern), mask,
	                                           patternLength, 0);
	     offset != INVALID_OFFSET;
	     offset = FindNextPatternOffset(image, imageSize, reinterpret_cast<const unsigned char*>(pattern), mask,
	                                    patternLength, offset + 1))
	{
		size_t result = reinterpret_cast<size_t>(image + offset);

		std::stringstream hex_address;
		hex_address << "0x" << std::hex << result;

		// Some games (PDTH) have very similar try_open signatures, so double check here.
		if (result == (size_t)try_open_property_match_resolver)
		{
			RAIDHOOK_LOG_LOG(string("Asset loading signature (") + hex_address.str() +
			                 string(") matched 'try_open_property_match_resolver' (") + hex_address.str() +
			                 string(") ignoring..."));

			continue;
		}

		cache.UpdateAddress("asset_load_signatures_id_" + to_string(results.size()), offset);
		results.push_back((void*)result);

		RAIDHOOK_LOG_LOG(string("Found signature #") + to_string(results.size()) + string(" for asset loading at ") +
		                 hex_address.str());
	}

	cache.UpdateAddress("asset_load_signatures_count", results.size());

	if (target_count > results.size())
	{
		RAIDHOOK_LOG_WARN(string("Failed to locate enough instances of the asset loading function:"));
	}
	else if (target_count < results.size())
	{
		RAIDHOOK_LOG_WARN(string("Located too many instances of the asset loading function:"));
	}
	else
	{
		return true; // cache was bad, but sigs still seem fine, at least count matches
	}

	return false; // sig count mismatch, something went wrong
}

std::vector<SignatureF>* allSignatures = NULL;

SignatureSearch::SignatureSearch(const char* funcname, void* adress, const char* signature, const char* mask,
                                 int offset)
{
	// lazy-init, container gets 'emptied' when initialized on compile.
	if (!allSignatures)
	{
		allSignatures = new std::vector<SignatureF>();
	}

	SignatureF ins = {funcname, signature, mask, offset, adress};
	allSignatures->push_back(ins);
}

bool SignatureSearch::Search()
{
	// Find the name of the current EXE
	TCHAR processPath[MAX_PATH + 1];
	GetModuleFileName(NULL, processPath, MAX_PATH + 1); // Get the path
	TCHAR filename[MAX_PATH + 1];
	_splitpath_s( // Find the filename part of the path
		processPath, // Input
		NULL, 0, // Don't care about the drive letter
		NULL, 0, // Don't care about the directory
		filename, MAX_PATH, // Grab the filename
		NULL, 0 // Extension is always .exe
	);

	string basename = filename;

	// Add the .exe back on
	strcat_s(filename, MAX_PATH, ".exe");
	MODULEINFO moduleInfo = GetModuleInfo(filename);
	if (!moduleInfo.lpBaseOfDll || moduleInfo.SizeOfImage == 0)
	{
		RAIDHOOK_LOG_ERROR(string("Failed to inspect module ") + filename);
		return false;
	}

	unsigned long ms_start = GetTickCount64();
	SignatureCacheDB cache(string("sigcache_") + basename + string(".db"));
	RAIDHOOK_LOG_LOG(string("Scanning for signatures in ") + string(filename));

	int cacheMisses = 0;
	bool hasError = false;
	std::vector<SignatureF>::iterator it;
	for (it = allSignatures->begin(); it < allSignatures->end(); it++)
	{
		string funcname = it->funcname;
		size_t hint = cache.GetAddress(funcname);

		bool hintCorrect;
		size_t hintOut = INVALID_OFFSET;
		size_t match = FindPattern(moduleInfo, it->funcname, it->signature, it->mask, hint, &hintCorrect, &hintOut);
		size_t addr = match ? match + it->offset : 0;
		*((void**)it->address) = (void*)addr;

		if (match == 0)
		{
			hintCorrect = true; // If the signature doesn't exist at all, it's not the cache's fault
			if (!hasError)
				hasError = true;
		}
		else if (hint == INVALID_OFFSET)
		{
			RAIDHOOK_LOG_LOG(string("Sigcache hit failed for function ") + funcname);
		}
		else if (!hintCorrect)
		{
			RAIDHOOK_LOG_WARN(string("Sigcache for function ") + funcname + " incorrect (" + to_string(hint) + " vs " +
			                  to_string(hintOut) + ")!");
		}

		if (!hintCorrect && hintOut != INVALID_OFFSET)
		{
			cache.UpdateAddress(funcname, hintOut);
			cacheMisses++;
		}

		std::stringstream hex_address;
		hex_address << "0x" << std::hex << addr;
		RAIDHOOK_LOG_LOG(funcname + ": " + hex_address.str());
	}

	int asset_cache_misses = 0;
	if (!FindAssetLoadSignatures(moduleInfo, cache, &asset_cache_misses) && !hasError)
		hasError = true;
	cacheMisses += asset_cache_misses;

	unsigned long ms_end = GetTickCount64();

	RAIDHOOK_LOG_LOG(string("Scanned for ") + to_string(allSignatures->size()) + string(" signatures in ") +
	                 to_string((int)(ms_end - ms_start)) + string(" milliseconds with ") + to_string(cacheMisses) +
	                 string(" cache misses"));

	if (cacheMisses > 0)
	{
		RAIDHOOK_LOG_LOG("Saving signature cache");
		cache.Save();
	}

	return !hasError;
}

void* SignatureSearch::GetFunctionByName(const char* name)
{
	if (!allSignatures)
		return NULL;

	for (const auto& sig : *allSignatures)
	{
		if (!strcmp(sig.funcname, name))
		{
			return *(void**)sig.address;
		}
	}

	return NULL;
}
