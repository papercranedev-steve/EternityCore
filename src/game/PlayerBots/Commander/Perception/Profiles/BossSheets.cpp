#include "Commander/Perception/Profiles/BossSheets.h"
#include "ScriptMgr.h"          // ScriptCommands.h: TARGET_T_x
#include <algorithm>
#include <iterator>

namespace Commander
{
namespace BossSheets
{
    namespace
    {
        // Dungeon bosses (BossSheets.h "Boss list"), checked against creature_template 2026-11-28.
        uint32 const BOSS_LIST[] =
        {
            // Ragefire Chasm
            11517, 11518, 11519, 11520,                     // Oggleflint, Jergosh, Bazzalan, Taragaman
            // Wailing Caverns
            3653, 3654, 3669, 3670, 3671, 3673, 3674, 5775, // Kresh, Mutanus, Cobrahn, Pythas, Anacondra, Serpentis, Skum, Verdan
            5912,                                           // Deviate Faerie Dragon
            // The Deadmines
            639, 642, 644, 645, 646, 647, 1763, 3586,       // VanCleef, Sneed's Shredder, Rhahk'Zor, Cookie, Mr. Smite, Greenskin, Gilnid, Miner Johnson
            // Shadowfang Keep
            3872, 3886, 3887, 3914, 3927, 4274, 4275, 4278, 4279, // Deathsworn Captain, Razorclaw, Silverlaine, Rethilgore, Nandos, Fenrus, Arugal, Springvale, Odo
            // The Stockade
            1663, 1666, 1696, 1716, 1717, 1720,             // Dextren, Kam, Targorr, Bazil, Hamhock, Bruegal
            // Blackfathom Deeps
            4829, 4830, 4831, 4832, 4887, 6243,             // Aku'mai, Serra'kis, Sarevess, Kelris, Ghamoo-ra, Gelihast
            // Gnomeregan
            6228, 6229, 6235, 7079, 7361, 7800,             // Dark Iron Ambassador, Crowd Pummeler, Electrocutioner, Viscous Fallout, Grubbis, Thermaplugg
            // Razorfen Kraul
            4420, 4421, 4422, 4424, 4425, 4428, 4842, 6168, // Ramtusk, Charlga, Agathelos, Aggem, Blind Hunter, Jargba, Earthcaller Halmgar, Roogug
            // Scarlet Monastery
            3974, 3975, 3976, 3977, 3983, 4542, 4543, 6487, // Loksey, Herod, Mograine, Whitemane, Vishas, Fairbanks, Thalnos, Doan
            // Razorfen Downs
            7354, 7355, 7357, 7358, 8567,                   // Ragglesnout, Tuten'kash, Mordresh, Amnennar, Glutton
            // Uldaman
            2748, 4854, 6906, 6907, 6908, 6910, 7023, 7206, 7228, 7291, // Archaedas, Grimlok, Baelog, Eric, Olaf, Revelosh, Obsidian Sentinel, Stone Keeper, Ironaya, Galgann
            // Zul'Farrak
            7267, 7271, 7272, 7273, 7275, 7795, 7796, 7797, 8127, 10080, 10082, // Ukorz, Zum'rah, Theka, Gahz'rilla, Sezz'ziz, Velratha, Nekrum, Ruuzlu, Antu'sul, Sandarr, Zerillis
            // Maraudon
            12201, 12203, 12225, 12236, 12237, 12258, 13282, 13596, 13601, // Theradras, Landslide, Celebras, Vyletongue, Meshlok, Razorlash, Noxxion, Rotgrip, Gizlock
            // The Temple of Atal'Hakkar
            5709, 5710, 5711, 5712, 5713, 5714, 5715, 5716, 5717, // Eranikus, Jammal'an, Ogom, the six Atal'ai defenders
            5719, 5720, 5721, 5722, 8443, 8580,             // Morphaz, Weaver, Dreamscythe, Hazzas, Avatar of Hakkar, Atal'alarion
            // Blackrock Depths
            8923, 8929, 8983, 9016, 9017, 9018, 9019, 9024, 9025, // Panzor, Moira, Argelmach, Bael'Gar, Incendius, Gerstahn, Thaurissan, Loregrain, Roccor
            9027, 9028, 9029, 9030, 9031, 9032, 16059,      // Ring of Law: Gorosh, Grizzle, Eviscerator, Ok'thor, Anub'shiah, Hedrum, Theldren
            9033, 9034, 9035, 9036, 9037, 9038, 9039, 9040, // Angerforge, the Seven
            9041, 9056, 9156, 9319, 9499, 9502, 9537, 9543, 9938, // Stilgiss, Fineous, Flamelash, Grebmar, Plugger, Phalanx, Hurley, Ribbly, Magmus
            // Lower Blackrock Spire
            9196, 9217, 9218, 9219, 9236, 9237, 9568, 9596, 9718, 9736, // Omokk, Spirestone trio, Vosh'gajin, Voone, Wyrmthalak, Bannok, Ghok, Zigris
            10220, 10263, 10268, 10376, 10584, 10596,       // Halycon, Burning Felguard, Gizrul, Crystal Fang, Urok, Smolderweb
            // Upper Blackrock Spire (the world boss rank ones need no line)
            10264, 10339, 10509, 10899,                     // Solakar, Gyth, Jed, Goraluk
            // Scholomance
            1853, 10432, 10433, 10502, 10503, 10504, 10505, 10506, 10507, 10508, // Gandling, Vectus, Marduk, Illucia, Jandice, Alexei, Malicia, Kirtonos, Ravenian, Ras
            10901, 11261, 11622, 14695, 16118,              // Polkelt, Theolen, Rattlegore, Lord Blackwood, Kormok
            // Stratholme
            10393, 10435, 10436, 10437, 10438, 10439, 10440, 10558, 10808, 10809, // Skul, Barthilas, Anastari, Nerub'enkan, Maleki, Ramstein, Rivendare, Forresten, Timmy, Stonespine
            10811, 10812, 10813, 10997, 11143, 14684, 16101, 16102, // Galford, Dathrohan, Balnazzar, Willey, Malown, Balzaphon, Jarien, Sothos
            // Dire Maul
            11467, 11486, 11487, 11488, 11489, 11490, 11491, 11492, 11496, 11501, // Tsu'zee, Tortheldrin, Kalendris, Illyanna, Tendris, Zevrim, Ironbark, Alzzin, Immol'thar, Gordok
            13280, 14321, 14322, 14323, 14324, 14325, 14326, 14327, 14354, 14690, 16097, // Hydrospawn, the guards, Kreeg, Cho'Rush, Kromcrush, Lethtendris, Pusillin, Revanchion, Isalien
            // Zul'Gurub: High Priest Thekal's zealots (the instance data rule had them)
            11347, 11348,
        };

