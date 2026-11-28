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
                // Magmus (boss_magmus.cpp). Fiery Burst lands around his victim (AT_DEST_LOC,
                // 10 yd). The hall's Ironhand Guardian statues are on their own spell list and
                // EventAI (WorldModel::bystanderAreas).
                { 9938, {
                    Row(13900, VICTIM, 5000, 5000, 6000, 6000),         // Fiery Burst
                    Row(24375, SELF, 0, 0, 8000, 8000, 51.0f),          // War Stomp, area stun
                } },
                // Doom'rel, last of the Seven (boss_tomb_of_seven.cpp). The event: the boss module.
                { 9039, {
                    Row(15245, VICTIM, 10000, 10000, 12000, 12000),     // Shadow Bolt Volley, area
                    Row(12742, TARGET_T_HOSTILE_RANDOM, 18000, 18000, 25000, 25000), // Immolate
                    Row(12493, VICTIM, 5000, 5000, 45000, 45000),       // Curse of Weakness
                    Row(13787, SELF, 16000, 16000, 300000, 300000),     // Demon Armor
                } },
                // Emperor Dagran Thaurissan (boss_emperor_dagran_thaurissan.cpp). Hand of
                // Thaurissan: the script picks a random player but casts on its victim.
                { 9019, {
                    Row(17492, VICTIM, 5000, 7500, 10000, 15000),       // Hand of Thaurissan, stun
                    Row(15636, SELF, 18000, 18000, 18000, 18000),       // Avatar of Flame
                } },
                // Ring of Law bosses (one of six at random; the event: the boss module).
                // Gorosh the Dervish (boss_gorosh_the_dervish.cpp). Whirlwind 15589 is one
                // instant hit around him, not an aura.
                { 9027, {
                    Row(15589, SELF, 12000, 12000, 15000, 15000),       // Whirlwind
                    Row(15708, VICTIM, 22000, 22000, 15000, 15000),     // Mortal Strike
                    Row(21049, SELF, 0, 0, 45000, 45000, 51.0f),        // Bloodlust
                } },
                // Grizzle (boss_grizzle.cpp).
                { 9028, {
                    Row(6524, SELF, 12000, 12000, 8000, 8000),          // Ground Tremor, area stun
                    Row(8269, SELF, 0, 0, 15000, 15000, 51.0f),         // Frenzy
                } },
                // Anub'shiah (boss_anubshiah.cpp).
                { 9031, {
                    Row(15472, VICTIM, 7000, 7000, 7000, 7000),         // Shadow Bolt
                    Row(15470, TARGET_T_HOSTILE_RANDOM, 24000, 24000, 18000, 18000), // Curse of Tongues
                    Row(12493, VICTIM, 12000, 12000, 45000, 45000),     // Curse of Weakness
                    Row(13787, SELF, 3000, 3000, 300000, 300000),       // Demon Armor
                    Row(15471, TARGET_T_HOSTILE_RANDOM, 16000, 16000, 12000, 12000), // Enveloping Web
                } },
                // Theldren's band (arena_challenge_ai.cpp, the Banner of Provocation challenge).
                { 16059, {
                    Row(17547, VICTIM, 10000, 10000, 8000, 15000),      // Mortal Strike
                    Row(19134, VICTIM, 30000, 30000, 30000, 40000),     // Intimidating Shout, area fear
                } },
                { 16055, {                                              // Va'jashni
                    Row(17138, TARGET_T_FRIENDLY_INJURED, 10000, 10000, 8000, 14000),  // Flash Heal
                    Row(20697, TARGET_T_FRIENDLY_INJURED, 20000, 20000, 40000, 50000), // Power Word: Shield
                    Row(23895, TARGET_T_FRIENDLY_INJURED, 30000, 30000, 55000, 65000), // Renew
                } },
                { 16053, {                                              // Korv
                    Row(21401, TARGET_T_HOSTILE_RANDOM, 10000, 10000, 10000, 12000),   // Frost Shock
                } },
                { 16049, {                                              // Lefty
                    Row(27673, VICTIM, 2000, 2000, 2000, 3000),         // Five Fat Finger Exploding Heart Technique
                } },
                { 16051, {                                              // Snokh Blackspine
                    Row(17273, VICTIM, 15000, 15000, 10000, 20000),     // Pyroblast
                    Row(13878, VICTIM, 4000, 4000, 3000, 5000),         // Scorch
                    Row(18399, TARGET_T_HOSTILE_RANDOM, 20000, 20000, 10000, 20000),   // Flamestrike
                    Row(13323, TARGET_T_HOSTILE_RANDOM, 30000, 30000, 25000, 30000),   // Polymorph
                } },
                { 16058, {                                              // Volida
                    Row(27618, TARGET_T_HOSTILE_RANDOM, 4000, 4000, 14000, 18000),     // Blizzard
                    Row(12557, VICTIM, 20000, 20000, 20000, 20000),     // Cone of Cold
                } },
                // Princess Moira Bronzebeard (same file): heals the Emperor when he is hurt.
                { 8929, {
                    Row(15586, TARGET_T_FRIENDLY_INJURED, 12000, 12000, 10000, 10000), // Heal
                    Row(15587, VICTIM, 16000, 16000, 14000, 14000),     // Mind Blast
                    Row(15654, VICTIM, 2000, 2000, 18000, 18000),       // Shadow Word: Pain
                    Row(10934, VICTIM, 8000, 8000, 10000, 10000),       // Smite
                } },

                // Fears of C++ bosses (boss doc G8: Fear Ward, Tremor Totem, Berserker Rage
                // before it), only those rows so far.
                // High Interrogator Gerstahn (boss_high_interrogator_gerstahn.cpp).
                { 9018, {
                    Row(13704, SELF, 32000, 32000, 30000, 30000),       // Psychic Scream, area fear
                } },
                // High Inquisitor Fairbanks (boss_high_inquisitor_fairbanks.cpp).
                { 4542, {
                    Row(12096, NOT_TOP, 40000, 40000, 40000, 40000),    // Fear
                    Row(8399, VICTIM, 30000, 30000, 30000, 30000),      // Sleep
                } },
                // Lady Illucia Barov (boss_illucia_barov.cpp).
                { 10502, {
                    Row(6215, VICTIM, 30000, 30000, 30000, 30000),      // Fear
                } },
                // Ras Frostwhisper (boss_ras_frostwhisper.cpp): cast on his victim, an area around him.
                { 10508, {
                    Row(26070, SELF, 45000, 45000, 30000, 30000),       // Fear, area
                } },
                // Captain Kromcrush (instance_dire_maul.cpp boss_kromcrush). Reavers at 50% are
                // summons; Retaliation (below 25%): the boss module stops melee on him.
                { 14325, {
                    Row(19134, SELF, 10000, 10000, 30000, 35000),       // Intimidating Shout, area fear
                    Row(22859, VICTIM, 7000, 13000, 15000, 20000),      // Mortal Cleave
                    Row(22857, SELF, 0, 0, 0, 0, 25.0f),                // Retaliation, once
                } },

                // Remaining P2 bosses (boss doc 2.2), from their scripts, read 2026-11-28.
                // Overlord Wyrmthalak (boss_overlord_wyrmthalak.cpp; its "BLASTWAVE" 11130 is a
                // Knock Away). Adds below 51%: summons, the generic modules'.
                { 9568, {
                    Row(11130, SELF, 20000, 20000, 20000, 20000),       // Knock Away, area
                    Row(23511, SELF, 2000, 2000, 10000, 10000),         // Demoralizing Shout
                    Row(20691, VICTIM, 6000, 6000, 7000, 7000),         // Cleave
                    Row(20686, SELF, 12000, 12000, 14000, 14000),       // Knock Away, area
                } },
                // King Gordok (boss_gordok_king.cpp). Cho'Rush the Observer's spells depend on the
                // gear the instance rolls (mage, shaman, priest): no sheet; the boss module.
                { 11501, {
                    Row(15572, VICTIM, 4000, 8000, 5000, 15000),        // Sunder Armor
                    Row(15708, VICTIM, 15000, 25000, 12000, 20000),     // Mortal Strike
                    Row(16727, SELF, 7000, 8000, 20000, 30000),         // War Stomp, area stun
                    Row(22886, TARGET_T_HOSTILE_RANDOM, 9000, 12000, 25000, 30000), // Berserker Charge
                } },
                // Immol'thar (boss_immol_thar.cpp). Eyes are summons; the portal (a random member
                // pulled in front of him, its threat wiped) is not a spell; Enrage at 50 s is timed.
                { 11496, {
                    Row(5568, SELF, 5000, 9000, 9000, 14000),           // Trample, area
                    Row(16128, VICTIM, 2000, 4000, 8000, 12000),        // Infected Bite
                } },
                // Prince Tortheldrin (instance_dire_maul.cpp boss_prince_tortheldrin; Counterspell
                // is on his spell list). The script resets all threat after each Arcane Blast.
                { 11486, {
                    Row(15589, SELF, 14000, 22000, 10000, 20000),       // Whirlwind (melee within 7 yd)
                    Row(22920, VICTIM, 15000, 20000, 10000, 15000, 0.0f, BOSS_ABILITY_THREAT_WIPE), // Arcane Blast
                    Row(22995, VICTIM, 0, 3000, 13000, 20000),          // Summon (pulls to him)
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
