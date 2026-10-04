/*
 * mod-lootrandomizer for AzerothCore
 * Adds configurable random loot drops to killed creatures.
 */

#include "Config.h"
#include "Creature.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "LootMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr uint8 MAX_PLAYER_LEVEL = 80;
    constexpr uint32 ITEM_SUBCLASS_MISC_PET = 2;
    constexpr uint32 ITEM_SUBCLASS_MISC_MOUNT = 5;

    struct EligibleItemTypes
    {
        bool FilterEnabled = false;

        // Broad class groups
        bool Weapons = true;
        bool Armor = true;
        bool Consumables = true;
        bool Containers = true;
        bool Gems = true;
        bool Reagents = true;
        bool Projectiles = true;
        bool TradeGoods = true;
        bool Recipes = true;
        bool Quivers = true;
        bool Quest = true;
        bool Keys = true;
        bool Misc = true;
        bool Glyphs = true;

        // Equipment families / slots
        bool Jewelry = true;          // ring, neck, trinket
        bool Rings = true;
        bool Necklaces = true;
        bool Trinkets = true;
        bool Cloaks = true;
        bool Shields = true;
        bool Relics = true;
        bool Holdables = true;
        bool Bags = true;
        bool Tabards = true;
        bool Shirts = true;

        bool Head = true;
        bool Shoulders = true;
        bool Chest = true;            // chest + robe
        bool Waist = true;
        bool Legs = true;
        bool Feet = true;
        bool Wrists = true;
        bool Hands = true;

        bool OneHandWeapons = true;   // weapon, main hand, off hand
        bool TwoHandWeapons = true;
        bool RangedWeapons = true;    // ranged, rangedright, thrown
    };

    struct RandomLootFilters
    {
        bool QualityFilterEnabled = false;
        bool QualityPoor = true;
        bool QualityCommon = true;
        bool QualityUncommon = true;
        bool QualityRare = true;
        bool QualityEpic = true;
        bool QualityLegendary = true;
        bool QualityArtifact = true;
        bool QualityHeirloom = true;

        int32 RequiredLevelMin = 0;
        int32 RequiredLevelMax = 0;

        int32 ItemLevelMin = 0;
        int32 ItemLevelMax = 0;

        bool ExpansionFilterEnabled = false;
        bool ExpansionClassic = true;
        bool ExpansionTBC = true;
        bool ExpansionWrath = true;

        bool BondingFilterEnabled = false;
        bool BondingNoBind = true;
        bool BondingBindOnPickup = true;
        bool BondingBindOnEquip = true;
        bool BondingBindOnUse = true;
        bool BondingQuestItem = true;
        bool BondingQuestItemUnused = true;

        bool RequireExistingLoot = false;
    };

    struct PlayerLevelBracket
    {
        bool Enabled = false;
        bool BracketEquippableOnly = false;
        bool ConfigurationValid = true;
        uint8 UpperLevelOffset = 10;
        std::array<uint32, MAX_PLAYER_LEVEL + 1> MinItemLevel {};
        std::array<uint32, MAX_PLAYER_LEVEL + 1> MaxItemLevel {};
    };

    struct RequiredLevelBracket
    {
        bool Enabled = false;
        uint8 LevelOffset = 5;
        bool IncludeUnrestrictedItems = false;
    };

    struct CompanionLootConfig
    {
        bool PetsEnabled = true;
        float PetChance = 1.0f;
        bool MountsEnabled = true;
        float MountChance = 1.0f;
    };

    class RandomLootState
    {
    public:
        void LoadConfig()
        {
            std::unique_lock lock(_mutex);

            _enabled = sConfigMgr->GetOption<bool>("RandomLoot.Enable", false);

            _minItems = std::max<int32>(0, sConfigMgr->GetOption<int32>("RandomLoot.MinItems", 1));
            _maxItems = std::max<int32>(_minItems, sConfigMgr->GetOption<int32>("RandomLoot.MaxItems", 1));

            _hasBuiltPoolSinceConfig = false;
            _templatesWereEmptyOnLastBuild = true;

            _eligibleTypes.FilterEnabled = sConfigMgr->GetOption<bool>("RandomLoot.Filter.TypeFilterEnabled", false, false);

            _eligibleTypes.Weapons = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Weapons", true, false);
            _eligibleTypes.Armor = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Armor", true, false);
            _eligibleTypes.Consumables = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Consumables", true, false);
            _eligibleTypes.Containers = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Containers", true, false);
            _eligibleTypes.Gems = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Gems", true, false);
            _eligibleTypes.Reagents = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Reagents", true, false);
            _eligibleTypes.Projectiles = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Projectiles", true, false);
            _eligibleTypes.TradeGoods = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.TradeGoods", true, false);
            _eligibleTypes.Recipes = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Recipes", true, false);
            _eligibleTypes.Quivers = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Quivers", true, false);
            _eligibleTypes.Quest = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Quest", true, false);
            _eligibleTypes.Keys = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Keys", true, false);
            _eligibleTypes.Misc = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Misc", true, false);
            _eligibleTypes.Glyphs = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Glyphs", true, false);

            _eligibleTypes.Jewelry = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Jewelry", true, false);
            _eligibleTypes.Rings = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Rings", true, false);
            _eligibleTypes.Necklaces = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Necklaces", true, false);
            _eligibleTypes.Trinkets = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Trinkets", true, false);
            _eligibleTypes.Cloaks = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Cloaks", true, false);
            _eligibleTypes.Shields = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Shields", true, false);
            _eligibleTypes.Relics = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Relics", true, false);
            _eligibleTypes.Holdables = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Holdables", true, false);
            _eligibleTypes.Bags = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Bags", true, false);
            _eligibleTypes.Tabards = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Tabards", true, false);
            _eligibleTypes.Shirts = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Shirts", true, false);

            _eligibleTypes.Head = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Head", true, false);
            _eligibleTypes.Shoulders = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Shoulders", true, false);
            _eligibleTypes.Chest = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Chest", true, false);
            _eligibleTypes.Waist = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Waist", true, false);
            _eligibleTypes.Legs = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Legs", true, false);
            _eligibleTypes.Feet = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Feet", true, false);
            _eligibleTypes.Wrists = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Wrists", true, false);
            _eligibleTypes.Hands = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.Hands", true, false);

            _eligibleTypes.OneHandWeapons = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.OneHandWeapons", true, false);
            _eligibleTypes.TwoHandWeapons = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.TwoHandWeapons", true, false);
            _eligibleTypes.RangedWeapons = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Include.RangedWeapons", true, false);

            if (_eligibleTypes.FilterEnabled && !HasAnyEligibleTypeSelected())
            {
                LOG_WARN("module.RandomLoot", "mod-lootrandomizer: RandomLoot.Filter.TypeFilterEnabled is true but no RandomLoot.Filter.Include.* keys are enabled. Falling back to TypeFilterEnabled=false.");
                _eligibleTypes.FilterEnabled = false;
            }

            _filters.QualityFilterEnabled = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Quality.FilterEnabled", false, false);
            _filters.QualityPoor = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Quality.Include.Poor", true, false);
            _filters.QualityCommon = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Quality.Include.Common", true, false);
            _filters.QualityUncommon = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Quality.Include.Uncommon", true, false);
            _filters.QualityRare = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Quality.Include.Rare", true, false);
            _filters.QualityEpic = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Quality.Include.Epic", true, false);
            _filters.QualityLegendary = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Quality.Include.Legendary", true, false);
            _filters.QualityArtifact = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Quality.Include.Artifact", true, false);
            _filters.QualityHeirloom = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Quality.Include.Heirloom", true, false);

            if (_filters.QualityFilterEnabled && !HasAnyQualitySelected())
            {
                LOG_WARN("module.RandomLoot", "mod-lootrandomizer: RandomLoot.Filter.Quality.FilterEnabled is true but no quality options are enabled. Falling back to Quality.FilterEnabled=false.");
                _filters.QualityFilterEnabled = false;
            }

            _filters.RequiredLevelMin = std::max<int32>(0, sConfigMgr->GetOption<int32>("RandomLoot.Filter.RequiredLevel.Min", 0, false));
            _filters.RequiredLevelMax = std::max<int32>(0, sConfigMgr->GetOption<int32>("RandomLoot.Filter.RequiredLevel.Max", 0, false));

            _filters.ItemLevelMin = std::max<int32>(0, sConfigMgr->GetOption<int32>("RandomLoot.Filter.ItemLevel.Min", 0, false));
            _filters.ItemLevelMax = std::max<int32>(0, sConfigMgr->GetOption<int32>("RandomLoot.Filter.ItemLevel.Max", 0, false));

            _filters.ExpansionFilterEnabled = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Expansion.FilterEnabled", false, false);
            _filters.ExpansionClassic = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Expansion.Include.Classic", true, false);
            _filters.ExpansionTBC = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Expansion.Include.TBC", true, false);
            _filters.ExpansionWrath = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Expansion.Include.Wrath", true, false);

            if (_filters.ExpansionFilterEnabled && !HasAnyExpansionSelected())
            {
                LOG_WARN("module.RandomLoot", "mod-lootrandomizer: RandomLoot.Filter.Expansion.FilterEnabled is true but no expansion options are enabled. Falling back to Expansion.FilterEnabled=false.");
                _filters.ExpansionFilterEnabled = false;
            }

            _filters.BondingFilterEnabled = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Bonding.FilterEnabled", false, false);
            _filters.BondingNoBind = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Bonding.Include.NoBind", true, false);
            _filters.BondingBindOnPickup = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Bonding.Include.BindOnPickup", true, false);
            _filters.BondingBindOnEquip = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Bonding.Include.BindOnEquip", true, false);
            _filters.BondingBindOnUse = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Bonding.Include.BindOnUse", true, false);
            _filters.BondingQuestItem = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Bonding.Include.QuestItem", true, false);
            _filters.BondingQuestItemUnused = sConfigMgr->GetOption<bool>("RandomLoot.Filter.Bonding.Include.QuestItemUnused", true, false);

            if (_filters.BondingFilterEnabled && !HasAnyBondingSelected())
            {
                LOG_WARN("module.RandomLoot", "mod-lootrandomizer: RandomLoot.Filter.Bonding.FilterEnabled is true but no bonding options are enabled. Falling back to Bonding.FilterEnabled=false.");
                _filters.BondingFilterEnabled = false;
            }

            _filters.RequireExistingLoot = sConfigMgr->GetOption<bool>("RandomLoot.Filter.RequireExistingLoot", false, false);

            LoadPlayerLevelBracketConfig();
            LoadRequiredLevelBracketConfig();

            _companionLoot.PetsEnabled = sConfigMgr->GetOption<bool>("RandomLoot.Companion.Pets.Enabled", true, false);
            _companionLoot.PetChance = std::clamp(sConfigMgr->GetOption<float>("RandomLoot.Companion.Pets.Chance", 1.0f, false), 0.0f, 100.0f);
            _companionLoot.MountsEnabled = sConfigMgr->GetOption<bool>("RandomLoot.Companion.Mounts.Enabled", true, false);
            _companionLoot.MountChance = std::clamp(sConfigMgr->GetOption<float>("RandomLoot.Companion.Mounts.Chance", 1.0f, false), 0.0f, 100.0f);

            // Account exclusions
            _excludedAccountIds.clear();
            _allowRandomLootWhenGrouped = sConfigMgr->GetOption<bool>("RandomLoot.Account.AllowRandomLootWhenGrouped", false, false);
            std::string excludeList = sConfigMgr->GetOption<std::string>("RandomLoot.Account.ExcludeIds", "", false);
            if (!excludeList.empty())
            {
                std::istringstream ss(excludeList);
                std::string token;
                while (std::getline(ss, token, ','))
                {
                    try { _excludedAccountIds.insert(static_cast<uint32>(std::stoul(token))); }
                    catch (...) { LOG_WARN("module.RandomLoot", "mod-lootrandomizer: invalid account id in RandomLoot.Account.ExcludeIds: '{}'", token); }
                }
            }

            _hasExcludedAccountRange = false;
            std::string excludeRange = sConfigMgr->GetOption<std::string>("RandomLoot.Account.ExcludeRange", "", false);
            if (!excludeRange.empty())
            {
                std::istringstream rangeStream(excludeRange);
                std::vector<std::string> rangeValues;
                std::string value;
                while (std::getline(rangeStream, value, ','))
                    rangeValues.push_back(value);

                if (rangeValues.size() != 2)
                {
                    LOG_WARN("module.RandomLoot", "mod-lootrandomizer: RandomLoot.Account.ExcludeRange must contain exactly two comma-separated account IDs: '{}'.", excludeRange);
                }
                else
                {
                    try
                    {
                        uint64 rangeStart = std::stoull(rangeValues[0]);
                        uint64 rangeEnd = std::stoull(rangeValues[1]);
                        if (rangeStart > std::numeric_limits<uint32>::max() ||
                            rangeEnd > std::numeric_limits<uint32>::max() || rangeStart > rangeEnd)
                        {
                            LOG_WARN("module.RandomLoot", "mod-lootrandomizer: invalid RandomLoot.Account.ExcludeRange: '{}'.", excludeRange);
                        }
                        else
                        {
                            _excludedAccountRangeStart = static_cast<uint32>(rangeStart);
                            _excludedAccountRangeEnd = static_cast<uint32>(rangeEnd);
                            _hasExcludedAccountRange = true;
                        }
                    }
                    catch (...)
                    {
                        LOG_WARN("module.RandomLoot", "mod-lootrandomizer: invalid RandomLoot.Account.ExcludeRange: '{}'.", excludeRange);
                    }
                }
            }

            _chanceByLevel.clear();

            for (uint8 level = 1; level <= 80; ++level)
            {
                float value = sConfigMgr->GetOption<float>("RandomLoot.Chance.Level." + std::to_string(level), -1.0f, false);
                if (value >= 0.0f)
                    _chanceByLevel[level] = value;
            }
        }

        void RebuildItemPool()
        {
            std::unique_lock lock(_mutex);

            RebuildItemPoolLocked();
        }

        void RebuildItemPoolLocked()
        {
            // Caller must hold _mutex in unique mode.

            _eligibleItemIds.clear();
            _petItemIds.clear();
            _mountItemIds.clear();
            for (std::vector<uint32>& itemIds : _playerLevelEligibleItemIds)
                itemIds.clear();

            if (!_enabled)
                return;

            ItemTemplateContainer const* items = sObjectMgr->GetItemTemplateStore();
            if (!items)
            {
                _templatesWereEmptyOnLastBuild = true;
                _hasBuiltPoolSinceConfig = true;
                return;
            }

            _templatesWereEmptyOnLastBuild = items->empty();
            _hasBuiltPoolSinceConfig = true;

            _eligibleItemIds.reserve(items->size());
            _petItemIds.reserve(items->size());
            _mountItemIds.reserve(items->size());

            for (ItemTemplateContainer::const_iterator itr = items->begin(); itr != items->end(); ++itr)
            {
                ItemTemplate const& itemTemplate = itr->second;
                if (IsPet(itemTemplate))
                {
                    if (MatchesFilters(itemTemplate, true))
                        _petItemIds.push_back(itemTemplate.ItemId);
                }
                else if (IsMount(itemTemplate))
                {
                    if (MatchesFilters(itemTemplate, true))
                        _mountItemIds.push_back(itemTemplate.ItemId);
                }
                else if (MatchesFilters(itemTemplate))
                    _eligibleItemIds.push_back(itemTemplate.ItemId);
            }

            if (UsesDynamicLevelBrackets())
                RebuildPlayerLevelCandidatePoolsLocked();

            LOG_INFO("module.RandomLoot", "mod-lootrandomizer built item pools: {} normal items, {} pets, {} mounts", _eligibleItemIds.size(), _petItemIds.size(), _mountItemIds.size());

            if (_eligibleItemIds.empty() && _petItemIds.empty() && _mountItemIds.empty())
            {
                uint32 equippableCount = 0;
                uint32 qualityMatchCount = 0;
                uint32 typeMatchCount = 0;

                for (ItemTemplateContainer::const_iterator itr = items->begin(); itr != items->end(); ++itr)
                {
                    ItemTemplate const& itemTemplate = itr->second;

                    if (itemTemplate.InventoryType != 0)
                        ++equippableCount;

                    if (MatchesQualityFilter(itemTemplate))
                        ++qualityMatchCount;

                    if (MatchesEligibleTypes(itemTemplate))
                        ++typeMatchCount;
                }

                LOG_WARN("module.RandomLoot",
                    "mod-lootrandomizer eligible pool is empty. item_template rows={}, typeMatches={}, equippable={}, qualityMatches={}. Current filters: TypeFilterEnabled={}, QualityFilterEnabled={}, ExpansionFilterEnabled={}, BondingFilterEnabled={}",
                    items->size(), typeMatchCount, equippableCount, qualityMatchCount,
                    _eligibleTypes.FilterEnabled, _filters.QualityFilterEnabled, _filters.ExpansionFilterEnabled, _filters.BondingFilterEnabled);
            }
        }

        void TryAddLoot(Player* killer, Creature* killed)
        {
            if (!killer || !killed)
                return;

            std::unique_lock lock(_mutex);

            if (!_enabled)
                return;

            if (_playerLevelBracket.Enabled && !_playerLevelBracket.ConfigurationValid)
                return;

            uint32 accountId = killer->GetSession()->GetAccountId();
            if (IsAccountExcluded(accountId) &&
                !(_allowRandomLootWhenGrouped && killer->GetGroup()))
                return;

            // Retry pool build only when needed: either never built since config load, or templates were empty on last build.
            if (_eligibleItemIds.empty() && _petItemIds.empty() && _mountItemIds.empty() &&
                (!_hasBuiltPoolSinceConfig || _templatesWereEmptyOnLastBuild))
                RebuildItemPoolLocked();

            uint8 playerLevel = std::clamp<uint8>(killer->GetLevel(), 1, MAX_PLAYER_LEVEL);
            std::vector<uint32> const& candidateItemIds = UsesDynamicLevelBrackets()
                ? _playerLevelEligibleItemIds[playerLevel]
                : _eligibleItemIds;

            bool hasPetCandidates = _companionLoot.PetsEnabled && !_petItemIds.empty();
            bool hasMountCandidates = _companionLoot.MountsEnabled && !_mountItemIds.empty();
            if (candidateItemIds.empty() && !hasPetCandidates && !hasMountCandidates)
                return;

            bool hadAnyLootBeforeRandom = !killed->loot.isLooted();

            if (_filters.RequireExistingLoot && !hadAnyLootBeforeRandom)
                return;

            // Loot::AddItem relies on lootOwnerGUID to determine whether generated items are lootable.
            // Some creatures (especially with no base loot template) can reach this hook without one set.
            if (killed->loot.lootOwnerGUID.IsEmpty())
            {
                if (Player* lootOwner = killed->GetLootRecipient())
                    killed->loot.lootOwnerGUID = lootOwner->GetGUID();
                else
                    killed->loot.lootOwnerGUID = killer->GetGUID();
            }

            if (killed->loot.items.size() >= MAX_NR_LOOT_ITEMS)
                return;

            bool addedAnyRandomLoot = false;

            if (!candidateItemIds.empty())
            {
                float chance = GetChanceForLevel(killed->GetLevel());
                if (chance > 0.0f && roll_chance_f(chance))
                {
                    uint32 minItems = static_cast<uint32>(_minItems);
                    uint32 maxItems = static_cast<uint32>(_maxItems);

                    if (maxItems < minItems)
                        std::swap(maxItems, minItems);

                    uint32 countToAdd = (minItems == maxItems) ? minItems : urand(minItems, maxItems);
                    addedAnyRandomLoot = AddRandomLootItems(killed, candidateItemIds, countToAdd);
                }
            }

            if (hasPetCandidates && roll_chance_f(_companionLoot.PetChance))
                addedAnyRandomLoot = AddRandomLootItems(killed, _petItemIds, 1) || addedAnyRandomLoot;

            if (hasMountCandidates && roll_chance_f(_companionLoot.MountChance))
                addedAnyRandomLoot = AddRandomLootItems(killed, _mountItemIds, 1) || addedAnyRandomLoot;

            // If base loot was empty, core may have already removed the lootable flag.
            // Ensure the corpse becomes lootable when random loot is added.
            if (addedAnyRandomLoot && !hadAnyLootBeforeRandom)
            {
                killed->SetDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
            }
        }

    private:
        bool IsAccountExcluded(uint32 accountId) const
        {
            if (!_excludedAccountIds.empty() && _excludedAccountIds.count(accountId))
                return true;

            return _hasExcludedAccountRange && accountId >= _excludedAccountRangeStart &&
                accountId <= _excludedAccountRangeEnd;
        }

        void LoadPlayerLevelBracketConfig()
        {
            _playerLevelBracket = {};
            _playerLevelBracket.Enabled = sConfigMgr->GetOption<bool>("RandomLoot.Filter.PlayerLevelBracket.Enabled", false, false);
            _playerLevelBracket.BracketEquippableOnly = sConfigMgr->GetOption<bool>("RandomLoot.Filter.PlayerLevelBracket.BracketEquippableOnly", true, false);

            int32 configuredOffset = sConfigMgr->GetOption<int32>("RandomLoot.Filter.PlayerLevelBracket.UpperLevelOffset", 10, false);
            _playerLevelBracket.UpperLevelOffset = static_cast<uint8>(std::clamp<int32>(configuredOffset, 0, MAX_PLAYER_LEVEL));

            if (!_playerLevelBracket.Enabled)
                return;

            bool valid = true;

            for (uint8 level = 1; level <= MAX_PLAYER_LEVEL; ++level)
            {
                std::string mapping = sConfigMgr->GetOption<std::string>("RandomLoot.Filter.PlayerLevelBracket.MinMaxItemLevel." + std::to_string(level), "", false);
                size_t separator = mapping.find(',');
                if (separator == std::string::npos || mapping.find(',', separator + 1) != std::string::npos)
                {
                    LOG_ERROR("module.RandomLoot", "mod-lootrandomizer: invalid PlayerLevelBracket mapping for player level {}. MinMaxItemLevel must contain exactly two comma-separated values.", level);
                    valid = false;
                    continue;
                }

                try
                {
                    size_t minLength = 0;
                    size_t maxLength = 0;
                    uint64 minItemLevel = std::stoull(mapping.substr(0, separator), &minLength);
                    uint64 maxItemLevel = std::stoull(mapping.substr(separator + 1), &maxLength);
                    if (minItemLevel == 0 || maxItemLevel == 0 || minItemLevel > maxItemLevel ||
                        minItemLevel > std::numeric_limits<uint32>::max() || maxItemLevel > std::numeric_limits<uint32>::max() ||
                        minLength != separator || maxLength != mapping.size() - separator - 1)
                    {
                        LOG_ERROR("module.RandomLoot", "mod-lootrandomizer: invalid PlayerLevelBracket mapping for player level {}: '{}'.", level, mapping);
                        valid = false;
                        continue;
                    }

                    _playerLevelBracket.MinItemLevel[level] = static_cast<uint32>(minItemLevel);
                    _playerLevelBracket.MaxItemLevel[level] = static_cast<uint32>(maxItemLevel);
                }
                catch (...)
                {
                    LOG_ERROR("module.RandomLoot", "mod-lootrandomizer: invalid PlayerLevelBracket mapping for player level {}: '{}'.", level, mapping);
                    valid = false;
                }
            }

            for (uint8 level = 1; level <= MAX_PLAYER_LEVEL; ++level)
            {
                uint8 lowerLevel = level > _playerLevelBracket.UpperLevelOffset
                    ? level - _playerLevelBracket.UpperLevelOffset
                    : 1;
                uint8 upperLevel = std::min<uint8>(MAX_PLAYER_LEVEL, level + _playerLevelBracket.UpperLevelOffset);
                if (_playerLevelBracket.MinItemLevel[lowerLevel] > _playerLevelBracket.MaxItemLevel[upperLevel])
                {
                    LOG_ERROR("module.RandomLoot", "mod-lootrandomizer: PlayerLevelBracket range is empty for player level {} with boundaries {} through {}.", level, lowerLevel, upperLevel);
                    valid = false;
                }
            }

            if (!valid)
            {
                LOG_ERROR("module.RandomLoot", "mod-lootrandomizer: PlayerLevelBracket is enabled but invalid. No random loot will be added until its configuration is corrected.");
                _playerLevelBracket.ConfigurationValid = false;
            }
        }

        void LoadRequiredLevelBracketConfig()
        {
            _requiredLevelBracket = {};
            _requiredLevelBracket.Enabled = sConfigMgr->GetOption<bool>("RandomLoot.Filter.RequiredLevelBracket.Enabled", false, false);
            _requiredLevelBracket.LevelOffset = static_cast<uint8>(std::clamp<int32>(
                sConfigMgr->GetOption<int32>("RandomLoot.Filter.RequiredLevelBracket.LevelOffset", 5, false),
                0, MAX_PLAYER_LEVEL));
            _requiredLevelBracket.IncludeUnrestrictedItems = sConfigMgr->GetOption<bool>(
                "RandomLoot.Filter.RequiredLevelBracket.IncludeUnrestrictedItems", false, false);
        }

        bool UsesDynamicLevelBrackets() const
        {
            return _playerLevelBracket.Enabled || _requiredLevelBracket.Enabled;
        }

        bool MatchesRequiredLevelBracket(ItemTemplate const& itemTemplate, uint8 playerLevel) const
        {
            if (!_requiredLevelBracket.Enabled)
                return true;

            if (_requiredLevelBracket.IncludeUnrestrictedItems && itemTemplate.RequiredLevel <= 1)
                return true;

            uint8 lowerLevel = playerLevel > _requiredLevelBracket.LevelOffset
                ? playerLevel - _requiredLevelBracket.LevelOffset
                : 1;
            uint8 upperLevel = std::min<uint8>(MAX_PLAYER_LEVEL, playerLevel + _requiredLevelBracket.LevelOffset);
            return itemTemplate.RequiredLevel >= lowerLevel && itemTemplate.RequiredLevel <= upperLevel;
        }

        void RebuildPlayerLevelCandidatePoolsLocked()
        {
            if (_playerLevelBracket.Enabled && !_playerLevelBracket.ConfigurationValid)
                return;

            for (uint8 playerLevel = 1; playerLevel <= MAX_PLAYER_LEVEL; ++playerLevel)
            {
                uint8 lowerLevel = playerLevel > _playerLevelBracket.UpperLevelOffset
                    ? playerLevel - _playerLevelBracket.UpperLevelOffset
                    : 1;
                uint8 upperLevel = std::min<uint8>(MAX_PLAYER_LEVEL, playerLevel + _playerLevelBracket.UpperLevelOffset);
                uint32 minItemLevel = _playerLevelBracket.MinItemLevel[lowerLevel];
                uint32 maxItemLevel = _playerLevelBracket.MaxItemLevel[upperLevel];
                std::vector<uint32>& candidateItemIds = _playerLevelEligibleItemIds[playerLevel];
                candidateItemIds.reserve(_eligibleItemIds.size());

                for (uint32 itemId : _eligibleItemIds)
                {
                    ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
                    if (!itemTemplate)
                        continue;

                    bool matchesItemLevelBracket = !_playerLevelBracket.Enabled ||
                        (_playerLevelBracket.BracketEquippableOnly && !IsItemEquippable(*itemTemplate)) ||
                        (itemTemplate->ItemLevel >= minItemLevel && itemTemplate->ItemLevel <= maxItemLevel);
                    if (matchesItemLevelBracket && MatchesRequiredLevelBracket(*itemTemplate, playerLevel))
                        candidateItemIds.push_back(itemId);
                }
            }
        }

        bool AddRandomLootItems(Creature* killed, std::vector<uint32> const& itemIds, uint32 requestedCount) const
        {
            if (itemIds.empty() || requestedCount == 0 || killed->loot.items.size() >= MAX_NR_LOOT_ITEMS)
                return false;

            uint32 availableSlots = MAX_NR_LOOT_ITEMS - static_cast<uint32>(killed->loot.items.size());
            uint32 countToAdd = std::min<uint32>(requestedCount, availableSlots);
            countToAdd = std::min<uint32>(countToAdd, static_cast<uint32>(itemIds.size()));

            std::vector<uint32> indices(itemIds.size());
            for (uint32 i = 0; i < indices.size(); ++i)
                indices[i] = i;

            for (uint32 i = 0; i < countToAdd; ++i)
            {
                uint32 randomPos = urand(i, static_cast<uint32>(indices.size() - 1));
                std::swap(indices[i], indices[randomPos]);

                LootStoreItem randomLoot(itemIds[indices[i]], 0, 100.0f, false, LOOT_MODE_DEFAULT, 0, 1, 1);
                killed->loot.AddItem(randomLoot);
            }

            return countToAdd > 0;
        }

        float GetChanceForLevel(uint8 level) const
        {
            if (_chanceByLevel.empty())
                return 0.0f;

            std::map<uint8, float>::const_iterator itr = _chanceByLevel.upper_bound(level);
            if (itr == _chanceByLevel.begin())
                return 0.0f;

            --itr;
            return itr->second;
        }

        bool IsItemEquippable(ItemTemplate const& itemTemplate) const
        {
            return itemTemplate.InventoryType != 0;
        }

        uint8 GetExpansionFromRequiredLevel(ItemTemplate const& itemTemplate) const
        {
            if (itemTemplate.RequiredLevel > 70)
                return 2; // Wrath
            if (itemTemplate.RequiredLevel > 60)
                return 1; // Burning Crusade
            return 0;     // Classic
        }

        bool IsPet(ItemTemplate const& itemTemplate) const
        {
            if (itemTemplate.Class == ITEM_CLASS_MISC && itemTemplate.SubClass == ITEM_SUBCLASS_MISC_PET)
                return true;

            for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itemTemplate.Spells[i].SpellId);
                if (spellInfo && spellInfo->HasEffect(SPELL_EFFECT_SUMMON_PET))
                    return true;
            }

            return false;
        }

        bool IsMount(ItemTemplate const& itemTemplate) const
        {
            if (itemTemplate.Class == ITEM_CLASS_MISC && itemTemplate.SubClass == ITEM_SUBCLASS_MISC_MOUNT)
                return true;

            for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itemTemplate.Spells[i].SpellId);
                if (spellInfo && spellInfo->HasAura(SPELL_AURA_MOUNTED))
                    return true;
            }

            return false;
        }

        bool MatchesFilters(ItemTemplate const& itemTemplate, bool bypassLevelFilters = false) const
        {
            if (!MatchesEligibleTypes(itemTemplate))
                return false;

            if (!MatchesQualityFilter(itemTemplate))
                return false;

            if (!bypassLevelFilters)
            {
                if (_filters.RequiredLevelMin > 0 && itemTemplate.RequiredLevel < static_cast<uint32>(_filters.RequiredLevelMin))
                    return false;
                if (_filters.RequiredLevelMax > 0 && itemTemplate.RequiredLevel > static_cast<uint32>(_filters.RequiredLevelMax))
                    return false;

                if (_filters.ItemLevelMin > 0 && itemTemplate.ItemLevel < static_cast<uint32>(_filters.ItemLevelMin))
                    return false;
                if (_filters.ItemLevelMax > 0 && itemTemplate.ItemLevel > static_cast<uint32>(_filters.ItemLevelMax))
                    return false;
            }

            if (!MatchesBondingFilter(itemTemplate))
                return false;

            if (!MatchesExpansionFilter(itemTemplate))
                return false;

            return true;
        }

        bool MatchesEligibleTypes(ItemTemplate const& itemTemplate) const
        {
            if (!_eligibleTypes.FilterEnabled)
                return true;

            bool match = false;

            uint32 cls = itemTemplate.Class;
            uint32 inv = itemTemplate.InventoryType;

            // Inclusive class matching (OR semantics).
            if (_eligibleTypes.Weapons && cls == ITEM_CLASS_WEAPON)
                match = true;
            if (_eligibleTypes.Armor && cls == ITEM_CLASS_ARMOR)
                match = true;
            if (_eligibleTypes.Consumables && cls == ITEM_CLASS_CONSUMABLE)
                match = true;
            if (_eligibleTypes.Containers && cls == ITEM_CLASS_CONTAINER)
                match = true;
            if (_eligibleTypes.Gems && cls == ITEM_CLASS_GEM)
                match = true;
            if (_eligibleTypes.Reagents && cls == ITEM_CLASS_REAGENT)
                match = true;
            if (_eligibleTypes.Projectiles && cls == ITEM_CLASS_PROJECTILE)
                match = true;
            if (_eligibleTypes.TradeGoods && cls == ITEM_CLASS_TRADE_GOODS)
                match = true;
            if (_eligibleTypes.Recipes && cls == ITEM_CLASS_RECIPE)
                match = true;
            if (_eligibleTypes.Quivers && cls == ITEM_CLASS_QUIVER)
                match = true;
            if (_eligibleTypes.Quest && cls == ITEM_CLASS_QUEST)
                match = true;
            if (_eligibleTypes.Keys && cls == ITEM_CLASS_KEY)
                match = true;
            if (_eligibleTypes.Misc && cls == ITEM_CLASS_MISC)
                match = true;
            if (_eligibleTypes.Glyphs && cls == ITEM_CLASS_GLYPH)
                match = true;

            // Inclusive equipment-family and slot matching (OR semantics).
            if (_eligibleTypes.Jewelry && (inv == INVTYPE_FINGER || inv == INVTYPE_NECK || inv == INVTYPE_TRINKET))
                match = true;
            if (_eligibleTypes.Rings && inv == INVTYPE_FINGER)
                match = true;
            if (_eligibleTypes.Necklaces && inv == INVTYPE_NECK)
                match = true;
            if (_eligibleTypes.Trinkets && inv == INVTYPE_TRINKET)
                match = true;
            if (_eligibleTypes.Cloaks && inv == INVTYPE_CLOAK)
                match = true;
            if (_eligibleTypes.Shields && inv == INVTYPE_SHIELD)
                match = true;
            if (_eligibleTypes.Relics && inv == INVTYPE_RELIC)
                match = true;
            if (_eligibleTypes.Holdables && inv == INVTYPE_HOLDABLE)
                match = true;
            if (_eligibleTypes.Bags && inv == INVTYPE_BAG)
                match = true;
            if (_eligibleTypes.Tabards && inv == INVTYPE_TABARD)
                match = true;
            if (_eligibleTypes.Shirts && inv == INVTYPE_BODY)
                match = true;

            if (_eligibleTypes.Head && inv == INVTYPE_HEAD)
                match = true;
            if (_eligibleTypes.Shoulders && inv == INVTYPE_SHOULDERS)
                match = true;
            if (_eligibleTypes.Chest && (inv == INVTYPE_CHEST || inv == INVTYPE_ROBE))
                match = true;
            if (_eligibleTypes.Waist && inv == INVTYPE_WAIST)
                match = true;
            if (_eligibleTypes.Legs && inv == INVTYPE_LEGS)
                match = true;
            if (_eligibleTypes.Feet && inv == INVTYPE_FEET)
                match = true;
            if (_eligibleTypes.Wrists && inv == INVTYPE_WRISTS)
                match = true;
            if (_eligibleTypes.Hands && inv == INVTYPE_HANDS)
                match = true;

            if (_eligibleTypes.OneHandWeapons && (inv == INVTYPE_WEAPON || inv == INVTYPE_WEAPONMAINHAND || inv == INVTYPE_WEAPONOFFHAND))
                match = true;
            if (_eligibleTypes.TwoHandWeapons && inv == INVTYPE_2HWEAPON)
                match = true;
            if (_eligibleTypes.RangedWeapons && (inv == INVTYPE_RANGED || inv == INVTYPE_RANGEDRIGHT || inv == INVTYPE_THROWN))
                match = true;

            return match;
        }

        bool MatchesQualityFilter(ItemTemplate const& itemTemplate) const
        {
            if (!_filters.QualityFilterEnabled)
                return true;

            switch (itemTemplate.Quality)
            {
                case ITEM_QUALITY_POOR:      return _filters.QualityPoor;
                case ITEM_QUALITY_NORMAL:    return _filters.QualityCommon;
                case ITEM_QUALITY_UNCOMMON:  return _filters.QualityUncommon;
                case ITEM_QUALITY_RARE:      return _filters.QualityRare;
                case ITEM_QUALITY_EPIC:      return _filters.QualityEpic;
                case ITEM_QUALITY_LEGENDARY: return _filters.QualityLegendary;
                case ITEM_QUALITY_ARTIFACT:  return _filters.QualityArtifact;
                case ITEM_QUALITY_HEIRLOOM:  return _filters.QualityHeirloom;
                default:                     return false;
            }
        }

        bool MatchesExpansionFilter(ItemTemplate const& itemTemplate) const
        {
            if (!_filters.ExpansionFilterEnabled)
                return true;

            switch (GetExpansionFromRequiredLevel(itemTemplate))
            {
                case 0:  return _filters.ExpansionClassic;
                case 1:  return _filters.ExpansionTBC;
                case 2:  return _filters.ExpansionWrath;
                default: return false;
            }
        }

        bool MatchesBondingFilter(ItemTemplate const& itemTemplate) const
        {
            if (!_filters.BondingFilterEnabled)
                return true;

            switch (itemTemplate.Bonding)
            {
                case NO_BIND:             return _filters.BondingNoBind;
                case BIND_WHEN_PICKED_UP: return _filters.BondingBindOnPickup;
                case BIND_WHEN_EQUIPPED:  return _filters.BondingBindOnEquip;
                case BIND_WHEN_USE:       return _filters.BondingBindOnUse;
                case BIND_QUEST_ITEM:     return _filters.BondingQuestItem;
                case BIND_QUEST_ITEM1:    return _filters.BondingQuestItemUnused;
                default:                  return false;
            }
        }

        bool HasAnyQualitySelected() const
        {
            return
                _filters.QualityPoor || _filters.QualityCommon || _filters.QualityUncommon ||
                _filters.QualityRare || _filters.QualityEpic || _filters.QualityLegendary ||
                _filters.QualityArtifact || _filters.QualityHeirloom;
        }

        bool HasAnyExpansionSelected() const
        {
            return _filters.ExpansionClassic || _filters.ExpansionTBC || _filters.ExpansionWrath;
        }

        bool HasAnyBondingSelected() const
        {
            return
                _filters.BondingNoBind || _filters.BondingBindOnPickup || _filters.BondingBindOnEquip ||
                _filters.BondingBindOnUse || _filters.BondingQuestItem || _filters.BondingQuestItemUnused;
        }

        bool HasAnyEligibleTypeSelected() const
        {
            return
                _eligibleTypes.Weapons || _eligibleTypes.Armor || _eligibleTypes.Consumables || _eligibleTypes.Containers ||
                _eligibleTypes.Gems || _eligibleTypes.Reagents || _eligibleTypes.Projectiles || _eligibleTypes.TradeGoods ||
                _eligibleTypes.Recipes || _eligibleTypes.Quivers || _eligibleTypes.Quest || _eligibleTypes.Keys ||
                _eligibleTypes.Misc || _eligibleTypes.Glyphs ||
                _eligibleTypes.Jewelry || _eligibleTypes.Rings || _eligibleTypes.Necklaces || _eligibleTypes.Trinkets ||
                _eligibleTypes.Cloaks || _eligibleTypes.Shields || _eligibleTypes.Relics || _eligibleTypes.Holdables ||
                _eligibleTypes.Bags || _eligibleTypes.Tabards || _eligibleTypes.Shirts ||
                _eligibleTypes.Head || _eligibleTypes.Shoulders || _eligibleTypes.Chest || _eligibleTypes.Waist ||
                _eligibleTypes.Legs || _eligibleTypes.Feet || _eligibleTypes.Wrists || _eligibleTypes.Hands ||
                _eligibleTypes.OneHandWeapons || _eligibleTypes.TwoHandWeapons || _eligibleTypes.RangedWeapons;
        }

        mutable std::shared_mutex _mutex;

        bool _enabled = true;
        std::set<uint32> _excludedAccountIds;
        bool _allowRandomLootWhenGrouped = false;
        bool _hasExcludedAccountRange = false;
        uint32 _excludedAccountRangeStart = 0;
        uint32 _excludedAccountRangeEnd = 0;
        int32 _minItems = 1;
        int32 _maxItems = 1;
        EligibleItemTypes _eligibleTypes;
        RandomLootFilters _filters;
        PlayerLevelBracket _playerLevelBracket;
        RequiredLevelBracket _requiredLevelBracket;
        CompanionLootConfig _companionLoot;
        bool _hasBuiltPoolSinceConfig = false;
        bool _templatesWereEmptyOnLastBuild = true;

        std::map<uint8, float> _chanceByLevel;
        std::vector<uint32> _eligibleItemIds;
        std::vector<uint32> _petItemIds;
        std::vector<uint32> _mountItemIds;
        std::array<std::vector<uint32>, MAX_PLAYER_LEVEL + 1> _playerLevelEligibleItemIds;
    };

    RandomLootState sRandomLootState;

    class RandomLootWorldScript : public WorldScript
    {
    public:
        RandomLootWorldScript() : WorldScript("RandomLootWorldScript") { }

        void OnAfterConfigLoad(bool reload) override
        {
            sRandomLootState.LoadConfig();

            if (reload)
                sRandomLootState.RebuildItemPool();
        }

        void OnStartup() override
        {
            // Startup guarantees item templates are loaded before first kill.
            sRandomLootState.RebuildItemPool();
        }
    };

    class RandomLootPlayerScript : public PlayerScript
    {
    public:
        RandomLootPlayerScript() : PlayerScript("RandomLootPlayerScript") { }

        void OnPlayerCreatureKill(Player* killer, Creature* killed) override
        {
            sRandomLootState.TryAddLoot(killer, killed);
        }

        void OnPlayerCreatureKilledByPet(Player* petOwner, Creature* killed) override
        {
            sRandomLootState.TryAddLoot(petOwner, killed);
        }
    };
}

void AddSC_mod_lootrandomizer()
{
    new RandomLootWorldScript();
    new RandomLootPlayerScript();
}
