#include "Commander/ProfileCommands.h"
#include "Commander/BotDirectives.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Perception/ProfileStore.h"
#include "Commander/Perception/ValueModel.h"
#include "Commander/Perception/CastValue.h"
#include "Commander/Perception/ControlValue.h"
#include "Commander/Perception/FightLoad.h"
#include "Commander/Perception/FleeStopValue.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Chat.h"
#include "Creature.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>
#include <string>

namespace Commander
{
namespace ProfileCommands
{
    namespace
    {
        std::string SpellName(uint32 spellId)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
            return spell ? spell->SpellName[0] : "?";
        }

        char const* TargetTypeName(uint8 castTarget)
        {
            switch (castTarget)
            {
                case 0:    return "provided";
                case 1:    return "victim";
                case 2:    return "2nd-threat";
                case 3:    return "last-threat";
                case 4:    return "random";
                case 5:    return "random-not-top";
                case 6:    return "nearest";
                case 8:    return "self";
                case 0xFF: return "-";
                default:   return "other";
            }
        }

        char const* ResultName(CandidateRecord::Result result)
        {
            switch (result)
            {
                case CandidateRecord::Result::NotTried:  return "not tried";
                case CandidateRecord::Result::Executed:  return "EXECUTED";
                case CandidateRecord::Result::Failed:    return "failed";
                case CandidateRecord::Result::LowerTier: return "lower tier";
                case CandidateRecord::Result::Dropped:   return "dropped (benefit <= cost)";
                case CandidateRecord::Result::Forbidden: return "forbidden by the plan";
                case CandidateRecord::Result::VetoSet:   return "VETO";
                case CandidateRecord::Result::OrderSet:  return "HEAL ORDER";
            }
            return "?";
        }

        char const* SchoolName(uint8 school)
        {
            switch (school)
            {
                case SPELL_SCHOOL_NORMAL: return "physical";
                case SPELL_SCHOOL_HOLY:   return "holy";
                case SPELL_SCHOOL_FIRE:   return "fire";
                case SPELL_SCHOOL_NATURE: return "nature";
                case SPELL_SCHOOL_FROST:  return "frost";
                case SPELL_SCHOOL_SHADOW: return "shadow";
                case SPELL_SCHOOL_ARCANE: return "arcane";
                default:                  return "?";
            }
        }

        // creature_spells.castFlags (ScriptCommands.h CastFlags) as short names, "" for none.
        std::string CastFlagNames(uint16 flags)
        {
            static struct { uint16 flag; char const* name; } const names[] =
            {
                { CF_INTERRUPT_PREVIOUS, "interrupt-previous" },
                { CF_TRIGGERED,          "triggered" },
                { CF_FORCE_CAST,         "force" },
                { CF_MAIN_RANGED_SPELL,  "MAIN-RANGED" },
                { CF_TARGET_UNREACHABLE, "if-unreachable" },
                { CF_AURA_NOT_PRESENT,   "aura-not-present" },
                { CF_ONLY_IN_MELEE,      "only-in-melee" },
                { CF_NOT_IN_MELEE,       "not-in-melee" },
                { CF_TARGET_CASTING,     "target-casting" },
            };
            std::string out;
            for (auto const& n : names)
            {
                if (!(flags & n.flag))
                    continue;
                if (!out.empty())
                    out += " ";
                out += n.name;
            }
            return out;
        }

