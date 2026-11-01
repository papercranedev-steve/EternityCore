/*
 * Zul'Farrak pyramid module (header comment: match, phases, plan, proposals, drinking).
 *
 * Known limits:
 *   - Before the first wave add fights anyone (cages open, prisoners walking, wave 1 still at
 *     the bottom) the module is not active: Matches has no live objects and the phase is only
 *     read while active. So there is no "drink to full before opening the cages" hint, and the
 *     first group may arrive while bots still sit (they stand up once a prisoner or a member
 *     fights: allies and enemies are refreshed every perception tick).
 *   - The PRE_WAVE_2 countdown starts at the first refresh that sees the phase, up to one
 *     perception interval late; the run time up the stairs (WAVE_TRAVEL_SEC) is an estimate
 *     (scenario 9, 1.2), not read from the adds' movement. No arrival prediction for single
 *     groups inside a wave: waves 1-2 simply allow no drinking at all.
 *   - Hold point: only melee bots whose own target flees are held. A bot sent down the stairs
 *     by something else (following the leader, a taunt) is not stopped; the hold point does not
 *     move to the bottom for wave 3 (no holding there). Fleeing is UNIT_STATE_FLEEING or the
 *     15% threshold, whichever comes first; Zealots (no flee) at low health are also let go
 *     once they are outside the radius.
 *   - The Weegli heal picks the smallest castable direct heal (least threat), not the one
 *     that fits his missing health; no HoTs, no shields, no heals for the other prisoners, and
 *     no mana budget (the scenario's Jev question "spend mana on the prisoners?" is not asked).
 *   - Focus tiers follow scenario 9, 2.1 as fixed rules; the values (dps, kill time) are
 *     ignored between tiers. Enemies that cannot be focused (fresh, intercepted) are skipped,
 *     so for NEW_ENEMY_SETTLE_SEC after a group arrives FocusAssigner's choice stays.
 *   - Not done: area damage timing, mana budget over the remaining waves, wave 3 positioning
 *     (Psychic Scream range), crew fight after the event (prisoners turn hostile: that is a
 *     normal fight, the module is no longer active once the phase is KILLED_ALL_TROLLS).
 */

#include "Commander/Encounters/ZulFarrakPyramid.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "Commander/Coordination/FocusAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/CommanderMgr.h"
#include "PartyBotAI.h"
#include "InstanceData.h"
#include "Map.h"
#include "Player.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>

namespace Commander
{
    Position3 const ZulFarrakPyramid::HOLD_POINT = { 1886.0f, 1268.0f, 41.5f };

    namespace
    {
        char const* const SOURCE = "zf-pyramid";

        // Focus tiers (scenario 9, 2.1), lower first.
        enum FocusTier : uint32
        {
            TIER_ON_SOFT_MEMBER = 0,    // hitting a healer bot or a cloth wearer
            TIER_ON_WEEGLI      = 1,
            TIER_ACOLYTE        = 2,
            TIER_CRETIN         = 3,
            TIER_OTHER          = 4,
            TIER_ON_BLY         = 5,
        };

        char const* TierReason(uint32 tier)
        {
            switch (tier)
            {
                case TIER_ON_SOFT_MEMBER: return "zf pyramid: hitting a healer / cloth";
                case TIER_ON_WEEGLI:      return "zf pyramid: hitting Weegli";
                case TIER_ACOLYTE:        return "zf pyramid: Acolyte (mana burn)";
                case TIER_CRETIN:         return "zf pyramid: Cretin";
                case TIER_ON_BLY:         return "zf pyramid: on Bly (last)";
                default:                  return "zf pyramid: wave add";
            }
        }

        bool IsCloth(uint8 classId)
        {
            return classId == CLASS_MAGE || classId == CLASS_PRIEST || classId == CLASS_WARLOCK;
        }