        // Shorthands for the sheets below.
        constexpr uint8 SELF = TARGET_T_OWNER_OR_SELF;
        constexpr uint8 VICTIM = TARGET_T_HOSTILE;
        constexpr uint8 NOT_TOP = TARGET_T_HOSTILE_RANDOM_NOT_TOP;
        constexpr uint8 SECOND = TARGET_T_HOSTILE_SECOND_AGGRO;

        BossAbility Row(uint32 spellId, uint8 castTarget, uint32 initialMinMs, uint32 initialMaxMs,
                        uint32 repeatMinMs, uint32 repeatMaxMs, float belowPct = 0.0f, uint8 flags = 0)
        {
            BossAbility a;
            a.spellId = spellId;
            a.castTarget = castTarget;
            a.initialMinMs = initialMinMs;
            a.initialMaxMs = initialMaxMs;
            a.repeatMinMs = repeatMinMs;
            a.repeatMaxMs = repeatMaxMs;
            a.belowPct = belowPct;
            a.flags = flags;
            return a;
        }

        // Ability sheets of C++ scripted bosses (BossSheets.h "Ability sheets"), from the scripts
        // in src/scripts, read 2026-11-28.
        std::vector<BossSheet> MakeSheets()
        {
            return
            {
                // Mekgineer Thermaplugg (boss_thermaplugg.cpp). Knock Away wipes the victim's
                // threat (SpellTags SCRIPTED_THREAT_WIPES); from 50% the area one instead (not
                // gated above 50%: the sheet has no upper line). Bombs: the boss module.
                { 7800, {
                    Row(10101, VICTIM, 17000, 20000, 17000, 20000),
                    Row(11130, SELF, 0, 0, 12000, 12000, 50.0f),
                } },
                // Baroness Anastari (boss_baroness_anastari.cpp). Possess: the boss module.
                { 10436, {
                    Row(16565, VICTIM, 1000, 1000, 4000, 4000),         // Banshee Wail
                    Row(16867, SELF, 11000, 11000, 18000, 18000),       // Banshee Curse, area
                    Row(18327, SELF, 13000, 13000, 13000, 18000),       // Silence, area
                } },
                // Arcanist Doan (boss_arcanist_doan.cpp): at 50% Arcane Bubble, then Detonation
                // (5 s cast, 30 yd) once.
                { 6487, {
                    Row(9433, SELF, 3000, 3000, 8000, 8000),            // Arcane Explosion
                    Row(8988, SELF, 15000, 15000, 15000, 20000),        // Silence, area
                    Row(13323, NOT_TOP, 20000, 20000, 20000, 20000),    // Polymorph
                    Row(9435, SELF, 0, 0, 0, 0, 50.0f, BOSS_ABILITY_EVERYONE_LEAVES), // Detonation
                } },
                // Herod (boss_herod.cpp). Whirlwind is a periodic aura on him (Pulse zone while up).
                { 3975, {
                    Row(8989, SELF, 10000, 20000, 20000, 30000),        // Whirlwind
                    Row(15496, VICTIM, 12000, 12000, 12000, 12000),     // Cleave
                    Row(8269, SELF, 0, 0, 0, 0, 50.0f),                 // Frenzy
                } },
                // Archaedas (boss_archaedas.cpp). The awakenings are summons, not on the sheet;
                // Awaken Vault Warder (10258, 5 s) cannot be interrupted. Ground Tremor is a
                // lockout for the healers (IncomingTimeline::Lockout): the last heal before it
                // carries the tank through it. It is instant: its timer runs from the stun seen
                // on us (WorldModelBuilder NoteInstantCasts).
                { 2748, {
                    Row(6524, SELF, 60000, 60000, 45000, 45000),        // Ground Tremor, area stun
                } },
                // His minions (mob_archaedas_minions). Earthen Custodian: heals him below 50%
                // (only that the sheet cannot say): a healer, the kill order's.
                { 7309, {
                    Row(10260, TARGET_T_FRIENDLY_INJURED, 4000, 10000, 10000, 10000),    // Reconstruct
                } },
                // Vault Warder (woken at 33%, elite).
                { 10120, {
                    Row(5568, SELF, 4000, 10000, 10000, 10000),         // Trample, area
                } },
                // Grand Crusader Dathrohan (boss_dathrohan_balnazzar.cpp); below 40% he becomes
                // Balnazzar (10813, same script).
                { 10812, {
                    Row(17286, SELF, 8000, 8000, 12000, 12000),         // Crusader's Hammer, area stun
                    Row(17281, VICTIM, 12000, 12000, 15000, 15000),     // Crusader Strike
                    Row(17284, VICTIM, 18000, 18000, 15000, 15000),     // Holy Strike
                    Row(17287, VICTIM, 6000, 6000, 15000, 20000),       // Mind Blast
                } },
                // Balnazzar: the timers run from the transform (not readable), counted from the fight start.
                { 10813, {
                    Row(17287, VICTIM, 6000, 6000, 15000, 20000),       // Mind Blast
                    Row(17399, SELF, 3000, 3000, 11000, 11000),         // Shadow Shock, area
                    Row(13704, SELF, 12000, 12000, 20000, 20000),       // Psychic Scream, area fear
                    Row(12098, NOT_TOP, 9000, 9000, 15000, 15000),      // Sleep
                    Row(17405, SECOND, 18000, 18000, 25000, 30000),     // Domination (charm, not dispellable)
                } },
                // General Angerforge (boss_general_angerforge.cpp). The adds below 30%: the boss module.
                { 9033, {
                    Row(15572, VICTIM, 5000, 10000, 5000, 15000),       // Sunder Armor
                } },
            };
        }

        std::vector<BossSheet> const& Sheets()
        {
            static std::vector<BossSheet> const sheets = MakeSheets();
            return sheets;
        }
    }

    bool IsListedBoss(uint32 entry)
    {
        return std::find(std::begin(BOSS_LIST), std::end(BOSS_LIST), entry) != std::end(BOSS_LIST);
    }

    BossSheet const* Find(uint32 entry)
    {
        for (BossSheet const& s : Sheets())
            if (s.entry == entry)
                return &s;
        return nullptr;
    }
}
}
