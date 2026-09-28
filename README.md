# mod-lootrandomizer

AzerothCore module that adds configurable random loot to creature kills.

## Features

- Enable/disable toggle for the whole module
- Configurable chance by creature level using `RandomLoot.Chance.Level.X`
- Configurable min/max random items to add per successful roll
- Smart item-pool filtering from `item_template` data already loaded by AzerothCore
- Optional player-level item-level brackets with a configurable symmetric character-level offset
- Separate configurable pet and mount drop pools
- Inclusive OR-style item-type filters (for example Rings + Weapons means rings OR weapons)
- Works for player kills and pet-owner kills

## Install

1. Put this folder in your AzerothCore `modules` directory:
   - `source/azerothcore-wotlk/modules/mod-lootrandomizer`
2. Re-run CMake.
3. Rebuild worldserver.
4. Copy `conf/lootrandomizer.conf.dist` to your server config folder as `lootrandomizer.conf`.
5. Restart worldserver.

## Config keys

See `conf/lootrandomizer.conf.dist`.

Important keys:

- `RandomLoot.Enable`
- `RandomLoot.Account.ExcludeIds`
- `RandomLoot.Account.ExcludeRange`
- `RandomLoot.Chance.Level.*`
- `RandomLoot.MinItems`
- `RandomLoot.MaxItems`
- `RandomLoot.Filter.*`
- `RandomLoot.Filter.PlayerLevelBracket.*`
- `RandomLoot.Companion.*`

Chance keys are configurable breakpoints. Only add the levels where the chance changes; each
intermediate creature level uses the closest lower configured key. A creature below the lowest
configured key has a 0% chance, so `RandomLoot.Chance.Level.1` is normally retained as the baseline.

`RandomLoot.Account.ExcludeRange` accepts an inclusive `start,end` account ID range. For example,
`1000,1377` excludes every account from 1000 through 1377. It can be combined with the individual
IDs in `RandomLoot.Account.ExcludeIds`.

## Notes about filtering

- Type filtering is controlled by `RandomLoot.Filter.TypeFilterEnabled` and `RandomLoot.Filter.Include.*` keys (inclusive OR).
- Quality filtering is controlled by `RandomLoot.Filter.Quality.FilterEnabled` and `RandomLoot.Filter.Quality.Include.*` keys.
- Expansion filtering is controlled by `RandomLoot.Filter.Expansion.FilterEnabled` and `RandomLoot.Filter.Expansion.Include.*` keys:
  - Classic (`<= 60`)
  - TBC (`61-70`)
  - Wrath (`71+`)
- Bonding filtering is controlled by `RandomLoot.Filter.Bonding.FilterEnabled` and `RandomLoot.Filter.Bonding.Include.*` keys.
- `EquippableOnly` uses non-zero `InventoryType`.
- `AllowItemsWithSpells` checks item spell slots in template data.
- `RandomLoot.Filter.RequireExistingLoot` controls whether random loot can appear on creatures that had no base loot. Random loot is added on top of normal loot and does not replace normal drops.

## Player-Level Bracket

`RandomLoot.Filter.PlayerLevelBracket.Enabled` is disabled by default. Each
`MinMaxItemLevel.<level>` entry uses `minimum,maximum`. When enabled, normal items selected for a
player must satisfy:

```
MinMaxItemLevel[max(1, player level - UpperLevelOffset)].minimum <= ItemLevel
  <= MinMaxItemLevel[min(80, player level + UpperLevelOffset)].maximum
```

Set `RandomLoot.Filter.PlayerLevelBracket.BracketEquippableOnly = 1` to apply this dynamic bracket
only to normal items with nonzero `InventoryType`. Normal non-equipment items then bypass the dynamic
bracket but continue to obey all static filters.

All 80 `MinMaxItemLevel.<level>` entries are explicit configuration values. Each must contain two
positive, ordered values and form a non-empty range for the configured offset. Invalid configuration
fails closed and adds no random loot.

## Companion Loot

Pets (`Class=15`, `SubClass=2`) and mounts (`Class=15`, `SubClass=5`) are excluded from the normal
pool and rolled independently per eligible kill. Each category has its own enable flag and percentage
chance; normal items continue to use the creature-level chance. One pet and one mount can be added
when corpse loot slots allow.
They bypass required-level, item-level, and player-level bracket filters, but otherwise retain the
module's configured filtering. When type filtering is enabled, `RandomLoot.Filter.Include.Misc` must
remain enabled. Other `Class=15` miscellaneous subclasses continue through the normal loot pool.

The bracket is applied when the randomizer creates loot. It leaves normal shared loot, pet-owner
kills, and mod-aoe-loot's loot transfer behavior unchanged.

## SQL

No schema changes are required for this module.