        uint32 TierOf(WorldModel const& world, EnemyFacts const& e)
        {
            if (PartyMemberFacts const* v = world.FindMember(e.victim))
                if (!v->isPet && ((v->isBot && v->role == ROLE_HEALER) || IsCloth(v->classId)))
                    return TIER_ON_SOFT_MEMBER;
            AllyFacts const* ally = world.FindAlly(e.victim);
            if (ally && ally->entry == ZulFarrakPyramid::NPC_WEEGLI)
                return TIER_ON_WEEGLI;
            if (ally && ally->entry == ZulFarrakPyramid::NPC_BLY)
                return TIER_ON_BLY;
            if (e.entry == ZulFarrakPyramid::NPC_ACOLYTE)
                return TIER_ACOLYTE;
            if (e.entry == ZulFarrakPyramid::NPC_CRETIN)
                return TIER_CRETIN;
            return TIER_OTHER;
        }

        // Base points of the direct heal effects (no +healing; the rotation uses HealSpells.h).
        int32 BasePoints(SpellEntry const* spell)
        {
            int32 points = 0;
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (spell->Effect[i] == SPELL_EFFECT_HEAL)
                    points += spell->EffectBasePoints[i];
            return points;
        }
    }

    bool ZulFarrakPyramid::IsWaveEntry(uint32 entry)
    {
        switch (entry)
        {
            case NPC_SLAVE:
            case NPC_DRUDGE:
            case NPC_CRETIN:
            case NPC_ACOLYTE:
            case NPC_ZEALOT:
            case NPC_SEZZZIZ:
            case NPC_NEKRUM:
                return true;
            default:
                return false;
        }
    }

    bool ZulFarrakPyramid::IsPrisonerEntry(uint32 entry)
    {
        return entry >= NPC_BLY && entry <= NPC_MURTA;
    }

    bool ZulFarrakPyramid::IsWavePhase(Phase phase)
    {
        return phase >= Phase::Wave1 && phase <= Phase::Wave3;
    }

    bool ZulFarrakPyramid::HoldsTheStairs() const
    {
        return m_phase == Phase::Wave1 || m_phase == Phase::PreWave2 || m_phase == Phase::Wave2 ||
               m_phase == Phase::Unknown;
    }

