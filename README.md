[English](README.md) | [简体中文](README_zh.md)

# EternityCore

**EternityCore** is a single-player focused fork of [vMaNGOS](https://github.com/vmangos/core) (World of Warcraft Vanilla 1.12.1), dedicated to building **human-like, intelligent AI teammates (PartyBots)** so solo players can level up, gear up, and conquer dungeons with reliable AI companions.

> [!NOTE]
> All core world simulation, progression, quests, spells, and database systems remain faithful to upstream **vMaNGOS**. EternityCore's primary focus is the **Commander AI architecture** (`src/game/PlayerBots/Commander/`) and single-player companionship features.

---

## Key Features (Implemented)

### 4-Layer Commander AI Architecture

EternityCore replaces basic scripted bot loops with a coordinated, real-time decision pipeline:

1. **Perception (Situational Awareness)**
   - Reads exact server-side `ThreatManager` values and tank safety headroom (`110%` melee / `130%` ranged).
   - Identifies enemy packs (`creature_groups` + proximity), patrol paths, and safe aggro margins.
   - Predicts pull assistance chains and low-HP fleeing aggro risks.
   - Evaluates enemy spell profiles (interruptible casts, dangerous AoE/silence zones) and party damage intake.

2. **Coordination (Team Brain)**
   - **Auto Role Assignment**: Dynamically assigns Tank, Healer, and DPS roles based on talents and gear.
   - **Focus & Target Allocation**: Prioritizes dangerous casters/healers and coordinates target switching and flee interception.
   - **Threat & Rescue**: Balances multi-target tanking threat, coordinates taunts, threat drops (Fade/Feign Death), and emergency team saves (shields, Shield Wall, Lay on Hands).
   - **Interrupt & Crowd Control Chains**: Assigns primary/backup interrupters and CC targets (Sheep, Sap, Trap, etc.) while preventing accidental CC breaks.
   - **Pull Planning & AoE Phases**: Coordinates ranged/LOS/corner pulls, ready checks, AoE burst windows (e.g., Blizzard + Frost Nova/shielding), and wipe-prevention / retreat logic (including dungeon exit routing and Soulstone/Reincarnation self-resurrection).

3. **Behavior & Arbitration (Cerebellum)**
   - Unified **Value − Cost** scoring arbiter per bot tick (`PartyBot.Commander.BotUpdateMs`, default `200 ms`).
   - **Smart Healing**: Selects optimal heal spells and ranks using actual HPS/HPM, incoming damage intake, and mana budgets, with mid-cast overheal/aggro cancellation (`CastWatch`).
   - **Tactical Positioning**: Position-field scoring for tanks, healers, melee back-arc positioning, ranged LOS/safe spots, and patrol avoidance.
   - **Cooldown & Pet Management**: Coordinated offensive/defensive cooldown usage and Hunter pet ability/threat management.

4. **Dungeon Boss Encounter Modules**
   - Specialized encounter mechanics support for classic 5-player bosses, including:
     - **Edwin VanCleef** (The Deadmines)
     - **Mekgineer Thermaplugg** (Gnomeregan — bomb & button handling)
     - **Scarlet Commander Mograine & High Inquisitor Whitemane** (Scarlet Monastery)
     - **Darkmaster Gandling** (Scholomance)
     - **Zul'Farrak Pyramid** (stair event)

---

## Roadmap & Milestones

EternityCore is being developed in progressive stages toward a complete, deeply immersive solo WoW experience:

- [x] **Stage 1: Dungeon Combat AI** *(Core complete, actively refining)* — Intelligent 5-player dungeon combat, threat/heal/CC/positioning coordination, and wipe-prevention.
- [~] **Stage 2: Open-World Party Leveling & Full 5-Man Boss Coverage** *(Basic open-world leveling works; boss modules expanding)* — Ensure all 5-player dungeon bosses and special mechanics are reliably clearable.
- [ ] **Stage 3: Player-Aware AI & Early Raids** — Make bots player-centric (adapting to player pace and providing companionship/emotional value) and support the first 3 Vanilla raids.
- [ ] **Stage 4: Autonomous Adventuring** — Bots can loot items, accept/complete quests, allocate talent points, and gather professions.
- [ ] **Stage 5: Bot Progression & Progressive Disclosure** — A companion growth system where bot features and capabilities unlock gradually as they level up.
- [ ] **Stage 6: Affinity, Synergy & Quirks** — Favorability/affinity system, combat synergy ratings, and distinct bot personalities/quirks.
- [ ] **Stage 7: LLM Integration** — Natural language interaction with bots for a truly lifelike companion experience.

---

## Quick Start

### 1. Configuration (`mangosd.conf`)

The Commander AI is enabled by default. Key settings in `mangosd.conf`:

```ini
PartyBot.Commander.Enable = 1
PartyBot.Commander.BotUpdateMs = 200
PartyBot.Commander.PerceptionMs = 200
PartyBot.Commander.DisabledModules = ""
```

### 2. In-Game GM / Player Commands

Use `.partybot ai` commands in-game to control and inspect the AI:

| Command | Description |
|---|---|
| `.partybot ai` | Show Commander status, update intervals, and module count |
| `.partybot ai on \| off` | Toggle Commander AI globally |
| `.partybot ai say [on\|off]` | Toggle bots announcing decisions in party chat |
| `.partybot ai pull [now\|cancel]` | Plan and pull the selected/nearest pack after ready check |
| `.partybot ai clear [r] [n]` | Continuously clear packs within `r` yards (max `n` mobs/pull) |
| `.partybot ai retreat [stop]` | Order the party to disengage and retreat to safety / dungeon entrance |
| `.partybot ai plan` | Inspect current team plan (focus, interrupts, pull state) |
| `.partybot ai threat` | Inspect live enemy threat tables and aggro headroom |
| `.partybot ai stats [reset]` | View AI tick CPU usage statistics |

### 3. Supported Client Builds & Building

EternityCore inherits vMaNGOS's client support (`1.12.1.5875+` through `1.5.1.4449`) and build pipeline (CMake + C++17 / MSVC / GCC / Clang). Refer to the upstream [vMaNGOS Wiki](https://github.com/vmangos/wiki) for full build and database setup instructions.

---

## License & Acknowledgments

- **License**: GNU General Public License v2.0 ([LICENSE](LICENSE)).
- **Upstream**: Built upon [vMaNGOS](https://github.com/vmangos/core) and the broader MaNGOS / Elysium / LightsHope open-source communities. See [NOTICE](NOTICE) for copyright and trademark disclaimers.