        // "[source, ...] " in front of one creature spell: where it comes from and when it is cast.
        std::string SpellRefHead(CreatureSpellRef const& s)
        {
            char buf[96];
            std::string head = "  [";
            head += CreatureProfiles::SourceName(s.source);
            if (s.source == SpellSource::OnHit)
            {
                snprintf(buf, sizeof(buf), " via %u, %u%%", s.viaAuraId, s.procChance);
                head += buf;
            }
            else
            {
                if (s.castTarget != 0xFF)
                {
                    head += ", target ";
                    head += TargetTypeName(s.castTarget);
                }
                if (s.healthPct > 0.0f)
                {
                    snprintf(buf, sizeof(buf), ", at %.0f%% hp", s.healthPct);
                    head += buf;
                }
                if (s.initialMaxMs)
                {
                    snprintf(buf, sizeof(buf), ", first %u-%u ms", s.initialMinMs, s.initialMaxMs);
                    head += buf;
                }
                if (s.repeatMaxMs || s.source == SpellSource::SpellList)
                {
                    snprintf(buf, sizeof(buf), ", repeat %u-%u ms", s.repeatMinMs, s.repeatMaxMs);
                    head += buf;
                }
                if (s.castFlags)
                {
                    snprintf(buf, sizeof(buf), ", flags 0x%X %s", s.castFlags, CastFlagNames(s.castFlags).c_str());
                    head += buf;
                }
            }
            if (s.reapplies)
                head += ", reapplies";
            head += "] ";
            return head;
        }

        // `prefix` starts the first line, `indent` the following ones (the creature view puts
        // the spell's source in front; repeating it on every line made them too long).
        void ShowSpellLine(ChatHandler* handler, uint32 spellId, uint32 casterLevel, float meleeHit,
                           char const* prefix, char const* indent)
        {
            std::shared_ptr<SpellProfile const> p = sProfileStore.GetSpell(spellId);
            if (!p)
            {
                handler->PSendSysMessage("%s%u: unknown spell", prefix, spellId);
                return;
            }
            SpellAmounts const a = SpellProfiles::Estimate(sSpellMgr.GetSpellEntry(spellId), casterLevel, meleeHit);
            handler->PSendSysMessage("%s%u %s: %s (%s), cast %u ms, dur %u ms, radius %.0f, %s%s%s, dispel %s%s",
                prefix, spellId, SpellName(spellId).c_str(), SpellTags::CategoryName(p->category),
                SchoolName(p->school), p->castMs, p->durationMs, p->radius,
                p->interruptible ? "interruptible" : "not interruptible",
                p->controls ? ", control" : "", p->breaksOnDamage ? " (breaks on damage)" : "",
                SpellProfiles::DispelName(p->dispel), p->harmful ? "" : " (positive)");
            handler->PSendSysMessage("%s    at level %u: damage %.0f + %.0f over time, heal %.0f + %.0f%s, control %.1f s, mana %.0f",
                indent, casterLevel, a.damage, a.periodicDamage, a.heal, a.periodicHeal,
                a.healsToFull ? " (to full)" : "", a.controlSec, a.mana);
            if (a.summonEntry)
            {
                CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(a.summonEntry);
                handler->PSendSysMessage("%s    summons %.0f x %u %s", indent, std::max(1.0f, a.summonCount),
                    a.summonEntry, info ? info->name.c_str() : "?");
            }
            if (p->hasOverride)
                handler->PSendSysMessage("%s    override: interrupt x%.2f, dispel %s  %s", indent,
                    p->override.interruptMult, SpellProfiles::PolicyName(p->override.dispel), p->override.comment.c_str());
        }
    }