    bool ZulFarrakPyramid::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        if (IsWavePhase(m_phase))
            return true;
        for (EnemyFacts const& e : world.enemies)
            if (e.summoned && IsWaveEntry(e.entry))
                return true;
        return false;
    }

    void ZulFarrakPyramid::OnReset()
    {
        *this = ZulFarrakPyramid();
    }

    void ZulFarrakPyramid::Refresh(PartyBotAI* bot, WorldModel const& world)
    {
        uint32 const now = world.refreshedAtMs;

        Phase phase = Phase::Unknown;
        if (bot && bot->me->IsInWorld())
            if (InstanceData* data = bot->me->GetMap()->GetInstanceData())
                phase = Phase(data->GetData(DATA_PYRAMID));
        if (phase != m_phase || !m_phaseSinceMs)
        {
            m_phase = phase;
            m_phaseSinceMs = now;
        }

        // Anyone fighting: an enemy of ours or of an ally, an ally in combat (allies are only
        // listed while in combat), or a party member in combat.
        bool fighting = !world.enemies.empty() || !world.allies.empty();
        for (PartyMemberFacts const& m : world.party)
            fighting = fighting || (!m.isPet && m.alive && m.inCombat);
        if (fighting || !m_lastFightMs)
            m_lastFightMs = now;
        uint32 const quietMs = WorldTimer::getMSTimeDiff(m_lastFightMs, now);

        switch (m_phase)
        {
            case Phase::Wave1:      m_wavesLeft = 3; break;
            case Phase::PreWave2:
            case Phase::Wave2:      m_wavesLeft = 2; break;
            case Phase::PreWave3:
            case Phase::Wave3:      m_wavesLeft = 1; break;
            default:                m_wavesLeft = 0; break;
        }
        m_inGap = m_phase == Phase::PreWave2 || m_phase == Phase::PreWave3;

        switch (m_phase)
        {
            case Phase::Wave1:
            case Phase::Wave2:
                // Groups keep coming every 10 s: no sitting down between two of them.
                m_noRest = quietMs < QUIET_STALE_MS;
                break;
            case Phase::PreWave2:
            {
                uint32 const elapsed = WorldTimer::getMSTimeDiff(m_phaseSinceMs, now);
                float const spawnSec = elapsed < PRE_WAVE_2_MS ? (PRE_WAVE_2_MS - elapsed) / 1000.0f : 0.0f;
                m_noRest = fighting || spawnSec + WAVE_TRAVEL_SEC < NO_REST_BEFORE_CONTACT_SEC;
                // The gap should last 10 s; if this script version keeps the phase longer and
                // nothing comes, let the party drink again (same escape as during the waves).
                if (!fighting && quietMs >= QUIET_STALE_MS)
                    m_noRest = false;
                break;
            }
            case Phase::PreWave3:
            case Phase::Wave3:
            case Phase::Unknown:
                // Wave 3 waits at the bottom until the prisoners walk into it: drink until then.
                m_noRest = fighting;
                break;
            default:
                m_noRest = false;
                break;
        }

        if ((m_phase == Phase::PreWave3 || m_phase == Phase::Wave3) && !m_wave3Noted)
        {
            m_wave3Noted = true;
            m_pendingNotes.push_back("Pyramid: wave 3 (two elite bosses) waits at the bottom of the stairs; "
                                     "the prisoners walk down 5 s after it spawns. Go down with them, the bots follow you. "
                                     "Healers drink until the fight starts.");
        }
    }

    void ZulFarrakPyramid::AddFacts(WorldModel& world)
    {
        // Only Bly holds enemies. Weegli has the health for it but must not tank (he opens the
        // door after the event), so damage dealers may take his enemies off him.
        for (AllyFacts& a : world.allies)
            if (IsPrisonerEntry(a.entry))
                a.tankLike = a.entry == NPC_BLY;
    }

    void ZulFarrakPyramid::AdjustPlan(WorldModel const& world, TeamPlan& plan)
    {
        for (std::string& note : m_pendingNotes)
            plan.notes.push_back(std::move(note));
        m_pendingNotes.clear();

        if (!HoldsTheStairs())
            return;     // wave 3: FocusAssigner (the Shadowpriest's heals are scored there)

        PartyMemberFacts const* tank = EncounterHelpers::FindTank(world);
        auto intercepted = [&plan](ObjectGuid guid)
        {
            return std::any_of(plan.intercepts.begin(), plan.intercepts.end(),
                [guid](InterceptDuty const& d) { return d.enemy == guid; });
        };

        EnemyFacts const* best = nullptr;
        uint32 bestTier = 0;
        for (EnemyFacts const& e : world.enemies)
        {
            // Adds on the prisoners count here (tier 1: on Weegli, tier 5: on Bly).
            if (!FocusAssigner::IsEligible(world, e, true) || intercepted(e.guid))
                continue;
            uint32 const tier = TierOf(world, e);
            if (!best || tier < bestTier)
            {
                best = &e;
                bestTier = tier;
                continue;
            }
            if (tier > bestTier)
                continue;
            // Same tier: keep the current focus, then (tier 4) the tank's enemies, then the
            // one closest to dying.
            if (best->guid == plan.focus.enemy)
                continue;
            if (e.guid == plan.focus.enemy)
            {
                best = &e;
                continue;
            }
            if (tier == TIER_OTHER && tank)
            {
                bool const eOnTank = e.victim == tank->guid;
                bool const bestOnTank = best->victim == tank->guid;
                if (eOnTank != bestOnTank)
                {
                    if (eOnTank)
                        best = &e;
                    continue;
                }
            }
            if (e.health < best->health)
                best = &e;
        }

        if (best)
            EncounterHelpers::SetFocus(world, plan, *best, TierReason(bestTier));
    }

    void ZulFarrakPyramid::Propose(PartyBotAI const* bot, WorldModel const& world,
                                   TeamPlan const& /*plan*/, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        PartyMemberFacts const* myFacts = world.FindMember(me->GetObjectGuid());
        if (!myFacts || !myFacts->CanAct())
            return;
        CombatBotRoles const role = bot->GetRole();
        float const tickSec = sCommanderMgr.GetBotUpdateInterval() / 1000.0f;

        // Melee and tank: do not chase a fleeing add down the stairs.
        if ((role == ROLE_MELEE_DPS || role == ROLE_TANK) && HoldsTheStairs())
        {
            Unit const* pVictim = me->GetVictim();
            EnemyFacts const* e = pVictim ? world.FindEnemy(pVictim->GetObjectGuid()) : nullptr;
            if (e && (e->healthPct < FLEE_PCT || pVictim->HasUnitState(UNIT_STATE_FLEEING)) &&
                PackResolver::Distance(e->pos, HOLD_POINT) > HOLD_RADIUS)
            {
                // Something to hit up here: switch to it (the rotation goes on this tick). A
                // MoveTo every tick would keep the bot on the fleeing add as its victim and
                // idle until the add dies (review of step 12).
                EnemyFacts const* nearest = nullptr;
                float nearestDistance = 0.0f;
                for (EnemyFacts const& other : world.enemies)
                {
                    if (other.guid == e->guid || !other.acting || other.crowdControlled || !other.health ||
                        other.healthPct < FLEE_PCT || PackResolver::Distance(other.pos, HOLD_POINT) > HOLD_RADIUS)
                        continue;
                    float const d = PackResolver::Distance(myFacts->pos, other.pos);
                    if (!nearest || d < nearestDistance)
                    {
                        nearest = &other;
                        nearestDistance = d;
                    }
                }
                if (nearest)
                {
                    Proposal p = EncounterHelpers::MakeReplace(SOURCE, ProposalTier::Assigned, BotActionType::Attack, nearest->guid);
                    p.reason = "fleeing add below the hold point: hit one up here";
                    p.SetValue(HOLD_POINTS, Value::LostAction(world, *myFacts, tickSec));
                    out.push_back(p);
                    return;
                }
                Position3 const dest = PackResolver::Distance(myFacts->pos, HOLD_POINT) > HOLD_ARRIVE_YARDS
                    ? HOLD_POINT : myFacts->pos;
                Proposal p = EncounterHelpers::MakeReplace(SOURCE, ProposalTier::Assigned, BotActionType::MoveTo);
                p.action.x = dest.x;
                p.action.y = dest.y;
                p.action.z = dest.z;
                p.action.holdMs = HOLD_MOVE_MS;
                p.reason = "fleeing add below the hold point: stay, ranged finish it";
                p.SetValue(HOLD_POINTS, Value::LostAction(world, *myFacts, tickSec));
                out.push_back(p);
            }
        }

        // Healer: keep Weegli alive once the party members are safe enough.
        if (role == ROLE_HEALER)
        {
            AllyFacts const* weegli = nullptr;
            for (AllyFacts const& a : world.allies)
                if (a.entry == NPC_WEEGLI)
                    weegli = &a;
            if (!weegli || weegli->healthPct >= WEEGLI_HEAL_PCT)
                return;
            for (PartyMemberFacts const& m : world.party)
                if (!m.isPet && m.alive && m.healthPct < MEMBERS_FIRST_PCT)
                    return;     // the rotation heals them first

            Unit* unit = me->GetMap()->GetUnit(weegli->guid);
            if (!unit || !bot->IsValidHealTarget(unit, WEEGLI_HEAL_PCT))
                return;

            // m_spellListDirectHeal is ordered strongest first: walk it from the weakest.
            for (auto itr = bot->m_spellListDirectHeal.rbegin(); itr != bot->m_spellListDirectHeal.rend(); ++itr)
            {
                SpellEntry const* spell = *itr;
                if (!bot->CanTryToCastSpell(unit, spell))
                    continue;
                float const missing = float(weegli->maxHealth > weegli->health ? weegli->maxHealth - weegli->health : 0);
                float const healed = std::min(missing, float(std::max(0, BasePoints(spell))));
                float const castSec = std::max(Value::GCD_SEC, float(spell->GetCastTime(me)) / 1000.0f);
                Proposal p = EncounterHelpers::MakeReplace(SOURCE, ProposalTier::Assigned, BotActionType::Cast,
                                                           weegli->guid, spell);
                p.reason = "heal Weegli (must survive the event)";
                p.SetValue(WEEGLI_WEIGHT * Value::Points(world, healed), Value::LostAction(world, *myFacts, castSec));
                out.push_back(p);
                return;
            }
        }
    }
}
