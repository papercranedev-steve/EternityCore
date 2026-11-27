/*
 * PartyBot Commander boss module: Edwin VanCleef, The Deadmines (combat scenario 6, section 3.3).
 *
 * Match: map 36 and VanCleef (639) or one of his summoned Defias Blackguards (636, summoned)
 * among the enemies. The summons outlive him (guardians, Pet.cpp), so the module stays
 * active, and OnReset waits, until they are dead too.
 *
 * Phases (from world.enemies on each refresh):
 *   Opening         - an original (not summoned) Blackguard is alive.
 *   PushLine        - originals dead, boss alive, no summon yet.
 *   Reinforcements  - boss alive and at least one summoned Blackguard alive.
 *   BossDead        - boss gone, summons still alive.
 * The 50% summon (creature_ai_events 63904, spell 5200: two 0-threat elite guardians next to
 * the boss) happens once per fight; m_summonAtMs remembers when the first summon was seen.
 *
 * Doctrine (ShapeDoctrine): the hold. While 50% < boss <= 53%, no summon yet, and (an original
 *   guard alive || tank < 80% || lowest healer mana < 40%), capped at MAX_HOLD_MS, the boss gets
 *   STAMP_DAMAGE_LIMIT: stop at HOLD_BAND_PCT, no hit past SUMMON_PCT, his tank keeps hitting.
 *   Nobody else attacks him (TargetAssigner: the focus and targets go to the guards; a bot on
 *   him drops him, TeamBotAI::StopHandsOffVictims), no cast takes him past the line, no DoTs
 *   (Constraints::HandsOff).
 *
 * Proposals:
 *   Tank, Assigned, area threat (Thunder Clap / Consecration / Swipe, Demoralizing as a
 *     fallback) within GROUP_THREAT_WINDOW_MS of the summon while a summon is within
 *     ThreatBehavior::GROUP_THREAT_RADIUS. ThreatBehavior proposes area threat as well; this
 *     one is valued on the known 0-threat summons so it wins inside the window.
 *   Healer, veto healing the tank within HEAL_VETO_WINDOW_MS of the summon while the tank
 *     is above HEAL_VETO_MIN_TANK_PCT: the first heal's threat is split over both 0-threat
 *     summons and pulls them (scenario 6, 1.3).
 *
 * Known limits: see the .cpp file comment at the top.
 */

#ifndef MANGOS_COMMANDER_ENCOUNTER_DEADMINES_VANCLEEF_H
#define MANGOS_COMMANDER_ENCOUNTER_DEADMINES_VANCLEEF_H

#include "Commander/Encounters/IEncounter.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "ObjectGuid.h"
#include <string>

namespace Commander
{
    class DeadminesVanCleef : public IEncounter
    {
    public:
        static constexpr uint32 MAP_ID = 36;
        static constexpr uint32 NPC_VANCLEEF = 639;
        static constexpr uint32 NPC_BLACKGUARD = 636;

        // Summon threshold (creature_ai_events 63904, param1 = 50) and the hold band above it.
        static constexpr float SUMMON_PCT = 50.0f;
        static constexpr float HOLD_BAND_PCT = 53.0f;
        // Ready to push: tank health and lowest healer mana at least this.
        static constexpr float TANK_READY_PCT = 80.0f;
        static constexpr float HEALER_READY_MANA_PCT = 40.0f;
        // Give up holding after this long (healer mana barely regenerates in combat; the boss
        // keeps hitting the tank while we wait, and the player may push anyway).
        static constexpr uint32 MAX_HOLD_MS = 15000;

        static constexpr uint32 SUMMON_COUNT = 2;               // spell 5200: base 1 + die 1
        // Blackguard melee as a share of VanCleef's when no Blackguard was seen yet
        // (scenario 6 table: ~27 vs ~43 dps).
        static constexpr float GUARD_DPS_SHARE = 0.6f;
        // How long loose summons hit the healer before taunt + threat bring them back.
        static constexpr float LOOSE_SEC = 4.0f;

        static constexpr uint32 GROUP_THREAT_WINDOW_MS = 3000;
        // Share of the summon loss one area threat cast prevents (it gives threat on both, but
        // one cast does not make the lead safe on its own).
        static constexpr float GROUP_THREAT_SHARE = 0.5f;
        static constexpr float FALLBACK_THREAT_SHARE = 0.25f;   // Demoralizing Shout / Roar

        static constexpr uint32 HEAL_VETO_WINDOW_MS = 1500;
        static constexpr float HEAL_VETO_MIN_TANK_PCT = 50.0f;

        enum class Phase : uint8
        {
            None,
            Opening,
            PushLine,
            Reinforcements,
            BossDead,
        };

        char const* GetId() const override { return "vancleef"; }
        bool Matches(WorldModel const& world, uint32 mapId) const override;
        void Refresh(TeamBotAI* bot, WorldModel const& world) override;
        void ShapeDoctrine(WorldModel const& world, TeamPlan& plan) override;
        void Propose(TeamBotAI const* bot, WorldModel const& world,
                     TeamPlan const& plan, std::vector<Proposal>& out) const override;
        void OnReset() override;

        Phase GetPhase() const { return m_phase; }

    private:
        void ProposeTank(TeamBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
        void ProposeHealer(TeamBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const;
        // Points lost when `guards` Blackguards hit the healer for LOOSE_SEC.
        float SummonLoss(WorldModel const& world, EnemyFacts const* boss, uint32 guards) const;
        uint32 MsSinceSummon(uint32 nowMs) const;

        Phase m_phase = Phase::None;
        ObjectGuid m_boss;
        bool m_bossAlive = false;
        float m_bossPct = 100.0f;
        uint32 m_originalsAlive = 0;
        std::vector<ObjectGuid> m_originals;    // alive original guards, this refresh
        std::vector<ObjectGuid> m_summons;      // alive summoned guards, this refresh
        uint32 m_summonAtMs = 0;                // 0 = not summoned yet this fight

        ObjectGuid m_tank;                      // boss victim if one of us, else the tank bot
        ObjectGuid m_healer;                    // healer bot with the least mana
        float m_tankPct = 100.0f;
        float m_healerManaPct = 100.0f;         // 100 when there is no healer bot

        EnemyFacts m_guardModel;                // last seen Blackguard (threat list dropped)
        bool m_haveGuardModel = false;

        bool m_holdDamage = false;
        bool m_holdGivenUp = false;             // MAX_HOLD_MS reached: no more holds this fight
        uint32 m_holdSinceMs = 0;
        float m_summonLoss = 0.0f;              // points, the two summons alone
    };
}

#endif
