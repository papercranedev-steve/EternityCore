#include "Commander/Coordination/PullControl.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Classes/ClassKit.h"
#include "Commander/Perception/AggroRange.h"
#include "Commander/Perception/ControlValue.h"
#include "Commander/Perception/FightLoad.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/SapSpot.h"
#include "Commander/Perception/SapValue.h"
#include "Commander/Perception/ShotSpot.h"
#include "Commander/Perception/StealthDetect.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/WorldModel.h"
#include "Creature.h"
#include "Map.h"
#include "PartyBotAI.h"
#include "Player.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <cstdio>

namespace Commander
{
namespace PullControl
{
    namespace
    {
        struct Option
        {
            PartyMemberFacts const* bot = nullptr;
            ControlAbility const* ability = nullptr;
            Creature* target = nullptr;
            bool sap = false;           // ControlAbility::peacefulOnly: cast before the opener
            float optimistic = 0.0f;    // net before the spot is known (sorting)
        };

        // An option with its spot and final worth.
        struct Choice
        {
            Option const* option = nullptr;
            Position3 spot;
            float net = 0.0f;
            uint32 placeTimeoutMs = PLACE_TIMEOUT_MS;
            std::string why;            // ControlValue / SapValue Describe
        };

        std::string NameOf(PullPlan const& pull, ObjectGuid guid)
        {
            for (PullMemberName const& n : pull.names)
                if (n.guid == guid)
                    return n.name;
            return "?";
        }

        PackCreature const* FindIdle(WorldModel const& world, ObjectGuid guid)
        {
            for (PackFacts const& p : world.packs)
                for (PackCreature const& c : p.members)
                    if (c.guid == guid)
                        return &c;
            return nullptr;
        }

        Position3 PositionOf(WorldObject const* obj)
        {
            Position3 p;
            p.x = obj->GetPositionX();
            p.y = obj->GetPositionY();
            p.z = obj->GetPositionZ();
            return p;
        }

        // Where the tank opens from: its shot or face pull spot, else the target.
        Position3 OpenerPoint(PullPlan const& pull, Map* map)
        {
            if (pull.rangedPullSpell)
                return pull.rangedSpot;
            if (pull.facePull)
                return pull.faceSpot;
            if (Creature* target = map->GetCreature(pull.target))
                return PositionOf(target);
            return pull.packAt;
        }

        // A Sap target must stay where the spot was found: no wanderers or patrols.
        bool StaysPut(Creature const* c)
        {
            return AggroRange::WanderOf(c).radius <= 0.0f && c->GetDefaultMovementType() != WAYPOINT_MOTION_TYPE;
        }

        bool TrySap(WorldModel const& world, Map* map, PullPlan const& pull, FightLoad::Input const& fight,
                    FightLoad::Result const& asIs, uint32 nearTarget, Option const& o, Choice& out)
        {
            Player* rogue = map->GetPlayer(o.bot->guid);
            PartyBotAI const* ai = rogue ? dynamic_cast<PartyBotAI const*>(rogue->AI()) : nullptr;
            if (!ai)
                return false;
            SapSpot::Request request;
            request.target = o.target;
            request.rogue = rogue;
            request.stealthSkill = StealthDetect::StealthSkill(rogue, ai->Kit().Stealth());
            request.standSec = SAP_STAND_SEC;
            SapSpot::Spot spot;
            SapSpot::Failure why = SapSpot::Failure::None;
            if (!SapSpot::Find(map, request, spot, &why))
                return false;

            SapValue::Input in;
            in.rogue = rogue;
            in.target = o.target;
            in.spot = spot.at;
            in.expected = pull.expected;
            in.hasPlannedSpot = pull.PlannedFightSpot(in.plannedSpot);
            if (PartyMemberFacts const* tank = Tanks::Main(world))
                in.tankTravelSec = PackResolver::Distance(tank->pos, OpenerPoint(pull, map)) / SapValue::RUN_YARDS_PER_SEC;
            in.nearTarget = nearTarget;
            SapValue::Worth const w = SapValue::Of(world, map, fight, asIs, *o.bot, *o.ability, in);
            out.option = &o;
            out.spot = spot.at;
            out.net = w.Net();
            out.placeTimeoutMs = std::max(PLACE_TIMEOUT_MS,
                uint32(spot.walkYd / STEALTH_WALK_YD_PER_SEC * 1000.0f) + SAP_WALK_SLACK_MS);
            out.why = SapValue::Describe(w);
            return true;
        }

