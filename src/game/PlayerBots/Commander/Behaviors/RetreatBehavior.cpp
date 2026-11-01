#include "Commander/Behaviors/RetreatBehavior.h"
#include "Commander/Coordination/RetreatPlanner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "PartyBotAI.h"
#include "Map.h"
#include "SpellEntry.h"
#include <algorithm>
#include <cmath>
#include <list>
#include <utility>
#include <vector>

namespace Commander
{
    namespace
    {
        float Dist2(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        float Dist3(Position3 const& a, Position3 const& b)
        {
            float const dz = a.z - b.z;
            float const d2 = Dist2(a, b);
            return std::sqrt(d2 * d2 + dz * dz);
        }
    }

    bool RetreatBehavior::AppliesTo(PartyBotAI const* bot) const
    {
        return bot->GetRole() != ROLE_INVALID;
    }

    void RetreatBehavior::Propose(PartyBotAI const* bot, WorldModel const& world,
                                  TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        RetreatPlan const& r = plan.retreat;
        if (!r.active)
            return;

        Player* me = bot->me;
        Position3 here;
        here.x = me->GetPositionX();
        here.y = me->GetPositionY();
        here.z = me->GetPositionZ();

        Proposal p;
        p.tier = ProposalTier::Override;
        p.score = 1.0f;
        p.source = "retreat";
        p.reason = "retreat ordered";
        p.action.type = BotActionType::Retreat;

        Position3 dest = r.point;
        bool move = Dist3(here, r.point) > RetreatAssigner::ARRIVED_DISTANCE;
        if (!move)
        {
            // Standing still next to a chaser for 5 s extends its leash (scenario 11, 1.5,
            // "caught up"): step to the far side of the point. The chaser follows, the bot
            // crosses again, so it never stands still for long.
            for (EnemyFacts const& e : world.enemies)
            {
                if (e.victim != me->GetObjectGuid() || Dist2(e.pos, here) > CHASER_DISTANCE)
                    continue;
                float dx = r.point.x - e.pos.x;
                float dy = r.point.y - e.pos.y;
                float const len = std::sqrt(dx * dx + dy * dy);
                if (len < 0.5f)
                {
                    // Enemy on the point: away from it, along the bot's own offset.
                    dx = here.x - e.pos.x;
                    dy = here.y - e.pos.y;
                }
                float const n = std::max(0.5f, std::sqrt(dx * dx + dy * dy));
                dest.x = r.point.x + dx / n * SIDESTEP_DISTANCE;
                dest.y = r.point.y + dy / n * SIDESTEP_DISTANCE;
                float const ground = me->GetMap()->GetHeight(dest.x, dest.y, r.point.z + 5.0f);
                dest.z = ground > INVALID_HEIGHT ? ground : r.point.z;
                move = true;
                break;
            }
        }

        if (move)
        {
            float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
            float const distance = Dist3(here, dest);
            p.action.x = dest.x;
            p.action.y = dest.y;
            p.action.z = dest.z;
            p.action.holdMs = uint32(distance * PATH_FACTOR / speed * 1000.0f) + HOLD_EXTRA_MS;
        }
        // holdMs = 0: stand at the point, still not attacking.
        out.push_back(p);

        if (!move)
            ProposeHeal(bot, world, out);
    }