    bool ShowCreature(ChatHandler* handler, Creature* creature)
    {
        if (!creature)
        {
            handler->SendSysMessage("Select a creature first.");
            return false;
        }

        std::shared_ptr<CreatureProfile const> p = sProfileStore.GetCreature(creature->GetEntry());
        if (!p)
        {
            handler->SendSysMessage("No creature template for this entry.");
            return false;
        }

        uint32 const level = creature->GetLevel();
        float const liveHit = (creature->GetFloatValue(UNIT_FIELD_MINDAMAGE) + creature->GetFloatValue(UNIT_FIELD_MAXDAMAGE)) / 2.0f;
        uint32 const liveAttackMs = creature->GetAttackTime(BASE_ATTACK);
        handler->PSendSysMessage("%s (entry %u) rank %u, level %u-%u (this one %u), class %u, damage x%.2f",
            p->name.c_str(), p->entry, p->rank, p->levelMin, p->levelMax, level, p->unitClass, p->damageMultiplier);
        handler->PSendSysMessage("  melee: profile %.0f per hit, %.1f dps at level %u | live %.0f per hit, %u ms, %.1f dps",
            CreatureProfiles::EstimateMeleeHit(*p, level), CreatureProfiles::EstimateMeleeDps(*p, level), level,
            liveHit, liveAttackMs, liveAttackMs ? liveHit / (liveAttackMs / 1000.0f) : 0.0f);
        handler->PSendSysMessage("  taunt immune: %s, immune masks: mechanic 0x%X, school 0x%X, flees at %.0f%% health",
            p->tauntImmune ? "yes" : "no", p->mechanicImmuneMask, p->schoolImmuneMask, p->fleeAtHealthPct);
        // Step 8 fields: what pull / clear and the cast values read from the profile.
        std::string ranged = "none";
        if (p->mainRangedSpellId)
            ranged = std::to_string(p->mainRangedSpellId) + " " + SpellName(p->mainRangedSpellId);
        handler->PSendSysMessage("  boss: %s, C++ script: %s, no aggro: %s, main ranged spell: %s",
            p->boss ? "yes" : "no", p->scripted ? "yes (EventAI rows ignored)" : "no",
            p->noAggro ? "yes" : "no", ranged.c_str());
        if (p->hasOverride)
            handler->PSendSysMessage("  override: interrupt x%.2f, taunt x%.2f  %s",
                p->override.interruptMult, p->override.tauntMult, p->override.comment.c_str());

        if (p->spells.empty())
            handler->SendSysMessage("  spells: none known (C++ script creatures are not covered)");
        for (CreatureSpellRef const& s : p->spells)
            ShowSpellLine(handler, s.spellId, level, liveHit, SpellRefHead(s).c_str(), "    ");
        return true;
    }

    bool ShowSpell(ChatHandler* handler, uint32 spellId, uint32 casterLevel)
    {
        if (!sSpellMgr.GetSpellEntry(spellId))
        {
            handler->PSendSysMessage("Unknown spell %u.", spellId);
            return false;
        }
        ShowSpellLine(handler, spellId, casterLevel, 0.0f, "", "");
        return true;
    }

