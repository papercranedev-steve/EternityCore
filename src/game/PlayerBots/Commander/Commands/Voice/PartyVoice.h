/*
 * PartyBot Commander: the party voice, what bots say to the players in party chat during a run
 * (user 2026-11-22). Natural lines, not debug data (that is BotSpeech.h, off for players).
 *
 *  - Who says it: whoever it concerns (the healer says he is out of mana, the tank says he
 *    pulls, the controller says what he sheeps). Never the player. Empty speaker: the main tank
 *    bot for fight lines, a random bot (chatty ones first) for social and atmosphere lines.
 *  - How often: one budget for the whole party, not per bot. One line per refresh at most;
 *    in a fight GAP_FIGHT_MS between lines, out of it GAP_MS, Urgent lines need URGENT_GAP_MS
 *    only, social and atmosphere lines FLAVOR_GAP_MS and no fight. A line waits up to its
 *    tier's expiry for its turn, then is dropped (late lines mislead). Each line is said at most
 *    once per its `onceMs` for its `once` key (an enemy, a member, a pull).
 *  - What is said: the human's talkativeness (VoiceSettings: quiet = Urgent and Essential only)
 *    and the speaker's personality (Companions/Personality.cpp: "quiet" ones keep atmosphere to
 *    themselves, "chatty" ones talk more). Text: PlayerBots/i18n "voice.*" keys through
 *    Shared/Phrases (lines by personality tag), in the party's language.
 *  - Only with a human in the team (a team of bots has nobody to tell).
 *
 * Moments (VoiceMoments.cpp) are found by comparing this refresh's facts and plan with the last
 * one's, the party's quest progress likewise (QuestMoments.cpp); brain modules, rotations and
 * the bot's life can also ask for a line: TeamPlan::voice (boss modules) or Queue
 * (GatherPlanner, the warlock's Soulstone, a quest handed in through PartyBotAI). The crowd
 * control marks (ControlMarks.h) run here too.
 *
 * Map thread of the party (PartyCommander::Update, after the planners).
 */

#ifndef MANGOS_COMMANDER_PARTY_VOICE_H
#define MANGOS_COMMANDER_PARTY_VOICE_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Commands/Voice/ControlMarks.h"
#include "Commander/Commands/Voice/VoiceSettings.h"
#include <map>
#include <set>
#include <string>
#include <vector>

class Map;
class Player;
class TeamBotAI;

namespace Commander
{
    struct WorldModel;

    class PartyVoice
    {
    public:
        static constexpr uint32 GAP_FIGHT_MS = 6000;
        static constexpr uint32 GAP_MS = 3000;
        static constexpr uint32 URGENT_GAP_MS = 2000;
        static constexpr uint32 FLAVOR_GAP_MS = 8000;
        static constexpr size_t MAX_PENDING = 8;
        // Out of a fight this long with nothing said: a chatty party may talk a little.
        static constexpr uint32 IDLE_CHATTY_MS = 4 * MINUTE * IN_MILLISECONDS;
        static constexpr uint32 IDLE_NORMAL_MS = 10 * MINUTE * IN_MILLISECONDS;
        // Health a member fell below this fight to have had a close call ("好险"; a bot healed
        // back from there by a human thanks him, PlayerMoments.cpp).
        static constexpr float CLOSE_CALL_PCT = 15.0f;
        // Praise for a human (PlayerMoments.cpp): at most one line per fight, this long apart.
        static constexpr uint32 PRAISE_GAP_MS = 5 * MINUTE * IN_MILLISECONDS;
        // A human standing in an area on the ground (or a pulse) this long: told to step out.
        static constexpr uint32 HAZARD_SAY_MS = 1500;
        // A human's buff on a bot lasting this long counts (not a short heal over time).
        static constexpr uint32 BUFF_MIN_MS = 10 * MINUTE * IN_MILLISECONDS;
        // A round of buffs on several bots is gathered this long into one line.
        static constexpr uint32 BUFF_SETTLE_MS = 3000;
        // A dead human back on his feet within this of a bot: "back again".
        static constexpr float BACK_YD = 40.0f;

