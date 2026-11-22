#include "Life/Errands/Prowl.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/PlayerClaims.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "MotionMaster.h"
#include "GridMap.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include <algorithm>
#include <cmath>
#include <list>
#include <vector>

namespace Life
{
namespace Prowl
{
    namespace
    {
        class PreyCheck
        {
        public:
            PreyCheck(Player* me, ProwlRules const& rules, std::vector<BotShared::PlayerClaims::Claim> const& claims)
                : m_me(me), m_rules(rules), m_claims(claims) {}
            bool operator()(Creature* c)
            {
                if (!c->IsAlive() || c->IsInCombat() || c->HasLootRecipient() || !c->GetOwnerGuid().IsEmpty() ||
                    c->IsElite() || c->GetCreatureType() == CREATURE_TYPE_CRITTER)
                    return false;
                if (!m_claims.empty() && !BotShared::PlayerClaims::ClaimantOf(m_claims, c).IsEmpty())
                    return false;
                if (m_rules.preyEntry && c->GetEntry() != m_rules.preyEntry)
                    return false;
                uint32 const level = c->GetLevel();
                if (level < m_rules.minLevel || level > m_rules.maxLevel)
                    return false;
                if (c->GetDistance2d(m_rules.areaX, m_rules.areaY, SizeFactor::None) > SEARCH_YD ||
                    m_me->IsWithinDist(c, m_rules.nearYd, false))
                    return false;
                return m_me->IsValidAttackTarget(c);
            }

        private:
            Player* m_me;
            ProwlRules const& m_rules;
            std::vector<BotShared::PlayerClaims::Claim> const& m_claims;
        };

        // Where it stops: STOP_SHORT_YD before the creature, on the line from the bot.
        void ShortOf(Player const* me, Creature const* c, float& x, float& y, float& z)
        {
            float const dx = me->GetPositionX() - c->GetPositionX();
            float const dy = me->GetPositionY() - c->GetPositionY();
            float const dist = std::sqrt(dx * dx + dy * dy);
            float const k = dist > STOP_SHORT_YD ? STOP_SHORT_YD / dist : 0.0f;
            x = c->GetPositionX() + dx * k;
            y = c->GetPositionY() + dy * k;
            z = me->GetMap()->GetHeight(x, y, c->GetPositionZ() + 5.0f);
            if (z <= INVALID_HEIGHT)
                z = c->GetPositionZ();
        }

        // The nearest creature to walk to. Those without a safe walk are marked done.
        Creature* Pick(Player* me, ProwlState& state, ProwlRules const& rules)
        {
            std::list<Creature*> found;
            float const reach = SEARCH_YD + me->GetDistance2d(rules.areaX, rules.areaY, SizeFactor::None);
            std::vector<BotShared::PlayerClaims::Claim> claims;
            BotShared::PlayerClaims::Collect(me, me, reach, claims);
            PreyCheck check(me, rules, claims);
            MaNGOS::CreatureListSearcher<PreyCheck> searcher(found, check);
            Cell::VisitGridObjects(me, searcher, reach);

            std::vector<Creature*> prey;
            for (Creature* c : found)
                if (!Errands::IsDone(state.done, c->GetObjectGuid(), FORGET_MS))
                    prey.push_back(c);
            std::sort(prey.begin(), prey.end(), [me](Creature* a, Creature* b)
            {
                return me->GetDistance(a) < me->GetDistance(b);
            });

            uint32 tried = 0;
            for (Creature* c : prey)
            {
                if (++tried > CANDIDATES)
                    break;
                float x, y, z;
                ShortOf(me, c, x, y, z);
                if (Errands::WalkIsSafe(me, x, y, z, MAX_PATH_DETOUR))
                    return c;
                Errands::MarkDone(state.done, c->GetObjectGuid(), DONE_KEEP);
            }
            return nullptr;
        }

