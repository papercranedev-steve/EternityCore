# Contributing to EternityCore

Thank you for your interest in contributing to **EternityCore**!

## Scope

- **AI & Single-Player Features**: Issues, discussions, and pull requests related to the Commander AI (`src/game/PlayerBots/Commander/`), PartyBot behavior, dungeon boss AI, and solo-play improvements are very welcome.
- **Core Vanilla Mechanics**: For general 1.12.1 world/spell/quest emulation bugs unrelated to AI bots, please check whether the issue also exists in upstream [vMaNGOS](https://github.com/vmangos/core).

## Reporting AI Bugs

When reporting an AI behavior issue (e.g., wipe, bad pull, aggro loss, healing gap, or stuck movement), please include:

1. **Scenario**: Dungeon/zone name, mob/boss name, and party composition (classes, specs, levels).
2. **Expected vs. Actual Behavior**: What a human player would do vs. what the bots did.
3. **Diagnostic Output** (if available):
   - Output from `.partybot ai plan`, `.partybot ai threat`, or `.partybot ai bot`
   - Any relevant combat log / `.partybot ai say on` party chat notes

## Submitting Pull Requests

1. Keep changes focused and well-scoped; follow the existing code style (`.clang-format` / `.editorconfig`).
2. Avoid modifying unrelated core vMaNGOS systems unless required by an AI feature.
3. All contributions are licensed under the **GNU General Public License v2.0** ([LICENSE](LICENSE)).