    bool ShowRates(ChatHandler* handler, Player* player)
    {
        std::shared_ptr<PartyCommander> commander = sCommanderMgr.FindCommander(player);
        if (!commander)
        {
            handler->SendSysMessage("No commander for your party on this map.");
            return false;
        }

        WorldModel const& world = commander->GetWorld();
        RateFacts const& r = world.rates;
        handler->PSendSysMessage("Party max health %.0f (1 point = %.0f health), %u healers, %u others",
            r.partyMaxHealth, r.partyMaxHealth / 100.0f, r.healers, r.damageDealers);
        handler->PSendSysMessage("Incoming melee %.1f/s = %.2f points/s; dealt %.1f/s (%s); enemy health %.0f, fight left %.1f s",
            r.incomingDps, Value::Points(world, r.incomingDps), Value::DealtDps(world),
            r.dealtMeasured ? "measured" : "estimate", r.enemyHealth, Value::FightRemainingSec(world));

        Map* map = player->GetMap();
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            Unit* unit = map->GetUnit(m.guid);
            handler->PSendSysMessage("  %s: %.2f points/s, global cooldown costs %.2f, death penalty %.0f",
                unit ? unit->GetName() : "?", Value::ContributionPerSec(world, m),
                Value::LostAction(world, m, Value::GCD_SEC), Value::DeathPenalty(m));
        }
        for (EnemyFacts const& e : world.enemies)
        {
            PartyMemberFacts const* victim = world.FindMember(e.victim);
            Unit* unit = map->GetUnit(e.guid);
            Unit* victimUnit = victim ? map->GetUnit(victim->guid) : nullptr;
            if (victim)
                handler->PSendSysMessage("  enemy %s: %.1f melee dps, on %s %.1f after armor, %s survives %.1f s alone",
                    unit ? unit->GetName() : "?", e.meleeDps, victimUnit ? victimUnit->GetName() : "?",
                    Value::HitDps(e, *victim), victimUnit ? victimUnit->GetName() : "?", Value::SurviveSec(e, *victim));
            else
                handler->PSendSysMessage("  enemy %s: %.1f melee dps, not hitting us", unit ? unit->GetName() : "?", e.meleeDps);
        }
        return true;
    }

    bool ShowControls(ChatHandler* handler, Player* player)
    {
        std::shared_ptr<PartyCommander> commander = sCommanderMgr.FindCommander(player);
        if (!commander)
        {
            handler->SendSysMessage("No commander for your party on this map.");
            return false;
        }

        WorldModel const& world = commander->GetWorld();
        Map* map = player->GetMap();
        auto name = [map](ObjectGuid guid) -> char const*
        {
            Unit* unit = map->GetUnit(guid);
            return unit ? unit->GetName() : "?";
        };
        FightLoad::Input fight;
        bool const haveFight = FightLoad::FromWorld(world, fight);
        if (haveFight)
            handler->PSendSysMessage("Fight as it is: %s", FightLoad::Describe(FightLoad::Simulate(fight)).c_str());
        else
            handler->SendSysMessage("No alive tank: values without the fight simulation.");

        for (EnemyFacts const& e : world.enemies)
            if (e.control.spellId)
                handler->PSendSysMessage("  %s is controlled: spell %u by %s, %.1f s left%s%s", name(e.guid),
                    e.control.spellId, name(e.control.caster), e.control.remainingMs / 1000.0f,
                    e.control.breaksOnDamage ? ", breaks on damage" : "", e.control.fear ? ", feared" : "");

        uint32 shown = 0;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.controls.empty())
                continue;
            for (ControlAbility const& a : m.controls)
            {
                SpellEntry const* spell = sSpellMgr.GetSpellEntry(a.spellId);
                handler->PSendSysMessage("%s: %s (%u)%s, %.0f s, %s%s%s", name(m.guid),
                    spell ? spell->SpellName[0].c_str() : "?", a.spellId, a.ready ? "" : " not ready",
                    a.durationSec, a.radius > 0.0f ? "area" : "single target",
                    a.breaksOnDamage ? ", breaks on damage" : "", a.fear ? ", fear" : "");
                // Sap needs a target out of combat: before a pull only (todo 10c).
                if (!a.ready || !haveFight || a.peacefulOnly)
                    continue;
                auto show = [&](EnemyFacts const* target)
                {
                    std::vector<EnemyFacts const*> const targets = ControlValue::Targets(world, m, a, target);
                    if (targets.empty())
                        return;
                    // Same aoe cost as CrowdControlAssigner.
                    float const aoe = (a.breaksOnDamage && target)
                        ? ControlValue::AreaLoss(world, *target, ControlValue::ExpectedSec(world, a, *target)) : 0.0f;
                    ControlValue::Worth const w = ControlValue::Of(world, fight, m, a, targets, aoe);
                    handler->PSendSysMessage("    %s%s: %s", target ? "on " : "", target ? name(target->guid) : "around",
                        ControlValue::Describe(w).c_str());
                };
                if (a.radius > 0.0f)
                    show(nullptr);
                else
                    for (EnemyFacts const& e : world.enemies)
                        if (!e.onAllyOnly && e.acting && ControlValue::Affects(a, e.entry, e.creatureTypeMask) && ++shown <= 30)
                            show(&e);
            }
        }
        return true;
    }

    bool ShowFlee(ChatHandler* handler, Player* player)
    {
        std::shared_ptr<PartyCommander> commander = sCommanderMgr.FindCommander(player);
        if (!commander)
        {
            handler->SendSysMessage("No commander for your party on this map.");
            return false;
        }

        WorldModel const& world = commander->GetWorld();
        Map* map = player->GetMap();
        auto name = [map](ObjectGuid guid) -> char const*
        {
            Unit* unit = map->GetUnit(guid);
            return unit ? unit->GetName() : "?";
        };
        uint32 shown = 0;
        for (EnemyFacts const& e : world.enemies)
        {
            FleeFacts const& f = e.flee;
            if (f.atPct <= 0.0f)
                continue;
            ++shown;
            char const* state = f.fleeing ? "FLEEING" : f.spent ? "already fled" : f.prevented ? "prevented" : "";
            handler->PSendSysMessage("%s: %.0f%% health, flees at %.0f%%, in ~%.1f s %s", name(e.guid),
                e.healthPct, f.atPct, f.secToFlee, state);
            if (!f.predicted)
                continue;
            std::string joiners;
            uint32 focused = 0;
            for (FleeJoiner const& j : f.joiners)
            {
                if (!joiners.empty())
                    joiners += ", ";
                char buf[64];
                snprintf(buf, sizeof(buf), "%s %.0f yd", name(j.guid), j.needYd);
                joiners += buf;
                if (j.needYd <= f.focusedReachYd)
                    ++focused;
            }
            handler->PSendSysMessage("  whole run %.0f yd: may bring %u (%s), costs %.1f points", f.reachYd,
                uint32(f.joiners.size()), joiners.empty() ? "nobody" : joiners.c_str(), f.points);
            handler->PSendSysMessage("  party on it: dies %.1f s into the run, %.0f yd: brings %u, costs %.1f (focus saves %.1f)",
                f.runKillSec, f.focusedReachYd, focused, f.focusedPoints, f.FocusSaves());
            for (PartyMemberFacts const& m : world.party)
                for (FleeStopAbility const& a : m.fleeStops)
                {
                    FleeStopValue::Worth const w = FleeStopValue::Of(world, m, a, e);
                    if (w.benefit <= 0.0f && !a.ready)
                        continue;
                    SpellEntry const* spell = sSpellMgr.GetSpellEntry(a.spellId);
                    handler->PSendSysMessage("    %s %s%s: %s", name(m.guid), spell ? spell->SpellName[0].c_str() : "?",
                        a.ready ? "" : " (not ready)", FleeStopValue::Describe(w).c_str());
                }
        }
        for (FleeStopDuty const& d : commander->GetPlan().fleeStops)
        {
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(d.spellId);
            handler->PSendSysMessage("Kept: %s holds %s for %s (%s, net %.1f)", name(d.bot),
                spell ? spell->SpellName[0].c_str() : "?", name(d.enemy),
                d.hold ? "stun / root once it dies within it" : "slow once it runs", d.points);
        }
        if (!shown)
            handler->SendSysMessage("No enemy of ours flees at low health.");
        return true;
    }

    void ReloadOverrides(ChatHandler* handler)
    {
        handler->SendSysMessage(sProfileStore.ReloadOverrides().c_str());
        // Cached cast values were computed from the old profiles (CastValue.cpp planning cache).
        CastValues::InvalidateCache();
    }

    void ShowDecisions(ChatHandler* handler, BotDirectives const& directives, uint32 count)
    {
        auto const& log = directives.GetDecisionLog();
        if (log.empty())
        {
            handler->SendSysMessage("  no arbiter decisions recorded");
            return;
        }

        uint32 const now = WorldTimer::getMSTime();
        uint32 shown = 0;
        for (auto itr = log.rbegin(); itr != log.rend() && shown < count; ++itr, ++shown)
        {
            handler->PSendSysMessage("  decision %u ms ago:", WorldTimer::getMSTimeDiff(itr->timeMs, now));
            for (CandidateRecord const& c : itr->candidates)
            {
                if (c.valued)
                    handler->PSendSysMessage("    %s %s: tier %u, benefit %.2f - cost %.2f = %.2f -> %s %s",
                        c.source.c_str(), c.what.c_str(), c.tier, c.benefit, c.cost, c.score, ResultName(c.result), c.reason.c_str());
                else
                    handler->PSendSysMessage("    %s %s: tier %u, score %.2f -> %s %s",
                        c.source.c_str(), c.what.c_str(), c.tier, c.score, ResultName(c.result), c.reason.c_str());
            }
        }
    }
}
}