        // Ask for a line (map thread of the party). Dropped when said within its onceMs.
        void Queue(VoiceEvent event, uint32 nowMs);
        // A member bot saw `caster` interrupt `target`'s cast of `spellId` (TeamBotAI::
        // OnInterruptSeen; every bot in sight reports it, kept once). PlayerMoments.cpp reads it.
        void NoteInterrupt(ObjectGuid caster, ObjectGuid target, uint32 spellId, uint32 nowMs);

        // PartyCommander::Update, after the planners: this refresh's moments and TeamPlan::voice,
        // the marks, then at most one line.
        void Step(TeamBotAI* bot, WorldModel const& world, TeamPlan& plan, uint32 nowMs);

    private:
        struct Pending
        {
            VoiceEvent event;
            uint32 queuedMs = 0;
        };

        // What the last refresh saw, to find this refresh's changes.
        struct MemberState
        {
            bool alive = true;
            uint32 level = 0;
            float lowestPct = 100.0f;   // health this fight
        };
        struct EnemyState
        {
            uint32 controlSpell = 0;
            ObjectGuid controlCaster;
            uint32 controlLeftMs = 0;
            bool breaksOnDamage = false;
            bool fleeing = false;
            bool boss = false;
            ObjectGuid victim;
        };

        // VoiceMoments.cpp
        void FindMoments(Map* map, WorldModel const& world, TeamPlan const& plan, Player const* human,
                         VoiceSettings const& settings, uint32 nowMs);
        void PullMoments(Map* map, WorldModel const& world, TeamPlan const& plan, Player const* human, uint32 nowMs);
        void FightMoments(Map* map, WorldModel const& world, TeamPlan const& plan, Player const* human, uint32 nowMs);
        void MemberMoments(Map* map, WorldModel const& world, bool fighting, bool fightEnded, uint32 nowMs);
        void QuietMoments(WorldModel const& world, TeamPlan const& plan, VoiceSettings const& settings, bool fighting,
                          uint32 nowMs);
        // An event the player paces (WavePace::Player, boss doc 2.3): bots drinking between waves
        // say so, and once they are done one says the party is ready for the next.
        void WaveMoments(WorldModel const& world, bool fighting, uint32 nowMs);
        // QuestMoments.cpp: the party's quest progress (who is done, who still needs what).
        void QuestMoments(Map* map, WorldModel const& world, Player const* human, bool fighting, uint32 nowMs);
        void QuestNews(Map* map, WorldModel const& world, Player const* human, uint32 questId, uint32 nowMs);
        // PlayerMoments.cpp: the human players themselves (doc 2026-11-28-player-voice.md): a
        // human down and raised, standing in fire, an enemy on him, his buffs on bots, praise
        // after a fight. Before MemberMoments (it may take the boss kill line).
        void PlayerMoments(Map* map, WorldModel const& world, TeamPlan const& plan, bool fighting, bool fightEnded,
                           uint32 nowMs);
        void HumanDeaths(Map* map, WorldModel const& world, TeamPlan const& plan, bool fighting, bool fightEnded,
                         uint32 nowMs);
        void HumanInFight(Map* map, WorldModel const& world, TeamPlan const& plan, uint32 nowMs);
        void Praise(Map* map, WorldModel const& world, uint32 nowMs);
        void HumanBuffs(Map* map, WorldModel const& world, bool fighting, uint32 nowMs);
        void Remember(WorldModel const& world, TeamPlan const& plan);

        // PartyVoice.cpp
        void Say(std::string key, ObjectGuid speaker, VoiceTier tier, uint32 nowMs,
                 std::vector<std::pair<std::string, std::string>> vars = {}, std::string once = {},
                 uint32 onceMs = 60 * 1000);
        void Deliver(Map* map, WorldModel const& world, VoiceSettings const& settings, bool fighting, uint32 nowMs);
        Player* PickSpeaker(Map* map, WorldModel const& world, Pending const& line) const;
        // The enemy's name for the party's human, its mark first ("月亮 迪菲亚劫掠者").
        std::string EnemyName(Map* map, ObjectGuid enemy, Player const* human) const;
        std::string MemberName(Map* map, ObjectGuid member) const;
        // The control spell's word ("变羊"), the English spell name without one.
        std::string ControlWord(Player const* human, uint32 spellId) const;