        // A random point of the area on about the bot's level.
        bool WanderPoint(Player* me, ProwlRules const& rules, float& x, float& y, float& z)
        {
            for (uint32 i = 0; i < WANDER_TRIES; ++i)
            {
                float const angle = frand(0.0f, 2.0f * float(M_PI));
                float const dist = frand(0.3f, 1.0f) * rules.areaRadius;
                x = rules.areaX + std::cos(angle) * dist;
                y = rules.areaY + std::sin(angle) * dist;
                z = me->GetMap()->GetHeight(x, y, me->GetPositionZ() + 5.0f);
                if (z > INVALID_HEIGHT && std::fabs(z - me->GetPositionZ()) <= WANDER_MAX_DZ &&
                    me->GetDistance2d(x, y, SizeFactor::None) >= 10.0f)
                    return true;
            }
            return false;
        }

        // Point movement finished or was replaced (no path, pushed away, ...).
        bool WalkEnded(Player* me)
        {
            return !me->IsMoving() && me->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE;
        }
    }

    void Stop(Player* me, ProwlState& state, bool stopWalk)
    {
        bool const walking = !state.prey.IsEmpty() || state.wandering;
        state.prey.Clear();
        state.wandering = false;
        if (walking && stopWalk)
            Errands::StopWalk(me);
    }

    bool Update(Player* me, ProwlState& state, ProwlRules const& rules)
    {
        if (!Errands::MayRun(me, nullptr) || me->HasUnitState(UNIT_STATE_NO_FREE_MOVE))
        {
            Stop(me, state, false);
            return false;
        }

        uint32 const now = WorldTimer::getMSTime();

        // On the way to a creature: until it is within the hunt's reach.
        if (!state.prey.IsEmpty())
        {
            Creature* c = me->GetMap()->GetCreature(state.prey);
            std::vector<BotShared::PlayerClaims::Claim> claims;
            if (c)
                BotShared::PlayerClaims::Collect(me, c, 0.0f, claims);
            // A player is about to fight it: his (another one next search).
            ObjectGuid const claimant = c && !c->HasLootRecipient() ? BotShared::PlayerClaims::ClaimantOf(claims, c) : ObjectGuid();
            if (!claimant.IsEmpty())
            {
                state.yieldedTo = claimant;
                state.yieldedAtMs = now ? now : 1;
                Errands::MarkDone(state.done, state.prey, DONE_KEEP);
                Stop(me, state, true);
                state.nextSearchMs = now + SEARCH_MS;
                return false;
            }
            PreyCheck check(me, rules, claims);
            bool const arrived = c && me->IsWithinDist(c, rules.nearYd * 0.8f, false);
            if (!c || arrived || WorldTimer::getMSTimeDiff(state.startedMs, now) > GIVE_UP_MS ||
                (!check(c) && !me->IsWithinDist(c, rules.nearYd, false)) || WalkEnded(me))
            {
                // Reached and still no fight: the hunt does not want it (not again for a while).
                Errands::MarkDone(state.done, state.prey, DONE_KEEP);
                Stop(me, state, !arrived);
                state.nextSearchMs = now + SEARCH_MS;
                return false;
            }
            return true;
        }

        if (state.wandering)
        {
            if (!WalkEnded(me) && WorldTimer::getMSTimeDiff(state.startedMs, now) <= GIVE_UP_MS)
            {
                // Something to walk to came up: that first.
                if (now < state.nextSearchMs)
                    return true;
            }
            else
            {
                state.wandering = false;
                state.nextWanderMs = now + urand(WANDER_MIN_MS, WANDER_MAX_MS);
            }
        }

        if (now >= state.nextSearchMs)
        {
            state.nextSearchMs = now + SEARCH_MS;
            if (Creature* c = Pick(me, state, rules))
            {
                float x, y, z;
                ShortOf(me, c, x, y, z);
                state.wandering = false;
                state.prey = c->GetObjectGuid();
                state.startedMs = now;
                me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING | MOVE_RUN_MODE | MOVE_EXCLUDE_STEEP_SLOPES);
                return true;
            }
        }
        if (state.wandering)
            return true;

        if (rules.areaRadius <= 0.0f || now < state.nextWanderMs)
            return false;
        float x, y, z;
        if (!WanderPoint(me, rules, x, y, z))
        {
            state.nextWanderMs = now + urand(WANDER_MIN_MS, WANDER_MAX_MS);
            return false;
        }
        state.wandering = true;
        state.startedMs = now;
        me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES);
        return true;
    }
}
}