        bool TryCast(Map* map, Option const& o, ControlValue::Worth const& worth, Choice& out)
        {
            Player* controller = map->GetPlayer(o.bot->guid);
            if (!controller)
                return false;
            ShotSpot::Request request;
            request.target = o.target;
            request.shooter = controller;
            request.maxRange = std::max(5.0f, o.ability->maxRange - RANGE_MARGIN_YD);
            request.standSec = STAND_SEC;
            ShotSpot::Spot spot;
            ShotSpot::Failure why = ShotSpot::Failure::None;
            if (!ShotSpot::Find(map, request, spot, &why))
                return false;
            out.option = &o;
            out.spot = spot.at;
            out.net = worth.Net();
            out.why = ControlValue::Describe(worth);
            return true;
        }
    }

    void Choose(TeamPlan& plan, WorldModel const& world, Map* map, uint32 nowMs)
    {
        PullPlan& pull = plan.pull;
        pull.control = PullControlPlan();
        pull.control.decided = true;
        pull.control.sinceMs = nowMs;
        if (!sCommanderMgr.IsEnabled() || !sCommanderMgr.IsModuleEnabled("cc"))
            return;

        FightLoad::Input fight;
        if (!FightLoad::Build(world, map, pull.expected, fight))
            return;
        FightLoad::Result const asIs = FightLoad::Simulate(fight);
        // Fought where the pack stands: the controlled one sleeps next to the fight.
        bool const elsewhere = pull.returnToSpot || pull.OpensFromAfar();
        uint32 const nearTarget = elsewhere ? 0 : uint32(pull.expected.size()) - 1;

        std::vector<Option> options;
        for (ObjectGuid guid : pull.expected)
        {
            if (guid == pull.target)
                continue;
            Creature* c = map->GetCreature(guid);
            if (!c || !c->IsAlive() || c->IsInCombat())
                continue;
            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.isBot || m.isPet || !m.CanAct() || m.guid == pull.tank)
                    continue;
                for (ControlAbility const& a : m.controls)
                {
                    if (!a.ready || a.fear || a.radius > 0.0f ||
                        !ControlValue::Affects(a, c->GetEntry(), c->GetCreatureTypeMask()))
                        continue;
                    Option o;
                    o.bot = &m;
                    o.ability = &a;
                    o.target = c;
                    o.sap = a.peacefulOnly;
                    if (o.sap && !StaysPut(c))
                        continue;
                    o.optimistic = o.sap ? SapValue::Optimistic(world, fight, asIs, m, a, guid, nearTarget)
                                         : ControlValue::OfPull(world, fight, asIs, m, a, guid, nearTarget).Net();
                    if (o.optimistic > 0.0f)
                        options.push_back(o);
                }
            }
        }
        std::stable_sort(options.begin(), options.end(), [](Option const& a, Option const& b)
        {
            return a.optimistic > b.optimistic;
        });

        // The first MAX_SPOT_TRIES options get a spot and their final worth (a Sap's failure
        // risk depends on its spot); the best positive one is taken.
        Choice best;
        uint32 tries = 0;
        for (Option const& o : options)
        {
            if (++tries > MAX_SPOT_TRIES)
                break;
            Choice c;
            bool const found = o.sap
                ? TrySap(world, map, pull, fight, asIs, nearTarget, o, c)
                : TryCast(map, o, ControlValue::OfPull(world, fight, asIs, *o.bot, *o.ability,
                                                        o.target->GetObjectGuid(), nearTarget), c);
            if (!found)
                continue;
            if (o.sap && c.net <= 0.0f)
            {
                char buf[300];
                snprintf(buf, sizeof(buf), "No Sap on %s: not worth the risk (%s).", o.target->GetName(), c.why.c_str());
                plan.notes.push_back(buf);
                continue;
            }
            if (c.net > 0.0f && (!best.option || c.net > best.net))
                best = c;
        }
        if (!best.option)
            return;