        std::vector<Pending> m_pending;
        std::map<std::string, uint32> m_said;     // once key -> when it was queued
        uint32 m_lastLineMs = 0;
        uint32 m_lastFightEndMs = 0;

        bool m_seen = false;            // a refresh was remembered (the first one finds nothing)
        uint32 m_firstMs = 0;           // of the first refresh with a human (on this map)
        bool m_fighting = false;
        uint32 m_fightStartMs = 0;
        PullPhase m_pullPhase = PullPhase::None;
        uint32 m_pullControlSince = 0;
        PullLosBreak m_losBreak = PullLosBreak::None;
        ObjectGuid m_patrolWait;
        bool m_waitChecked = false;     // this pull's "waiting for" was looked at
        // WaveMoments: gaps counted here (a fight ended, the event modules do not number the
        // player-paced waves), whether a bot drank in this one, "ready" said in it.
        uint32 m_waveGap = 0;
        bool m_waveFought = false;
        bool m_waveDrank = false;
        bool m_waveReadySaid = false;
        bool m_retreat = false;
        bool m_retreatAdvice = false;
        bool m_wipe = false;
        uint8 m_crisisLost = 0;
        size_t m_patrolsAnnounced = 0;
        bool m_bossDied = false;        // this fight
        bool m_entered = false;         // "here we are" said on this map
        std::vector<ObjectGuid> m_pendingLevelUps;  // humans who levelled in a fight: said after it
        std::map<uint64, MemberState> m_members;
        std::map<uint64, EnemyState> m_enemies;

        // QuestMoments.cpp: each member's quests at the last look, and the quests that changed
        // since (said once the changes settled, out of a fight).
        struct QuestSnap
        {
            bool complete = false;
            uint32 have = 0;            // kills, objects and items counted so far
        };
        std::map<uint64, std::map<uint32, QuestSnap>> m_questSnaps;
        std::map<uint32, uint32> m_questChanged;    // quest -> first change since it was said
        uint32 m_nextQuestLookMs = 0;

        // PlayerMoments.cpp
        std::map<uint64, uint32> m_humansDown;      // dead humans -> when they died ("back" once up near us)
        std::map<std::string, uint32> m_hazardSince; // "human:enemy:spell" -> since when he stands in it
        // This fight: somebody died; bots a human's heal reached below CLOSE_CALL_PCT (bot ->
        // human); a human's control on an enemy that hit a non-tank or ran (who, who thanks).
        bool m_deathThisFight = false;
        std::map<uint64, ObjectGuid> m_savedBy;
        ObjectGuid m_controlPraiseBy;
        ObjectGuid m_controlPraiseSpeaker;
        // ... a human's interrupt of a cast that mattered (an enemy the brain had an interrupt
        // duty for at the last refresh, or a heal).
        ObjectGuid m_interruptPraiseBy;
        struct SeenInterrupt
        {
            ObjectGuid caster;
            ObjectGuid target;
            uint32 spellId = 0;
            uint32 atMs = 0;
        };
        std::vector<SeenInterrupt> m_interruptsSeen;    // since the last refresh
        std::set<uint64> m_interruptNeeds;              // enemies with an interrupt duty, last refresh
        uint32 m_lastPraiseMs = 0;
        // Long buffs cast by a human on a bot at the last look (bot raw guid, spell); new ones
        // since gathered BUFF_SETTLE_MS.
        std::set<std::pair<uint64, uint32>> m_humanBuffs;
        bool m_humanBuffsSeen = false;
        uint32 m_nextBuffLookMs = 0;
        uint32 m_buffNewsMs = 0;                    // 0 = none
        ObjectGuid m_buffBy;
        std::vector<ObjectGuid> m_buffedBots;

        ControlMarks m_marks;
    };
}

#endif
