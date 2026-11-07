#include "Commander/Coordination/PullControl.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/CommanderMgr.h"
#include "Commander/Perception/ControlValue.h"
#include "Commander/Perception/FightLoad.h"
#include "Commander/Perception/PackResolver.h"
#include "Commander/Perception/ShotSpot.h"
#include "Commander/Perception/WorldModel.h"
#include "Creature.h"
#include "Map.h"
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
            ControlValue::Worth worth;
        };

        std::string NameOf(PullPlan const& pull, ObjectGuid guid)
        {
            for (PullMemberName const& n : pull.names)
                if (n.guid == guid)
                    return n.name;
            return "?";
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
                    if (!a.ready || a.fear || a.radius > 0.0f || a.peacefulOnly ||
                        !ControlValue::Affects(a, c->GetEntry(), c->GetCreatureTypeMask()))
                        continue;
                    Option o;
                    o.bot = &m;
                    o.ability = &a;
                    o.target = c;
                    o.worth = ControlValue::OfPull(world, fight, asIs, m, a, guid, nearTarget);
                    if (o.worth.Net() > 0.0f)
                        options.push_back(o);
                }
            }
        }
        std::stable_sort(options.begin(), options.end(), [](Option const& a, Option const& b)
        {
            return a.worth.Net() > b.worth.Net();
        });

        uint32 tries = 0;
        for (Option const& o : options)
        {
            if (++tries > MAX_SPOT_TRIES)
                break;
            Player* controller = map->GetPlayer(o.bot->guid);
            if (!controller)
                continue;
            ShotSpot::Request request;
            request.target = o.target;
            request.shooter = controller;
            request.maxRange = std::max(5.0f, o.ability->maxRange - RANGE_MARGIN_YD);
            ShotSpot::Spot spot;
            ShotSpot::Failure why = ShotSpot::Failure::None;
            if (!ShotSpot::Find(map, request, spot, &why))
                continue;

            PullControlPlan& control = pull.control;
            control.bot = o.bot->guid;
            control.target = o.target->GetObjectGuid();
            control.spellId = o.ability->spellId;
            control.spot = spot.at;
            control.points = o.worth.Net();
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(control.spellId);
            char buf[300];
            snprintf(buf, sizeof(buf), "Opening control: %s casts %s on %s as the tank pulls (%s).",
                controller->GetName(), spell ? spell->SpellName[0].c_str() : "?", o.target->GetName(),
                ControlValue::Describe(o.worth).c_str());
            plan.notes.push_back(buf);
            return;
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
        if (pull.phase != PullPhase::Approach || !control.Active() || control.inPlace)
            return;
        // The fight started without it (the player, a patrol): it casts when it can anyway.
        if (world.HasPartyEnemy())
        {
            control.inPlace = true;
            return;
        }
        PartyMemberFacts const* bot = world.FindMember(control.bot);
        if (!bot || !bot->CanAct())
        {
            control.dropped = true;
            notes.push_back("Opening control dropped: " + NameOf(pull, control.bot) + " cannot act.");
        }
        else if (PackResolver::Distance(bot->pos, control.spot) <= ARRIVED_YD)
            control.inPlace = true;
        else if (WorldTimer::getMSTimeDiff(control.sinceMs, nowMs) > PLACE_TIMEOUT_MS)
        {
            control.dropped = true;
            notes.push_back("Opening control dropped: " + NameOf(pull, control.bot) +
                " did not reach its spot in time. Pulling without it.");
        }
        else
            return;
        // The tank waited for this: its Approach timers start now.
        pull.phaseSinceMs = nowMs;
    }
}
}
