#include "Commander/Coordination/Situation/FightSpot/FightSpot.h"
#include "Commander/Coordination/Doctrine/AreaIntent.h"
#include "Commander/Coordination/Doctrine/AreaSpot.h"
#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Values/AreaValue.h"
#include "Commander/Perception/Values/ControlValue.h"
#include "Commander/Perception/Values/SpotCost.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Trace/Trace.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace Commander
{
namespace FightSpot
{
    namespace
    {
        // Header "area", before the fight: how well the planned area caster can stand for a pile
        // at S (AreaSpot's pre-fight score: slip + walk of its best cell within range).
        struct Room
        {
            bool on = false;
            float reach = 0.0f;                     // range - AreaSpot::SPOT_MARGIN_YD
            std::vector<AreaSpot::CasterCell> cells;
            std::vector<float> slipByYd;            // AreaSpot::SlipAt by whole yards of gap
            float worst = 0.0f;                     // no cell in range: cast standing on the pile

            float Slip(float gap) const
            {
                size_t const i = std::min(slipByYd.size() - 1, size_t(std::max(0.0f, gap)));
                return slipByYd[i];
            }

            // Lower is better.
            float Loss(Position3 const& s) const
            {
                float best = worst;
                float const reach2 = reach * reach;
                for (AreaSpot::CasterCell const& c : cells)
                {
                    float const dx = c.pos.x - s.x, dy = c.pos.y - s.y;
                    float const d2 = dx * dx + dy * dy;
                    if (d2 > reach2)
                        continue;
                    best = std::min(best, Slip(std::sqrt(d2)) + c.walkCost);
                }
                return best;
            }
        };

        Room MakeRoom(WorldModel const& world, TeamPlan const& plan, Request const& r)
        {
            Room room;
            PartyMemberFacts const* m = AreaIntent::PlannedCaster(world, plan);
            AreaSpellFacts const* spell = m ? AreaIntent::BestSpell(*m) : nullptr;
            if (!spell || !spell->onTarget || !spell->channel || !world.field)
                return room;
            room.reach = std::max(0.0f, spell->range - AreaSpot::SPOT_MARGIN_YD);
            room.cells = AreaSpot::CasterCells(world, plan, *m, r.at, MAX_YD + room.reach);
            // Where it stands now, as Pick's "here" (no walk), when no idle creature covers it.
            FieldCell const* own = world.field->At(m->pos);
            if (!own || !own->idle)
            {
                AreaSpot::CasterCell here;
                here.pos = m->pos;
                room.cells.push_back(here);
            }
            Position3 away = r.at;
            for (uint32 yd = 0; yd <= uint32(std::ceil(room.reach)); ++yd)
            {
                away.x = r.at.x + float(yd);
                room.slipByYd.push_back(AreaSpot::SlipAt(world, *m, *spell, r.at, away));
            }
            room.worst = room.slipByYd.front();
            room.on = true;
            return room;
        }

        // Header "area", in the fight: the channel running, valued with the pile at S.
        struct Channel
        {
            bool on = false;
            AreaValue::Request request;
            float hereNet = 0.0f;
        };

        Channel MakeChannel(WorldModel const& world, TeamPlan const& plan)
        {
            Channel ch;
            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.isBot || m.isPet || !m.areaChannel.spellId)
                    continue;
                AreaSpellFacts const* spell = AreaValue::SpellOf(m, m.areaChannel.spellId);
                if (!spell)
                    continue;
                AreaValue::Request& r = ch.request;
                r.caster = &m;
                r.spell = spell;
                r.centre = m.areaChannel.centre;
                r.from = m.pos;
                r.ticks = m.areaChannel.ticksLeft;
                r.paid = true;
                r.forbidden = [&plan](EnemyFacts const& e) { return plan.KeepsUnbroken(e); };
                AreaValue::Result const v = AreaValue::Evaluate(world, r);
                ch.hereNet = v.valid ? std::max(0.0f, v.net) : 0.0f;
                ch.on = true;
                return ch;
            }
            return ch;
        }

        // The channel's loss with the enemies on the tank around S (each keeps its place
        // relative to the tank) and the tank busy walking there. `moved` is a copy of the world
        // whose enemy positions are rewritten.
        float ChannelLoss(WorldModel const& world, WorldModel& moved, Channel const& ch, PartyMemberFacts const& tank,
                          Position3 const& at, Position3 const& s)
        {
            for (size_t i = 0; i < world.enemies.size(); ++i)
            {
                EnemyFacts const& e = world.enemies[i];
                Position3& p = moved.enemies[i].pos;
                p = e.pos;
                if (e.victim != tank.guid || !e.acting || e.crowdControlled)
                    continue;
                p.x = s.x + (e.pos.x - at.x);
                p.y = s.y + (e.pos.y - at.y);
                p.z = s.z + (e.pos.z - at.z);
            }
            AreaValue::Request r = ch.request;
            r.turn.busyTanks.push_back(tank.guid);
            AreaValue::Result const v = AreaValue::Evaluate(moved, r);
            return std::max(0.0f, ch.hereNet - (v.valid ? std::max(0.0f, v.net) : 0.0f));
        }

        struct Scored
        {
            Position3 pos;
            float loss = 0.0f;      // patrols + holds
            float walk = 0.0f;
            float area = 0.0f;      // room (before the fight) or the channel's loss
            float Total() const { return loss + walk + area; }
        };
    }

    Request InFight(WorldModel const& world, PartyMemberFacts const& tank, float patrolWithinSec)
    {
        Request r;
        r.at = tank.pos;
        r.remainingSec = Value::FightRemainingSec(world);
        r.patrolWithinSec = patrolWithinSec;
        return r;
    }

    Choice Choose(WorldModel const& world, TeamPlan const& plan, PartyMemberFacts const& tank, Request const& r)
    {
        Choice c;
        std::vector<SpotCost::HeldMez> const held = SpotCost::HeldMezzes(world);
        float const clearYd = ControlValue::ClearOfPileYd(world);
        c.herePatrols = SpotCost::PatrolJoins(world, tank, r.at, r.remainingSec, r.patrolWithinSec, &c.patrol);
        c.hereHolds = SpotCost::BreaksHold(held, r.at, clearYd);
        c.hereLoss = c.herePatrols + c.hereHolds;
        float const margin = Value::LostAction(world, tank, MARGIN_SEC);
        if (!world.field)
            return c;
        // Before the fight the caster's room can make another cell better (a gain); in the
        // fight the area term is only a loss of moving: nothing to gain here, no search.
        Room const room = r.walkFromAt ? MakeRoom(world, plan, r) : Room();
        if (c.hereLoss <= margin && !room.on)
            return c;
        float const hereRoom = room.on ? room.Loss(r.at) : 0.0f;

        Trace::Scope scope("fightspot");
        FieldSpots::Query q;
        q.center = r.at;
        q.minYd = MIN_YD;
        q.maxYd = MAX_YD;
        std::vector<Scored> scored;
        for (FieldSpots::Spot const& s : FieldSpots::Collect(world, tank, q))
        {
            Scored x;
            x.pos = s.pos;
            x.loss = SpotCost::PatrolJoins(world, tank, s.pos, r.remainingSec, r.patrolWithinSec) +
                     SpotCost::BreaksHold(held, s.pos, clearYd);
            float const walkYd = r.walkFromAt ? PackResolver::Distance(r.at, s.pos) * PATH_FACTOR : s.WalkEstimate(PATH_FACTOR);
            x.walk = Value::FightExtension(world, walkYd / WALK_YARDS_PER_SEC);
            if (room.on)
                x.area = room.Loss(s.pos) - hereRoom;
            scored.push_back(x);
        }
        std::stable_sort(scored.begin(), scored.end(), [](Scored const& a, Scored const& b) { return a.Total() < b.Total(); });

        // The channel running (in the fight): the best MAX_VALUED cells only (doc 9, answer 5).
        Channel const channel = r.walkFromAt ? Channel() : MakeChannel(world, plan);
        if (channel.on && !scored.empty())
        {
            if (scored.size() > MAX_VALUED)
                scored.resize(MAX_VALUED);
            WorldModel moved = world;
            for (Scored& x : scored)
                x.area = ChannelLoss(world, moved, channel, tank, r.at, x.pos);
            std::stable_sort(scored.begin(), scored.end(), [](Scored const& a, Scored const& b) { return a.Total() < b.Total(); });
        }
        scored.erase(std::remove_if(scored.begin(), scored.end(),
            [&](Scored const& x) { return x.Total() >= c.hereLoss - margin; }), scored.end());

        for (size_t i = 0; i < scored.size() && i < MAX_SPOTS; ++i)
            c.spots.push_back(scored[i].pos);
        if (!scored.empty())
        {
            c.bestLoss = scored.front().loss + scored.front().area;
            c.bestArea = scored.front().area;
            c.bestWalk = scored.front().walk;
        }
        char buf[240];
        snprintf(buf, sizeof(buf), "here %.1f (patrols %.1f, holds %.1f); best %s %.1f (area %+.1f) + walk %.1f",
            c.hereLoss, c.herePatrols, c.hereHolds, scored.empty() ? "none" : "spot", c.bestLoss, c.bestArea, c.bestWalk);
        c.why = buf;
        if (scope.On())
        {
            scope.Label("%s", c.why.c_str());
            scope.Data(Trace::Format("{\"candidates\":%u,\"held\":%u,\"clearYd\":%.1f,\"room\":%s,\"hereRoom\":%.1f,"
                "\"channel\":%s,\"channelNet\":%.1f}", uint32(scored.size()), uint32(held.size()), clearYd,
                room.on ? "true" : "false", hereRoom, channel.on ? "true" : "false", channel.hereNet));
        }
        return c;
    }
}
}
