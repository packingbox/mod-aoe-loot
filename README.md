# AOE Loot Module for AzerothCore

![AzerothCore](https://img.shields.io/badge/azerothcore-mod-blue.svg)

## Description

This module enhances the looting experience in AzerothCore by implementing Area-of-Effect (AOE) looting functionality. It allows players to loot multiple corpses at once within a defined radius, significantly improving quality of life for players.

The module has been completely rebuilt from the ground up to provide better performance, stability, and integration with the latest AzerothCore versions.

This module does NOT condense, consolidate, or delete unlooted items from corpses. There is built in safety measures to guarantee loot will not ever be lost. The module will automatically default to the preexisting in-game looting logic if only 1 lootable corpse is within the distance set in the config file.

If you wish to demo the AoE Looting feature, you can try it out on my AzerothCore private server project. The Best WoW Private Server: https://thebestwowprivateserver.com/ 

## Features

- Automatically iterate through all nearby corpses and loot all corpses that are assigned to the looting player with a single interaction.
- Configurable loot radius.
- Compatible with other loot-related modules and core functionality (That I know of. This module runs the same logic as the in-game looting logic minus the distance limitations and a few other very niche cases).
- Minimal performance impact
- Customizable messages and notifications

## Requirements

- AzerothCore v3.0.0+
- CMake 3.13+ (compile-time only)

## Installation

### Source Code

You can clone the module directly into your AzerothCore modules directory:

```bash
cd path/to/azerothcore/modules
git clone https://github.com/TerraByte-tbwps/mod-aoe-loot.git
```

## Configuration

You can find the configuration file in the module's `conf` directory or folder. The configuration options can be set in `mod_aoe_loot.conf` or make a copy of the `mod_aoe_loot.conf.dist` and remove the `.dist` at the end of the file name.

Key terms for none coders:
A "directory" is the same thing as a "folder" if your using a Microsoft Windows computer/server.

## Usage

Once installed and enabled, the module works automatically. When a player loots a corpse, all eligible corpses within the configured radius will also be looted.

### Commands

| Command                     | Description                                   | Access Level |
|-----------------------------|-----------------------------------------------|--------------|
| `.aoeloot on`               | Enable AOE looting for your character         | Player       |
| `.aoeloot off`              | Disable AOE looting for your character        | Player       |

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork it
2. Create your feature branch (`git checkout -b feature/yourfeature`)
3. Commit your changes (`git commit -am 'Add some feature'`)
4. Push to the branch (`git push origin feature/yourfeature`)
5. Create a new Pull Request

## Credits

* [TerraByte-tbwps](https://github.com/TerraByte-tbwps) - Author and maintainer
* Original AzerothCore community and contributors
* This module was inspired by sudlud's https://github.com/azerothcore/mod-aoe-loot

## License

This module is free for the AzerothCore community to use and modify as needed.

## Links

* [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk)
* [Module Documentation](https://www.azerothcore.org/catalogue.html#terrabytetbwps-mod-aoe-loot)
