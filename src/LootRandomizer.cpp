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

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
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

        bool EquippableOnly = false;
        bool AllowQuestItems = false;
        bool AllowContainers = false;
        bool AllowItemsWithSpells = true;
        bool AllowBindOnPickup = true;
        bool RequireExistingLoot = false;
    };

    class RandomLootState
    {
    public:
        void LoadConfig()
        {
            std::unique_lock lock(_mutex);

            _enabled = sConfigMgr->GetOption<bool>("RandomLoot.Enable", true);

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

            _filters.EquippableOnly = sConfigMgr->GetOption<bool>("RandomLoot.Filter.EquippableOnly", false, false);
            _filters.AllowQuestItems = sConfigMgr->GetOption<bool>("RandomLoot.Filter.AllowQuestItems", false, false);
            _filters.AllowContainers = sConfigMgr->GetOption<bool>("RandomLoot.Filter.AllowContainers", false, false);
            _filters.AllowItemsWithSpells = sConfigMgr->GetOption<bool>("RandomLoot.Filter.AllowItemsWithSpells", true, false);
            _filters.AllowBindOnPickup = sConfigMgr->GetOption<bool>("RandomLoot.Filter.AllowBindOnPickup", true, false);
            _filters.RequireExistingLoot = sConfigMgr->GetOption<bool>("RandomLoot.Filter.RequireExistingLoot", false, false);

            // Account exclusions
            _excludedAccountIds.clear();
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

            _maxAccountId = static_cast<uint32>(std::max<int32>(0, sConfigMgr->GetOption<int32>("RandomLoot.Account.MaxId", 0, false)));

            _chanceByLevel.clear();

            // Defaults match your requested baseline, and can be overridden by explicit level keys.
            SetChance(1, 1.0f);
            SetChance(10, 2.0f);
            SetChance(20, 3.0f);
            SetChance(30, 4.0f);
            SetChance(40, 5.0f);
            SetChance(50, 6.5f);
            SetChance(60, 8.0f);
            SetChance(70, 10.0f);
            SetChance(80, 15.0f);

            for (uint8 level = 1; level <= 80; ++level)
            {
                float value = sConfigMgr->GetOption<float>("RandomLoot.Chance.Level." + std::to_string(level), -1.0f, false);
                if (value >= 0.0f)
                    _chanceByLevel[level] = value;
            }

            if (_chanceByLevel.empty())
                _chanceByLevel[1] = 0.0f;
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

            for (ItemTemplateContainer::const_iterator itr = items->begin(); itr != items->end(); ++itr)
            {
                ItemTemplate const& itemTemplate = itr->second;
                if (MatchesFilters(itemTemplate))
                    _eligibleItemIds.push_back(itemTemplate.ItemId);
            }

            LOG_INFO("module.RandomLoot", "mod-lootrandomizer built item pool: {} eligible items", _eligibleItemIds.size());

            if (_eligibleItemIds.empty())
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
                    "mod-lootrandomizer eligible pool is empty. item_template rows={}, typeMatches={}, equippable={}, qualityMatches={}. Current filters: TypeFilterEnabled={}, QualityFilterEnabled={}, ExpansionFilterEnabled={}, BondingFilterEnabled={}, EquippableOnly={}, AllowQuestItems={}, AllowContainers={}, AllowItemsWithSpells={}, AllowBindOnPickup={}",
                    items->size(), typeMatchCount, equippableCount, qualityMatchCount,
                    _eligibleTypes.FilterEnabled, _filters.QualityFilterEnabled, _filters.ExpansionFilterEnabled, _filters.BondingFilterEnabled, _filters.EquippableOnly,
                    _filters.AllowQuestItems, _filters.AllowContainers, _filters.AllowItemsWithSpells, _filters.AllowBindOnPickup);
            }
        }

        void TryAddLoot(Player* killer, Creature* killed)
        {
            if (!killer || !killed)
                return;

            std::unique_lock lock(_mutex);

            if (!_enabled)
                return;

            uint32 accountId = killer->GetSession()->GetAccountId();
            if (!_excludedAccountIds.empty() && _excludedAccountIds.count(accountId))
                return;
            if (_maxAccountId > 0 && accountId > _maxAccountId)
                return;

            // Retry pool build only when needed: either never built since config load, or templates were empty on last build.
            if (_eligibleItemIds.empty() && (!_hasBuiltPoolSinceConfig || _templatesWereEmptyOnLastBuild))
                RebuildItemPoolLocked();

            if (_eligibleItemIds.empty())
                return;

            bool hadAnyLootBeforeRandom = !killed->loot.isLooted();

            if (_filters.RequireExistingLoot && !hadAnyLootBeforeRandom)
                return;

            uint8 creatureLevel = killed->GetLevel();
            float chance = GetChanceForLevel(creatureLevel);
            if (chance <= 0.0f || !roll_chance_f(chance))
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

            uint32 minItems = static_cast<uint32>(_minItems);
            uint32 maxItems = static_cast<uint32>(_maxItems);

            if (maxItems < minItems)
                std::swap(maxItems, minItems);

            uint32 countToAdd = (minItems == maxItems) ? minItems : urand(minItems, maxItems);
            if (countToAdd == 0)
                return;

            bool addedAnyRandomLoot = false;

            uint32 availableSlots = MAX_NR_LOOT_ITEMS - static_cast<uint32>(killed->loot.items.size());
            countToAdd = std::min<uint32>(countToAdd, availableSlots);
            countToAdd = std::min<uint32>(countToAdd, static_cast<uint32>(_eligibleItemIds.size()));

            if (countToAdd == 0)
                return;

            std::vector<uint32> indices(_eligibleItemIds.size());
            for (uint32 i = 0; i < indices.size(); ++i)
                indices[i] = i;

            for (uint32 i = 0; i < countToAdd; ++i)
            {
                uint32 randomPos = urand(i, static_cast<uint32>(indices.size() - 1));
                std::swap(indices[i], indices[randomPos]);

                uint32 itemId = _eligibleItemIds[indices[i]];
                LootStoreItem randomLoot(itemId, 0, 100.0f, false, LOOT_MODE_DEFAULT, 0, 1, 1);
                killed->loot.AddItem(randomLoot);
                addedAnyRandomLoot = true;
            }

            // If base loot was empty, core may have already removed the lootable flag.
            // Ensure the corpse becomes lootable when random loot is added.
            if (addedAnyRandomLoot && !hadAnyLootBeforeRandom)
            {
                killed->SetDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
            }
        }

    private:
        void SetChance(uint8 level, float defaultValue)
        {
            float configured = sConfigMgr->GetOption<float>("RandomLoot.Chance.Level." + std::to_string(level), defaultValue, false);
            _chanceByLevel[level] = std::max(0.0f, configured);
        }

        float GetChanceForLevel(uint8 level) const
        {
            if (_chanceByLevel.empty())
                return 0.0f;

            std::map<uint8, float>::const_iterator itr = _chanceByLevel.upper_bound(level);
            if (itr == _chanceByLevel.begin())
                return itr->second;

            --itr;
            return itr->second;
        }

        bool IsItemEquippable(ItemTemplate const& itemTemplate) const
        {
            return itemTemplate.InventoryType != 0;
        }

        bool HasSpellData(ItemTemplate const& itemTemplate) const
        {
            for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                if (itemTemplate.Spells[i].SpellId != 0)
                    return true;
            }

            return false;
        }

        uint8 GetExpansionFromRequiredLevel(ItemTemplate const& itemTemplate) const
        {
            if (itemTemplate.RequiredLevel > 70)
                return 2; // Wrath
            if (itemTemplate.RequiredLevel > 60)
                return 1; // Burning Crusade
            return 0;     // Classic
        }

        bool MatchesFilters(ItemTemplate const& itemTemplate) const
        {
            if (!MatchesEligibleTypes(itemTemplate))
                return false;

            if (!MatchesQualityFilter(itemTemplate))
                return false;

            if (_filters.RequiredLevelMin > 0 && itemTemplate.RequiredLevel < static_cast<uint32>(_filters.RequiredLevelMin))
                return false;
            if (_filters.RequiredLevelMax > 0 && itemTemplate.RequiredLevel > static_cast<uint32>(_filters.RequiredLevelMax))
                return false;

            if (_filters.ItemLevelMin > 0 && itemTemplate.ItemLevel < static_cast<uint32>(_filters.ItemLevelMin))
                return false;
            if (_filters.ItemLevelMax > 0 && itemTemplate.ItemLevel > static_cast<uint32>(_filters.ItemLevelMax))
                return false;

            if (!MatchesBondingFilter(itemTemplate))
                return false;

            if (!MatchesExpansionFilter(itemTemplate))
                return false;

            if (_filters.EquippableOnly && !IsItemEquippable(itemTemplate))
                return false;

            if (!_filters.AllowQuestItems && itemTemplate.Class == ITEM_CLASS_QUEST)
                return false;

            if (!_filters.AllowContainers && itemTemplate.Class == ITEM_CLASS_CONTAINER)
                return false;

            if (!_filters.AllowItemsWithSpells && HasSpellData(itemTemplate))
                return false;

            if (!_filters.AllowBindOnPickup && itemTemplate.Bonding == BIND_WHEN_PICKED_UP)
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
        uint32 _maxAccountId = 0;
        int32 _minItems = 1;
        int32 _maxItems = 1;
        EligibleItemTypes _eligibleTypes;
        RandomLootFilters _filters;
        bool _hasBuiltPoolSinceConfig = false;
        bool _templatesWereEmptyOnLastBuild = true;

        std::map<uint8, float> _chanceByLevel;
        std::vector<uint32> _eligibleItemIds;
    };

    RandomLootState sRandomLootState;

    class RandomLootWorldScript : public WorldScript
    {
    public:
        RandomLootWorldScript() : WorldScript("RandomLootWorldScript") { }

        void OnAfterConfigLoad(bool /*reload*/) override
        {
            sRandomLootState.LoadConfig();
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
