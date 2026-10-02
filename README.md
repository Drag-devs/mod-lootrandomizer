# mod-lootrandomizer

AzerothCore module that adds configurable random loot to creature kills.

## Features

- Configurable random loot chance by creature level using `RandomLoot.Chance.Level.X`
- Configurable min/max random items to add per successful roll
- Smart item-pool filtering from `item_template` with many filterable categories and item types
- Optional player-level item-level brackets with a configurable symmetric character-level offset to keep random loot within a specified range appropriate for a player level
- Separate configurable pet and mount drop pools
- Works for player kills and pet-owner kills
- Exclude individual or a range of account IDs to, for example, exclude playerbot accounts.
- Pets and mounts are excluded from the normal pool and rolled independently with their own toggle and configurable chance. 

## Install

1. Put this folder in your AzerothCore `modules` directory:
   - `source/modules/mod-lootrandomizer`
2. Re-run CMake.
4. Copy `conf/lootrandomizer.conf.dist` to your server config folder as `lootrandomizer.conf`, configure settings to your liking.
5. Restart worldserver.

## Configuration

See `conf/lootrandomizer.conf.dist` for every available setting and default value.

### Module And Account Controls

- Enable or disable the module
- Exclude individual accounts or an account ID range
- Allow excluded accounts to generate random loot while grouped with real players (in the case of grouping with playerbots)

### Chance And Item Count

- Set the random-loot chance by creature-level breakpoint
- Set the minimum and maximum number of normal random items added on a successful roll

`RandomLoot.Chance.Level.*` keys are configurable breakpoints. Only add the levels where the chance changes; each
intermediate creature level uses the closest lower configured key. A creature below the lowest
configured key has a 0% chance, so `RandomLoot.Chance.Level.1` is normally retained as the baseline.

### Account Exclusions

`RandomLoot.Account.ExcludeRange` accepts an inclusive `start,end` account ID range. For example,
`1000,1377` excludes every account from 1000 through 1377. It can be combined with the individual
IDs in `RandomLoot.Account.ExcludeIds`.

Set `RandomLoot.Account.AllowRandomLootWhenGrouped = 1` when excluded playerbots only group with
real players. This allows a playerbot killing blow to generate random loot for that group while
excluded solo playerbots remain blocked.

### Item Pool, Brackets, And Companions

- Filter normal loot by item category, equipment family, quality, level, expansion, and binding
- Apply an optional player-level item-level bracket to normal equipment
- Configure independent pet and mount companion pools and their chances

## Notes about filtering

Optionally filter the loot pool by the following item categories and equipment families. When type filtering is enabled, an item is included if it matches at least one selected option. Other enabled filters further narrow the pool.

## Available Item Filters

### Item Categories

- Weapons
- Armor
- Consumables
- Containers
- Gems
- Reagents
- Projectiles and ammunition
- Trade goods
- Recipes
- Quivers and ammo pouches
- Quest items
- Keys
- Miscellaneous items
- Glyphs

### Equipment Families And Slots

- Jewelry: rings, necklaces, and trinkets
- Individual jewelry types: rings, necklaces, or trinkets
- Cloaks, shields, relics, and held-in-off-hand items
- Bags, tabards, and shirts
- Helmets, shoulders, chest pieces and robes, belts, legs, boots, wrists, and gloves
- One-hand, main-hand, and off-hand weapons
- Two-hand weapons
- Ranged weapons, including bows, guns, wands, and thrown weapons

### Quality, Level, And Expansion

- Item quality: poor, common, uncommon, rare, epic, legendary, artifact, and heirloom
- Static required-level and item-level minimum or maximum ranges
- Player-level item-level bracket for normal equipment
- Expansion bracket derived from required level: Classic (level 60 and below), TBC (61-70), or Wrath
  (71 and above)

### Binding And Pool Rules

- Binding type: no bind, bind on pickup, bind on equip, bind on use, or either quest-item binding
- Require existing loot: only add random loot to creatures that already had base loot or money
- Companion pets and mounts use separate pools and their own independent chances

Random loot is additive: it is appended and never replaces normal loot.

## Player-Level Bracket

`RandomLoot.Filter.PlayerLevelBracket.Enabled` is disabled by default. Each
`MinMaxItemLevel.<level>` entry uses `minimum,maximum`. When enabled, normal items selected for a
player use a level window. The module takes the minimum ilvl from the lower end of the window and the maximum ilvl from the upper end.

For example, with `RandomLoot.Filter.PlayerLevelBracket.UpperLevelOffset = 5`, a level 14 player can receive items within the minimum ilvl from `RandomLoot.Filter.PlayerLevelBracket.MinMaxItemLevel.9` and the
maximum ilvl from `RandomLoot.Filter.PlayerLevelBracket.MinMaxItemLevel.19`. In that scenario, with the default mappings, eligible equipment can be between ilvl 9 through 29.

Set `RandomLoot.Filter.PlayerLevelBracket.BracketEquippableOnly = 1` to apply this dynamic bracket
only to equippable items. Normal non-equipment items then bypass the dynamic
bracket but continue to obey all other filters. 

## SQL

No SQL changes are required for this module.

## License

GNU General Public License v2 or later, matching AzerothCore. Full text in [LICENSE](LICENSE).
