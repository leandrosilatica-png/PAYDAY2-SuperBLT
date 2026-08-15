//
// Created by ZNix on 16/08/2020.
//

#include <vector>
#define INCLUDE_TRY_OPEN_FUNCTIONS

#include "../dbutil/DB.h"
#include "assets.h"
#include "platform.h"
#include "subhook.h"

#include <stdio.h>

#include <string>
#include <tweaker/db_hooks.h>
#include <unordered_map>
#include <utility>

#include <AK/SoundEngine/Common/AkSoundEngine.h>

using raidhook::tweaker::dbhook::hook_asset_load;

#define HOOK_FLAG subhook::HookFlags::HookFlag64BitOffset
#include "assets/assets_game.cpp"

// Three hooks for the other try_open functions: property_match_resolver, language_resolver and english_resolver
DECLARE_PASSTHROUGH_ARRAY(0)
DECLARE_PASSTHROUGH_ARRAY(1)
DECLARE_PASSTHROUGH_ARRAY(2)
DECLARE_PASSTHROUGH(try_open_property_match_resolver)

static subhook::Hook WwDevice_loadBankIdstringDetour;
static subhook::Hook WwDevice_idToEntryDetour;

// Access happens on two different threads
std::mutex customWwiseMapsMutex;
std::unordered_map<blt::idstring, std::string> customWwiseSoundbankNames;
std::unordered_map<unsigned int, blt::idstring> customWwiseIdToEntryNames;

class sound_WwDevice
{
  public:
	virtual ~sound_WwDevice() = 0;
	virtual void unneeded_virtual_1() = 0;
	virtual void unneeded_virtual_2() = 0;
	virtual void unneeded_virtual_3() = 0;
	virtual void unneeded_virtual_4() = 0;
	virtual void unneeded_virtual_5() = 0;
	virtual SoundBank* load_bank_idstring(idstr bank, int async) = 0;
	virtual SoundBank* load_bank_string(const char* bank, bool async) = 0;

  public:
};

SoundBank* sound_WwDevice__load_bank_idstring_h(sound_WwDevice* this_, idstr bank, bool async)
{
	subhook::ScopedHookRemove scoped_remove(&WwDevice_loadBankIdstringDetour);

	SoundBank* soundbank = (SoundBank*)sound_WwDevice__load_bank_idstring(this_, bank, async);

	if (soundbank == nullptr)
	{
		std::string bankName;
		{
			std::lock_guard customListLock(customWwiseMapsMutex);
			auto entry = customWwiseSoundbankNames.find(bank._id);
			if (entry != customWwiseSoundbankNames.end())
				bankName = entry->second;
		}

		if (!bankName.empty())
			soundbank = this_->load_bank_string(bankName.c_str(), async);
	}

	return soundbank;
}

idstr* sound_WwDevice__id_to_entry_h(sound_WwDevice* this_, idstr* result, unsigned int wwise_id)
{
	subhook::ScopedHookRemove scoped_remove(&WwDevice_idToEntryDetour);

	result = sound_WwDevice__id_to_entry(this_, result, wwise_id);

	if (result->_id != 0x8DB63936938575BF) // empty idstring (""), default return value from
	                                       // sound::WwDevice::id_to_entry if no match was found
	{
		return result;
	}

	std::lock_guard customListLock(customWwiseMapsMutex);

	auto entry = customWwiseIdToEntryNames.find(wwise_id);
	if (entry != customWwiseIdToEntryNames.end())
		result->_id = entry->second;

	return result;
}

void blt::win32::InitAssets()
{
#define SETUP_PASSTHROUGH(func) hook_##func.Install((void*)func, (void*)&stub_##func, HOOK_FLAG)
#define SETUP_PASSTHROUGH_ARRAY(id) hook_##id.Install((void*)try_open_functions.at(id), (void*)&stub_##id, HOOK_FLAG)

	if (!try_open_functions.empty())
		SETUP_PASSTHROUGH_ARRAY(0);
	if (try_open_functions.size() > 1)
		SETUP_PASSTHROUGH_ARRAY(1);
	if (try_open_functions.size() > 2)
		SETUP_PASSTHROUGH_ARRAY(2);

	SETUP_PASSTHROUGH(try_open_property_match_resolver);

	setup_extra_asset_hooks();
	blt::InitDBHooks();

	WwDevice_loadBankIdstringDetour.Install(sound_WwDevice__load_bank_idstring, &sound_WwDevice__load_bank_idstring_h,
	                                        HOOK_FLAG);
	WwDevice_idToEntryDetour.Install(sound_WwDevice__id_to_entry, &sound_WwDevice__id_to_entry_h, HOOK_FLAG);
}

unsigned int GetWwiseHash(const char* str)
{
	unsigned int hash = 2166136261;
	while (*str)
	{
		hash = *str++ ^ (16777619 * hash);
	}
	return hash;
}

void blt::platform::wwise::RegisterCustomSoundbank(const char* dbPath)
{
	if (!dbPath)
		return;

	std::lock_guard customListLock(customWwiseMapsMutex);

	blt::idstring hashedPath = blt::idstring_hash(dbPath);
	unsigned int wwiseHash = GetWwiseHash(dbPath);
	customWwiseSoundbankNames.insert_or_assign(hashedPath, dbPath);
	customWwiseIdToEntryNames.insert_or_assign(wwiseHash, hashedPath);
}

void blt::platform::wwise::UnregisterCustomSoundbank(const char* dbPath)
{
	if (!dbPath)
		return;

	std::lock_guard customListLock(customWwiseMapsMutex);

	blt::idstring hashedPath = blt::idstring_hash(dbPath);

	unsigned int wwiseHash = GetWwiseHash(dbPath);
	customWwiseSoundbankNames.erase(hashedPath);
	customWwiseIdToEntryNames.erase(wwiseHash);
}

void blt::platform::wwise::RegisterCustomStreamedWemPath(unsigned int wemId, const char* dbPath)
{
	if (!dbPath)
		return;

	std::lock_guard customListLock(customWwiseMapsMutex);

	customWwiseIdToEntryNames.insert_or_assign(wemId, blt::idstring_hash(dbPath));
}

void blt::platform::wwise::UnregisterCustomStreamedWemPath(unsigned int wemId)
{
	std::lock_guard customListLock(customWwiseMapsMutex);

	customWwiseIdToEntryNames.erase(wemId);
}
