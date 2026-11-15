#include "Commander/Coordination/Situation/Los/CasterLureAssigner.h"
#include "Commander/Coordination/Situation/Los/LosBreak.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Profiles/ProfileStore.h"
#include "Commander/Perception/Profiles/CreatureProfile.h"
#include "Timer.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
    namespace
    {
        std::string NameOf(uint32 entry)
        {
            std::shared_ptr<CreatureProfile const> profile = sProfileStore.GetCreature(entry);
            return profile ? profile->name : std::string("The caster");
        }

        bool Contains(std::vector<ObjectGuid> const& list, ObjectGuid guid)
        {
            return std::find(list.begin(), list.end(), guid) != list.end();
        }

        bool Going(LosLure const& l)
        {
            return l.state == LosLureState::Search || l.state == LosLureState::Active;
        }

        // Any lure this fight (going or ended) had `caster`: one try each.
        bool Tried(CasterLurePlan const& m, ObjectGuid caster)
        {
            return std::any_of(m.lures.begin(), m.lures.end(),
                [caster](LosLure const& l) { return Contains(l.casters, caster); });
        }

        LosLure* GoingLureOf(CasterLurePlan& m, ObjectGuid hider)
        {
            for (LosLure& l : m.lures)
                if (l.hider == hider && Going(l))
                    return &l;
            return nullptr;
        }

        // The caster no longer needs the lure: gone, controlled, turned to someone else, or
        // came close to the hider.
        bool CasterCame(WorldModel const& world, LosLure const& l, ObjectGuid caster)
        {
            EnemyFacts const* e = world.FindEnemy(caster);
            if (!e || !e->acting || !e->health || e->crowdControlled || e->victim != l.hider)
                return true;
            PartyMemberFacts const* hider = world.FindMember(l.hider);
            return !hider || PackResolver::Distance(e->pos, hider->pos) <= CasterLureAssigner::ARRIVED;
        }

        void EndGoing(CasterLurePlan& m)
        {
            for (LosLure& l : m.lures)
                if (Going(l))
                    l.state = LosLureState::Done;
        }
    }

    bool CasterLureAssigner::StandsBack(WorldModel const& world, EnemyFacts const& e)
    {
        if (e.onAllyOnly || !e.acting || e.crowdControlled || !e.health)
            return false;
        PartyMemberFacts const* victim = world.FindMember(e.victim);
        if (!victim || PackResolver::Distance(e.pos, victim->pos) <= MIN_DISTANCE)
            return false;
        return !e.meleeEnabled || (LosBreak::IsRangedCaster(e.entry) && !e.moving);
    }

    void CasterLureAssigner::Clear(TeamPlan& plan) const
    {
        plan.casterLure = CasterLurePlan();
    }

    void CasterLureAssigner::Assign(WorldModel const& world, TeamPlan& plan) const
    {
        CasterLurePlan& m = plan.casterLure;
        if (!world.HasPartyEnemy())
        {
            m = CasterLurePlan();       // new fight, new tries
            return;
        }
        // The pull (its own break) and the retreat own the movement.
        if (plan.retreat.active || plan.pull.phase != PullPhase::None)
        {
            EndGoing(m);
            return;
        }

        uint32 const now = world.refreshedAtMs;
        for (LosLure& l : m.lures)
        {
            PartyMemberFacts const* hider = world.FindMember(l.hider);
            if (l.state == LosLureState::Search && (!hider || !hider->CanAct()))
                l.state = LosLureState::NoPoint;
            if (l.state != LosLureState::Active)
                continue;
            bool const allCame = std::all_of(l.casters.begin(), l.casters.end(),
                [&](ObjectGuid g) { return CasterCame(world, l, g); });
            // (the fight spot moving the tank for a patrol coming soon goes first)
            if (allCame || !hider || !hider->CanAct() || plan.fightSpot.Moves(l.hider))
                l.state = LosLureState::Done;
            else if (WorldTimer::getMSTimeDiff(l.sinceMs, now) > TIMEOUT_MS)
            {
                l.state = LosLureState::Failed;
                EnemyFacts const* first = world.FindEnemy(l.casters.front());
                char buf[200];
                snprintf(buf, sizeof(buf), "%s did not come out within %u s: fighting it from range.",
                    NameOf(first ? first->entry : 0).c_str(), TIMEOUT_MS / 1000);
                plan.notes.push_back(buf);
            }
        }

        for (EnemyFacts const& e : world.enemies)
        {
            if (Tried(m, e.guid) || plan.patrolGuard.Avoids(e.guid) || !StandsBack(world, e))
                continue;
            PartyMemberFacts const* victim = world.FindMember(e.victim);
            if (!victim || !victim->isBot || victim->isPet || !victim->CanAct())
                continue;
            // The patrol guard's hider already walks; the fight spot walks the tank (asked again
            // once it stands).
            if (plan.patrolGuard.ActiveLureOf(victim->guid) || plan.fightSpot.Moves(victim->guid))
                continue;
            if (LosLure* own = GoingLureOf(m, victim->guid))
            {
                if (own->state == LosLureState::Search && own->casters.size() < MAX_CASTERS)
                    own->casters.push_back(e.guid);
                continue;
            }
            LosLure l;
            l.hider = victim->guid;
            l.casters.push_back(e.guid);
            m.lures.push_back(l);
        }
    }
}