        Option const& o = *best.option;
        PullControlPlan& control = pull.control;
        control.bot = o.bot->guid;
        control.target = o.target->GetObjectGuid();
        control.spellId = o.ability->spellId;
        control.spot = best.spot;
        control.points = best.net;
        control.beforeOpener = o.sap;
        control.placeTimeoutMs = best.placeTimeoutMs;
        SpellEntry const* spell = sSpellMgr.GetSpellEntry(control.spellId);
        Player const* controller = map->GetPlayer(o.bot->guid);
        char buf[400];
        snprintf(buf, sizeof(buf), o.sap
                ? "Opening control: %s sneaks up and casts %s on %s before the tank pulls (%s)."
                : "Opening control: %s casts %s on %s as the tank pulls (%s).",
            controller ? controller->GetName() : "?", spell ? spell->SpellName[0].c_str() : "?", o.target->GetName(),
            best.why.c_str());
        plan.notes.push_back(buf);
    }

    namespace
    {
        void Drop(PullPlan const& pull, PullControlPlan& control, char const* why, std::vector<std::string>& notes)
        {
            control.dropped = true;
            notes.push_back("Opening control dropped: " + NameOf(pull, control.bot) + " " + why);
        }

        // Sap (PullControlPlan::beforeOpener): true when the tank's wait changed.
        bool UpdateBeforeOpener(WorldModel const& world, PullPlan const& pull, PullControlPlan& control,
                                uint32 nowMs, std::vector<std::string>& notes)
        {
            if (control.landed)
                return false;
            // A miss or an immune target pulls it onto the rogue; anything else starting the
            // fight first (the player, a patrol) leaves it in combat: no Sap any more.
            if (world.HasPartyEnemy())
            {
                Drop(pull, control, "could not Sap: the fight started.", notes);
                return true;
            }
            if (PackCreature const* c = FindIdle(world, control.target))
                if (c->controlSpellId)
                {
                    control.landed = true;
                    notes.push_back("Opening control: " + NameOf(pull, control.bot) + "'s Sap landed, the tank pulls.");
                    return true;
                }
            PartyMemberFacts const* bot = world.FindMember(control.bot);
            if (!bot || !bot->CanAct())
            {
                Drop(pull, control, "cannot act.", notes);
                return true;
            }
            if (!control.inPlace)
            {
                if (PackResolver::Distance(bot->pos, control.spot) <= ARRIVED_YD)
                {
                    control.inPlace = true;
                    control.inPlaceMs = nowMs;
                    return false;
                }
                if (WorldTimer::getMSTimeDiff(control.sinceMs, nowMs) > control.placeTimeoutMs)
                {
                    Drop(pull, control, "did not reach its Sap spot in time. Pulling without it.", notes);
                    return true;
                }
                return false;
            }
            if (WorldTimer::getMSTimeDiff(control.inPlaceMs, nowMs) > SAP_CAST_TIMEOUT_MS)
            {
                Drop(pull, control, "could not cast Sap from its spot. Pulling without it.", notes);
                return true;
            }
            return false;
        }
    }

    void Update(WorldModel const& world, PullPlan& pull, uint32 nowMs, std::vector<std::string>& notes)
    {
        PullControlPlan& control = pull.control;
        if (pull.phase == PullPhase::WaitReady)
        {
            control = PullControlPlan();
            return;
        }
        if (pull.phase != PullPhase::Approach || !control.Active())
            return;
        if (control.beforeOpener)
        {
            // The tank waited for this: its Approach timers start now.
            if (UpdateBeforeOpener(world, pull, control, nowMs, notes))
                pull.phaseSinceMs = nowMs;
            return;
        }
        if (control.inPlace)
            return;
        // The fight started without it (the player, a patrol): it casts when it can anyway.
        if (world.HasPartyEnemy())
        {
            control.inPlace = true;
            return;
        }
        PartyMemberFacts const* bot = world.FindMember(control.bot);
        if (!bot || !bot->CanAct())
            Drop(pull, control, "cannot act.", notes);
        else if (PackResolver::Distance(bot->pos, control.spot) <= ARRIVED_YD)
            control.inPlace = true;
        else if (WorldTimer::getMSTimeDiff(control.sinceMs, nowMs) > PLACE_TIMEOUT_MS)
            Drop(pull, control, "did not reach its spot in time. Pulling without it.", notes);
        else
            return;
        // The tank waited for this: its Approach timers start now.
        pull.phaseSinceMs = nowMs;
    }
}
}
