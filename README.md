# mod-lootrandomizer

AzerothCore module that adds configurable random loot to creature kills.

## Features

- Enable/disable toggle for the whole module
- Configurable chance by creature level using `RandomLoot.Chance.Level.X`
- Configurable min/max random items to add per successful roll
- Smart item-pool filtering from `item_template` data already loaded by AzerothCore
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
- `RandomLoot.Chance.Level.*`
- `RandomLoot.MinItems`
- `RandomLoot.MaxItems`
- `RandomLoot.Filter.*`

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

## SQL

No schema changes are required for this module.
