#include "Commander/Coordination/Situation/FightSpot/FightSpot.h"
#include "Commander/Coordination/Doctrine/AreaIntent.h"
#include "Commander/Coordination/Doctrine/AreaSpot.h"
#include "Commander/Perception/Spatial/FieldSpots.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Spatial/PositionField.h"
#include "Commander/Perception/Values/AreaValue.h"
#include "Commander/Perception/Forecast/FightLoad.h"
#include "Commander/Perception/Forecast/FleePredictor.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Values/Exposure.h"
#include "Commander/Perception/Values/StandLoss.h"
#include "Commander/Coordination/Situation/Patrol/RouteGuard.h"
#include "Commander/Perception/Values/ControlValue.h"
#include "Commander/Perception/Values/SpotCost.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Trace/Trace.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>

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
            uint32 outside = 0;         // the tank's enemies (acting, not controlled) outside the circle now

            // A tank's cell `s` inside the circle (the pile around it is then mostly in it).
            bool Inside(Position3 const& s) const
            {
                Position3 const& c = request.centre;
                float const dx = s.x - c.x, dy = s.y - c.y;
                return dx * dx + dy * dy <= request.spell->radius * request.spell->radius;
            }
        };

        Channel MakeChannel(WorldModel const& world, TeamPlan const& plan, PartyMemberFacts const& tank)
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
                float const radius = spell->radius;
                for (EnemyFacts const& e : world.enemies)
                {
                    if (e.victim != tank.guid || !e.acting || e.crowdControlled || !e.health || plan.KeepsUnbroken(e))
                        continue;
                    float const dx = e.pos.x - r.centre.x, dy = e.pos.y - r.centre.y;
                    float const edge = std::sqrt(dx * dx + dy * dy) - e.boundingRadius;
                    if (edge > radius)
                        ++ch.outside;
                }
                ch.on = true;
                return ch;
            }
            return ch;
        }

        // The channel's loss with the enemies on the tank around S (each keeps its place
        // relative to the tank) and the tank busy walking there; negative: more of them in the
        // circle at S than here (a gain). `moved` is a copy of the world whose enemy positions
        // are rewritten.
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
            return ch.hereNet - (v.valid ? std::max(0.0f, v.net) : 0.0f);
        }

        // Header "flee": the creatures on the tank (before the fight: the pull's) that run for
        // help, their joiners judged from a run starting at S.
        struct Flee
        {
            struct Runner
            {
                std::vector<FleeJoiner> const* joiners = nullptr;
                Position3 offset;           // from the pile's place (each keeps its place relative to the tank)
                float reachYd = 0.0f;
                float arriveSec = 0.0f;
                std::map<uint64, float> points;     // by the joiners reached
            };
            bool on = false;
            FightLoad::Input base;
            float baseLoss = 0.0f;
            std::vector<Runner> runners;

            float Loss(WorldModel const& world, Position3 const& s)
            {
                float total = 0.0f;
                for (Runner& r : runners)
                {
                    Position3 start = s;
                    start.x += r.offset.x;
                    start.y += r.offset.y;
                    uint64 const mask = FleePredictor::Reached(*r.joiners, start, r.reachYd);
                    if (!mask)
                        continue;
                    auto const it = r.points.find(mask);
                    float const p = it != r.points.end() ? it->second
                        : (r.points[mask] = FleePredictor::PointsOf(world, base, baseLoss, *r.joiners, mask, r.arriveSec));
                    total += p;
                }
                return total;
            }
        };

        Flee MakeFlee(WorldModel const& world, TeamPlan const& plan, PartyMemberFacts const& tank, Request const& r)
        {
            Flee f;
            if (r.walkFromAt)
            {
                for (PullRunnerFacts const& p : world.pullRunners)
                {
                    Flee::Runner x;
                    x.joiners = &p.joiners;
                    x.reachYd = p.reachYd;
                    x.arriveSec = p.arriveSec;
                    f.runners.push_back(x);
                }
            }
            else
            {
                for (EnemyFacts const& e : world.enemies)
                {
                    FleeFacts const& fl = e.flee;
                    if (e.victim != tank.guid || !fl.predicted || fl.spent || fl.prevented || fl.joiners.empty())
                        continue;
                    Flee::Runner x;
                    x.joiners = &fl.joiners;
                    x.offset.x = e.pos.x - r.at.x;
                    x.offset.y = e.pos.y - r.at.y;
                    x.reachYd = fl.focusedReachYd;
                    x.arriveSec = (fl.fleeing ? 0.0f : fl.secToFlee) + FleePredictor::JOIN_SEC;
                    f.runners.push_back(x);
                }
            }
            if (f.runners.empty() || !FightLoad::FromWorld(world, f.base))
                return f;
            // Before the fight: the planned pull's creatures as the fight (nothing is engaged yet).
            if (r.walkFromAt)
                for (ObjectGuid g : plan.pull.expected)
                    for (PackFacts const& pack : world.packs)
                        for (PackCreature const& c : pack.members)
                            if (c.guid == g)
                                f.base.foes.push_back(FightLoad::FoeOf(c, tank, 0.0f));
            PartyMemberFacts const* main = Tanks::Main(world);
            float const penalty = main ? Value::DeathPenalty(*main) : 0.0f;
            f.baseLoss = FightLoad::Loss(world, FightLoad::Simulate(f.base, "flee: the fight spot's base"), penalty);
            f.on = true;
            return f;
        }

        // Header "team": how well the healer and ranged bots can stand around S (StandLoss), minus
        // the same around the pile's place.
        struct Team
        {
            struct Member
            {
                PartyMemberFacts const* facts = nullptr;
                bool healer = false;
                std::vector<size_t> turnable;       // indices into world.enemies (Exposure::Turnable)
                float here = 0.0f;
            };
            bool on = false;
            bool movePile = false;                  // in the fight: the tank's enemies come along
            PartyMemberFacts const* tank = nullptr;
            Position3 at;
            float patrolSec = 0.0f;
            std::vector<Member> members;
            std::vector<EnemyFacts> moved;          // world.enemies with the tank's around S

            float Best(WorldModel const& world, Member const& m, Position3 const& s) const
            {
                std::vector<EnemyFacts const*> enemies;
                for (size_t i : m.turnable)
                    enemies.push_back(&moved[i]);
                return m.healer ? StandLoss::BestHealerSpot(world, *m.facts, tank->guid, s, enemies, patrolSec)
                                : StandLoss::BestRangedSpot(world, *m.facts, s, enemies, patrolSec);
            }

            // No cell to stand on around S: it does nothing over the horizon.
            float Nowhere(WorldModel const& world, Member const& m) const
            {
                return Value::LostAction(world, *m.facts, StandLoss::HORIZON_SEC);
            }

            void Move(WorldModel const& world, Position3 const& s)
            {
                for (size_t i = 0; i < world.enemies.size(); ++i)
                {
                    EnemyFacts const& e = world.enemies[i];
                    Position3& p = moved[i].pos;
                    p = e.pos;
                    if (!movePile || e.victim != tank->guid || !e.acting || e.crowdControlled)
                        continue;
                    p.x = s.x + (e.pos.x - at.x);
                    p.y = s.y + (e.pos.y - at.y);
                    p.z = s.z + (e.pos.z - at.z);
                }
            }

            float Loss(WorldModel const& world, Position3 const& s)
            {
                Move(world, s);
                float total = 0.0f;
                for (Member const& m : members)
                {
                    float const best = Best(world, m, s);
                    total += (best >= 0.0f ? best : Nowhere(world, m)) - m.here;
                }
                return total;
            }
        };

        Team MakeTeam(WorldModel const& world, TeamPlan const& plan, PartyMemberFacts const& tank, Request const& r)
        {
            Team t;
            if (!world.field)
                return t;
            t.tank = &tank;
            t.at = r.at;
            t.movePile = !r.walkFromAt;
            t.patrolSec = RouteGuard::StaySec(world, plan);
            t.moved = world.enemies;
            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.isBot || m.isPet || !m.CanAct() || m.guid == tank.guid ||
                    (m.role != ROLE_HEALER && m.role != ROLE_RANGE_DPS))
                    continue;
                // (the area caster's place is the area term's)
                if (plan.area.IsCaster(m.guid))
                    continue;
                Team::Member x;
                x.facts = &m;
                x.healer = m.role == ROLE_HEALER;
                for (EnemyFacts const* e : Exposure::Turnable(world, m.guid))
                    x.turnable.push_back(size_t(e - world.enemies.data()));
                t.members.push_back(x);
            }
            if (t.members.empty())
                return t;
            t.Move(world, r.at);
            for (Team::Member& m : t.members)
            {
                float const best = t.Best(world, m, r.at);
                m.here = best >= 0.0f ? best : t.Nowhere(world, m);
            }
            t.on = true;
            return t;
        }

        struct Scored
        {
            Position3 pos;
            float loss = 0.0f;      // patrols + holds + flee
            float walk = 0.0f;
            float area = 0.0f;      // room (before the fight) or the channel's loss
            float team = 0.0f;      // the healers' and ranged's places (negative: better)
            float Total() const { return loss + walk + area + team; }
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
        Flee flee = MakeFlee(world, plan, tank, r);
        c.hereFlee = flee.on ? flee.Loss(world, r.at) : 0.0f;
        c.hereLoss = c.herePatrols + c.hereHolds + c.hereFlee;
        float const margin = Value::LostAction(world, tank, MARGIN_SEC);
        if (!world.field)
            return c;
        // Before the fight the caster's room can make another cell better (a gain). In the fight
        // a channel running gains when some of the tank's enemies are outside its circle
        // (strat-20261114-172715: the tank moved the pile off a Blizzard for a shackle and,
        // the loss here gone, stood at the circle's edge with half of them out); else nothing
        // to gain here, no search.
        Room const room = r.walkFromAt ? MakeRoom(world, plan, r) : Room();
        Channel const channel = r.walkFromAt ? Channel() : MakeChannel(world, plan, tank);
        c.channel = channel.on;
        if (c.hereLoss <= margin && !r.walkFromAt && channel.outside == 0)
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
                     SpotCost::BreaksHold(held, s.pos, clearYd) + (flee.on ? flee.Loss(world, s.pos) : 0.0f);
            float const walkYd = r.walkFromAt ? PackResolver::Distance(r.at, s.pos) * PATH_FACTOR : s.WalkEstimate(PATH_FACTOR);
            x.walk = Value::FightExtension(world, walkYd / WALK_YARDS_PER_SEC);
            if (room.on)
                x.area = room.Loss(s.pos) - hereRoom;
            scored.push_back(x);
        }
        std::stable_sort(scored.begin(), scored.end(), [](Scored const& a, Scored const& b) { return a.Total() < b.Total(); });

        // The channel running (in the fight) and the party's places: the best MAX_VALUED cells
        // by the other terms only (doc 9, answer 5), and with a channel also the best
        // MAX_VALUED inside its circle (the nearest cells by walk may all be outside it).
        Team team = scored.empty() ? Team() : MakeTeam(world, plan, tank, r);
        if ((channel.on || team.on) && !scored.empty())
        {
            if (scored.size() > MAX_VALUED)
            {
                std::vector<Scored> kept(scored.begin(), scored.begin() + MAX_VALUED);
                if (channel.on)
                {
                    size_t inside = 0;
                    for (size_t i = MAX_VALUED; i < scored.size() && inside < MAX_VALUED; ++i)
                        if (channel.Inside(scored[i].pos))
                        {
                            kept.push_back(scored[i]);
                            ++inside;
                        }
                }
                scored.swap(kept);
            }
            WorldModel moved;
            if (channel.on)
                moved = world;
            for (Scored& x : scored)
            {
                if (channel.on)
                    x.area = ChannelLoss(world, moved, channel, tank, r.at, x.pos);
                if (team.on)
                    x.team = team.Loss(world, x.pos);
            }
            std::stable_sort(scored.begin(), scored.end(), [](Scored const& a, Scored const& b) { return a.Total() < b.Total(); });
        }
        // Searched for the channel alone: only a cell with more of them in it moves the tank (the
        // team term prices the cells, it does not start a move, as without a channel).
        bool const channelOnly = !r.walkFromAt && c.hereLoss <= margin;
        scored.erase(std::remove_if(scored.begin(), scored.end(),
            [&](Scored const& x) { return x.Total() >= c.hereLoss - margin || (channelOnly && x.area >= 0.0f); }),
            scored.end());

        for (size_t i = 0; i < scored.size() && i < MAX_SPOTS; ++i)
            c.spots.push_back(scored[i].pos);
        if (!scored.empty())
        {
            c.bestLoss = scored.front().loss + scored.front().area + scored.front().team;
            c.bestArea = scored.front().area;
            c.bestTeam = scored.front().team;
            c.bestWalk = scored.front().walk;
        }
        char buf[300];
        snprintf(buf, sizeof(buf), "here %.1f (patrols %.1f, holds %.1f, flee %.1f%s); best %s %.1f (area %+.1f, party %+.1f) + walk %.1f",
            c.hereLoss, c.herePatrols, c.hereHolds, c.hereFlee,
            channel.outside ? Trace::Format(", %u outside the channel", channel.outside).c_str() : "",
            scored.empty() ? "none" : "spot", c.bestLoss, c.bestArea, c.bestTeam, c.bestWalk);
        c.why = buf;
        if (scope.On())
        {
            scope.Label("%s", c.why.c_str());
            scope.Data(Trace::Format("{\"candidates\":%u,\"held\":%u,\"clearYd\":%.1f,\"room\":%s,\"hereRoom\":%.1f,"
                "\"channel\":%s,\"channelNet\":%.1f,\"outside\":%u}", uint32(scored.size()), uint32(held.size()), clearYd,
                room.on ? "true" : "false", hereRoom, channel.on ? "true" : "false", channel.hereNet, channel.outside));
        }
        return c;
    }
}
}