    // Only called at the point with no chaser close (the caller did not step aside). Picks the
    // most injured member below RETREAT_HEAL_PCT that is safe to heal and an instant heal for it.
    void RetreatBehavior::ProposeHeal(PartyBotAI const* bot, WorldModel const& world, std::vector<Proposal>& out) const
    {
        if (bot->GetRole() != ROLE_HEALER)
            return;
        Player* me = bot->me;
        ObjectGuid const self = me->GetObjectGuid();

        // Nothing may be chasing me at all, also from farther than CHASER_DISTANCE: a heal does
        // not stop the run, but a chaser that catches a target standing still extends its leash.
        for (EnemyFacts const& e : world.enemies)
            if (e.victim == self)
                return;

        // Most injured first. Pets are left out (a pet is not worth a new threat entry).
        std::vector<std::pair<float, ObjectGuid>> candidates;
        for (PartyMemberFacts const& m : world.party)
            if (!m.isPet && m.alive && !m.charmed && m.healthPct < RETREAT_HEAL_PCT)
                candidates.emplace_back(m.healthPct, m.guid);
        std::sort(candidates.begin(), candidates.end(),
            [](std::pair<float, ObjectGuid> const& a, std::pair<float, ObjectGuid> const& b) { return a.first < b.first; });

        for (auto const& c : candidates)
        {
            Unit* pTarget = me->GetMap()->GetUnit(c.second);
            if (!pTarget || !bot->IsValidHealTarget(pTarget, RETREAT_HEAL_PCT))
                continue;   // dead since the refresh, out of 30 yd or line of sight

            // Heal threat goes to every creature that has the target on its threat list, and puts
            // the healer on it. A creature in combat near the target would turn on the healer or
            // keep chasing (its leash is what the retreat is waiting for), so: no hostile unit in
            // combat within RETREAT_HEAL_ENEMY_CLEARANCE of the target. Creatures farther away
            // that still have the target on their list get the threat too; they are not chasing
            // anyone at the point (none is close), so a new entry there is accepted.
            bool enemyClose = false;
            std::list<Unit*> enemies;
            me->GetEnemyListInRadiusAround(pTarget, RETREAT_HEAL_ENEMY_CLEARANCE, enemies);
            for (Unit const* pEnemy : enemies)
            {
                if (pEnemy->IsAlive() && pEnemy->IsInCombat())
                {
                    enemyClose = true;
                    break;
                }
            }
            if (enemyClose)
                continue;

            SpellEntry const* spell = SelectInstantHeal(bot, pTarget);
            if (!spell)
                continue;

            // Same tier as the Retreat action, scored higher: tried first, and when the cast
            // fails at execution the arbiter falls back to the Retreat action in the same tick.
            // Not value-scored: the retreat itself is not valued either.
            Proposal p;
            p.tier = ProposalTier::Override;
            p.score = HEAL_SCORE;
            p.source = "retreat";
            p.reason = "heal at the retreat point";
            p.action.type = BotActionType::Cast;
            p.action.target = pTarget->GetObjectGuid();
            p.action.spell = spell;
            out.push_back(p);
            return;
        }
    }

    // Instant heals only: a cast time would stop the bot (DoCastSpell stops movement) and a
    // chaser could catch it standing still. Order: instant direct heals, HoTs (Renew,
    // Rejuvenation; Regrowth has a cast time and is skipped), then Power Word: Shield.
    // Strongest castable first within each list (the lists are sorted strongest first).
    SpellEntry const* RetreatBehavior::SelectInstantHeal(PartyBotAI const* bot, Unit const* pTarget)
    {
        Player* me = bot->me;
        for (SpellEntry const* spell : bot->m_spellListDirectHeal)
            if (spell->GetCastTime(me) == 0 && bot->CanTryToCastSpell(pTarget, spell))
                return spell;
        // One HoT is enough; another one on top mostly fails ("a more powerful spell is active").
        if (!pTarget->HasAuraType(SPELL_AURA_PERIODIC_HEAL))
            for (SpellEntry const* spell : bot->m_spellListPeriodicHeal)
                if (spell->GetCastTime(me) == 0 && bot->CanTryToCastSpell(pTarget, spell))
                    return spell;
        // Weakened Soul is not checked by CanTryToCastSpell: the cast then fails and the
        // arbiter falls back to the Retreat action.
        // (m_spells is per class: read the priest entry only for priests)
        if (me->GetClass() == CLASS_PRIEST)
            if (SpellEntry const* shield = bot->m_spells.priest.pPowerWordShield)
                if (bot->CanTryToCastSpell(pTarget, shield))
                    return shield;
        return nullptr;
    }
}
