/*
 * The party voice's moments (PartyVoice.h): what changed since the last refresh that the
 * players want to hear about. Keys are PlayerBots/i18n "voice.*".
 */

#include "Commander/Commands/Voice/PartyVoice.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Coordination/Situation/Pull/PullPlanner.h"
#include "Map.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>
#include <cstring>

namespace Commander
{
    namespace
    {
        // A pull waits this long before the party says who it waits for.
        constexpr uint32 WAIT_SAY_MS = 6000;
        // An enemy hitting a healer this long: the healer calls for help.
        constexpr uint32 HEALER_HIT_MS = 1500;
        constexpr float OOM_PCT = 20.0f;
        constexpr float OOM_EMPTY_PCT = 6.0f;
        // A control breaking with this much time left was broken by someone.
        constexpr uint32 BROKE_LEFT_MS = 3000;
        constexpr float CLOSE_CALL_PCT = 15.0f;
        // After arriving on a map: everyone came through the door.
        constexpr uint32 ENTER_SAY_MS = 8000;

        std::string G(ObjectGuid guid) { return std::to_string(guid.GetRawValue()); }

        // The bot behind a control (a bot, or the owner of a bot's pet), empty otherwise.
        ObjectGuid BotCaster(WorldModel const& world, ObjectGuid caster)
        {
            PartyMemberFacts const* m = world.FindMember(caster);
            if (m && m->isPet)
                m = world.FindMember(m->ownerGuid);
            return m && m->isBot ? m->guid : ObjectGuid();
        }

        // A healer bot that is not also the one holding the enemies (a lone bot doing it all).
        bool IsHealerOnly(WorldModel const& world, PartyMemberFacts const& m)
        {
            if (!m.HealsTeam() || m.isPet)
                return false;
            PartyMemberFacts const* tank = Tanks::Main(world);
            return !tank || tank->guid != m.guid;
        }
    }

    void PartyVoice::FindMoments(Map* map, WorldModel const& world, TeamPlan const& plan, Player const* human,
                                 VoiceSettings const& settings, uint32 nowMs)
    {
        if (!m_seen)
        {
            m_seen = true;
            m_firstMs = nowMs;
            Remember(world, plan);
            return;
        }
        bool const fighting = world.HasPartyEnemy();
        bool const fightEnded = m_fighting && !fighting;
        if (fighting && !m_fighting)
        {
            m_fightStartMs = nowMs;
            m_bossDied = false;
        }

        PullMoments(map, world, plan, human, nowMs);
        FightMoments(map, world, plan, human, nowMs);
        MemberMoments(map, world, fighting, fightEnded, nowMs);
        if (fightEnded)
            m_lastFightEndMs = nowMs;
        QuietMoments(world, plan, settings, fighting, nowMs);
        Remember(world, plan);
    }

    void PartyVoice::PullMoments(Map* map, WorldModel const& world, TeamPlan const& plan, Player const* human, uint32 nowMs)
    {
        PullPlan const& pull = plan.pull;
        if (pull.phase == PullPhase::None)
            return;
        std::string const pullKey = std::to_string(pull.plannedAtMs);
        if (m_pullPhase == PullPhase::None)
            m_waitChecked = false;

        // Waiting: who for, once per pull (the bot itself says it needs to drink).
        if (pull.phase == PullPhase::WaitReady && !m_waitChecked &&
            WorldTimer::getMSTimeDiff(pull.phaseSinceMs, nowMs) >= WAIT_SAY_MS)
        {
            m_waitChecked = true;
            Position3 planned;
            bool const hasPlanned = plan.PlannedFightSpot(planned);
            uint32 said = 0;
            for (PullPlanner::Issue const& issue : PullPlanner::CheckReady(world, pull.healerManaPct, hasPlanned ? &planned : nullptr))
            {
                PartyMemberFacts const* m = world.FindMember(issue.who);
                if (!m || m->isPet || said >= 2)
                    continue;
                std::string const what = issue.what;
                std::string const once = "wait:" + what + ":" + G(m->guid);
                std::vector<std::pair<std::string, std::string>> const name = { { "name", MemberName(map, m->guid) } };
                if (what == "dead")
                    Say("voice.pull.wait.dead", ObjectGuid(), VoiceTier::Essential, nowMs, name, once);
                else if (m->isBot && what == "mana")
                    Say("voice.pull.wait.mana", m->guid, VoiceTier::Essential, nowMs, {}, once);
                else if (m->isBot && what == "hp")
                    Say("voice.pull.wait.hp", m->guid, VoiceTier::Essential, nowMs, {}, once);
                else if (!m->isBot && what == "far")
                    Say("voice.pull.wait.far", ObjectGuid(), VoiceTier::Essential, nowMs, name, once);
                else if (!m->isBot && (what == "hp" || what == "mana"))
                    Say("voice.pull.wait.player", ObjectGuid(), VoiceTier::Essential, nowMs, name, once);
                else
                    continue;
                ++said;
            }
        }

        // Held for a patrol walking by.
        if (!pull.patrolWaitingFor.IsEmpty() && pull.patrolWaitingFor != m_patrolWait)
            Say("voice.pull.patrol", pull.tank, VoiceTier::Essential, nowMs, {}, "patrolwait:" + G(pull.patrolWaitingFor), 120000);

        // Going: the tank tells the party (a corner fight: where to stand).
        if (pull.phase == PullPhase::Approach && (m_pullPhase == PullPhase::None || m_pullPhase == PullPhase::WaitReady))
            Say(pull.corner ? "voice.pull.go.corner" : "voice.pull.go", pull.tank, VoiceTier::Essential, nowMs, {},
                "go:" + pullKey, 120000);

        // The opening control: the controller says what he takes (its mark is on already).
        if (pull.control.Active() && pull.control.sinceMs != m_pullControlSince)
            Say("voice.pull.control", pull.control.bot, VoiceTier::Essential, nowMs,
                { { "target", EnemyName(map, pull.control.target, human) }, { "cc", ControlWord(human, pull.control.spellId) } },
                "pc:" + G(pull.control.target));

        // Line of sight pull: the tank steps out of the casters' sight.
        if (pull.losBreak == PullLosBreak::Active && m_losBreak != PullLosBreak::Active)
            Say("voice.pull.los", pull.tank, VoiceTier::Essential, nowMs, {}, "los:" + pullKey, 120000);
    }

    void PartyVoice::FightMoments(Map* map, WorldModel const& world, TeamPlan const& plan, Player const* human, uint32 nowMs)
    {
        for (EnemyFacts const& e : world.enemies)
        {
            if (e.onAllyOnly)
                continue;
            auto const prevItr = m_enemies.find(e.guid.GetRawValue());
            EnemyState const* prev = prevItr != m_enemies.end() ? &prevItr->second : nullptr;
            std::string const g = G(e.guid);

            // A bot's control landed (the opening control was said before the pull).
            if (e.control.spellId && !e.control.landing && (!prev || prev->controlSpell != e.control.spellId))
            {
                ObjectGuid const caster = BotCaster(world, e.control.caster);
                auto const opening = m_said.find("pc:" + g);
                bool const announced = opening != m_said.end() && WorldTimer::getMSTimeDiff(opening->second, nowMs) < 60000;
                if (!caster.IsEmpty() && !announced)
                    Say("voice.cc.on", caster, VoiceTier::Essential, nowMs,
                        { { "target", EnemyName(map, e.guid, human) }, { "cc", ControlWord(human, e.control.spellId) } },
                        "cc:" + g, 30000);
            }
            // ... and was broken early (someone hit it).
            if (prev && prev->controlSpell && !e.control.spellId && prev->breaksOnDamage && prev->controlLeftMs > BROKE_LEFT_MS)
            {
                ObjectGuid const caster = BotCaster(world, prev->controlCaster);
                if (!caster.IsEmpty())
                    Say("voice.cc.broke", caster, VoiceTier::Essential, nowMs, { { "target", EnemyName(map, e.guid, human) } },
                        "ccb:" + g, 20000);
            }

            // On a healer: the healer calls it.
            if (!e.crowdControlled && !e.victim.IsEmpty() && e.victimSinceMs &&
                WorldTimer::getMSTimeDiff(e.victimSinceMs, nowMs) >= HEALER_HIT_MS)
            {
                PartyMemberFacts const* victim = world.FindMember(e.victim);
                if (victim && victim->alive && IsHealerOnly(world, *victim))
                    Say("voice.fight.healer_hit", victim->guid, VoiceTier::Urgent, nowMs,
                        { { "target", EnemyName(map, e.guid, human) } }, "hh:" + g, 30000);
            }

            // Running for help: who is to stop it says so.
            if (e.flee.fleeing && (!prev || !prev->fleeing))
            {
                ObjectGuid speaker;
                for (FleeStopDuty const& d : plan.fleeStops)
                    if (d.enemy == e.guid)
                        speaker = d.bot;
                Say("voice.fight.runner", speaker, VoiceTier::Urgent, nowMs, { { "target", EnemyName(map, e.guid, human) } },
                    "run:" + g, 20000);
            }
        }

        // A boss left the lists dead (said when the fight is over).
        for (auto const& entry : m_enemies)
        {
            if (!entry.second.boss)
                continue;
            ObjectGuid const guid(entry.first);
            if (world.FindEnemy(guid))
                continue;
            Unit const* unit = map->GetUnit(guid);
            if (unit && !unit->IsAlive())
                m_bossDied = true;
        }

        bool const fighting = world.HasPartyEnemy();
        if (fighting)
        {
            for (PartyMemberFacts const& m : world.party)
            {
                if (!m.alive || m.manaPct < 0.0f || !IsHealerOnly(world, m))
                    continue;
                if (m.manaPct < OOM_EMPTY_PCT)
                    Say("voice.fight.oom.empty", m.guid, VoiceTier::Urgent, nowMs, {}, "oom0:" + G(m.guid), 90000);
                else if (m.manaPct < OOM_PCT)
                    Say("voice.fight.oom", m.guid, VoiceTier::Urgent, nowMs, {}, "oom:" + G(m.guid), 90000);
            }
        }

        // A patrol walked into the fight.
        std::vector<ObjectGuid> const& announced = plan.patrolGuard.announced;
        if (announced.size() > m_patrolsAnnounced)
            Say("voice.fight.patrol", ObjectGuid(), VoiceTier::Essential, nowMs, {}, "patrol:" + G(announced.back()), 120000);

        if (plan.retreat.active && !m_retreat)
        {
            Player const* leader = map->GetPlayer(world.leader);
            Say(plan.retreat.automatic ? "voice.fight.retreat.follow" : "voice.fight.retreat", ObjectGuid(), VoiceTier::Urgent,
                nowMs, { { "name", leader ? leader->GetName() : "" } }, "retreat", 20000);
        }
        if (plan.retreat.adviceSent && !m_retreatAdvice)
            Say("voice.fight.danger", ObjectGuid(), VoiceTier::Urgent, nowMs, {}, "danger", 60000);

        if (plan.wipe.active && !m_wipe)
        {
            Say("voice.fight.wipe", ObjectGuid(), VoiceTier::Urgent, nowMs, {}, "wipe", 60000);
            for (WipeHider const& h : plan.wipe.hiders)
                Say("voice.fight.wipe.hide", h.bot, VoiceTier::Essential, nowMs, {}, "hide:" + G(h.bot), 60000);
        }

        uint8 const lost = plan.crisis.lost;
        if ((lost & CRISIS_TANK) && !(m_crisisLost & CRISIS_TANK))
        {
            Say("voice.fight.tank_down", ObjectGuid(), VoiceTier::Urgent, nowMs, {}, "tankdown", 60000);
            if (plan.crisis.best >= 0 && size_t(plan.crisis.best) < plan.crisis.options.size())
            {
                ObjectGuid const standIn = plan.crisis.options[plan.crisis.best].standIn;
                if (!standIn.IsEmpty())
                    Say("voice.fight.standin", standIn, VoiceTier::Urgent, nowMs, {}, "standin:" + G(standIn), 60000);
            }
        }
        if ((lost & CRISIS_HEALER) && !(m_crisisLost & CRISIS_HEALER))
            Say("voice.fight.healer_down", ObjectGuid(), VoiceTier::Urgent, nowMs, {}, "healerdown", 60000);
    }

    void PartyVoice::MemberMoments(Map* map, WorldModel const& world, bool fighting, bool fightEnded, uint32 nowMs)
    {
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            auto const itr = m_members.find(m.guid.GetRawValue());
            if (itr == m_members.end())
                continue;
            MemberState const& prev = itr->second;
            std::string const g = G(m.guid);
            if (m.isBot && prev.alive && !m.alive)
                Say("voice.death.self", m.guid, VoiceTier::Essential, nowMs, {}, "dead:" + g, 30000);
            if (m.isBot && !prev.alive && m.alive)
                Say("voice.death.thanks", m.guid, VoiceTier::Social, nowMs, {}, "risen:" + g, 30000);
            if (prev.level && m.level > prev.level)
            {
                std::string const once = "lvl:" + g + ":" + std::to_string(m.level);
                if (m.isBot)
                    Say("voice.levelup.self", m.guid, VoiceTier::Social, nowMs, { { "level", std::to_string(m.level) } }, once, 3600000);
                else if (fighting)
                    m_pendingLevelUps.push_back(m.guid);
                else
                    Say("voice.levelup.player", ObjectGuid(), VoiceTier::Social, nowMs,
                        { { "name", MemberName(map, m.guid) }, { "level", std::to_string(m.level) } }, once, 3600000);
            }
        }

        if (!fightEnded)
            return;
        bool anyAlive = false, anyRes = false;
        ObjectGuid healer;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet || !m.alive)
                continue;
            anyAlive = true;
            anyRes = anyRes || m.resurrects;
            if (m.isBot && m.HealsTeam())
                healer = m.guid;
        }
        for (PartyMemberFacts const& m : world.party)
            if (!m.isPet && !m.alive && m.isBot && anyRes)
                Say("voice.death.ask_res", m.guid, VoiceTier::Essential, nowMs, {}, "askres:" + G(m.guid), 120000);
        if (anyAlive)
        {
            if (m_bossDied)
                Say("voice.boss.kill", ObjectGuid(), VoiceTier::Social, nowMs, {}, "bosskill:" + std::to_string(m_fightStartMs));
            for (PartyMemberFacts const& m : world.party)
            {
                if (m.isPet || m.isBot || !m.alive)
                    continue;
                auto const itr = m_members.find(m.guid.GetRawValue());
                if (itr != m_members.end() && itr->second.lowestPct < CLOSE_CALL_PCT)
                    Say("voice.fight.close_call", healer, VoiceTier::Flavor, nowMs, { { "name", MemberName(map, m.guid) } },
                        "close:" + G(m.guid), 300000);
            }
        }
        for (ObjectGuid const& guid : m_pendingLevelUps)
            if (PartyMemberFacts const* m = world.FindMember(guid))
                Say("voice.levelup.player", ObjectGuid(), VoiceTier::Social, nowMs,
                    { { "name", MemberName(map, guid) }, { "level", std::to_string(m->level) } },
                    "lvl:" + G(guid) + ":" + std::to_string(m->level), 3600000);
        m_pendingLevelUps.clear();
    }

    void PartyVoice::QuietMoments(WorldModel const& world, TeamPlan const& plan, VoiceSettings const& settings, bool fighting,
                                  uint32 nowMs)
    {
        if (fighting || plan.pull.phase != PullPhase::None || plan.retreat.active)
            return;

        // Arrived in a dungeon or raid, once everyone came through the door.
        if (!m_entered && world.mapKind != MapKind::World && WorldTimer::getMSTimeDiff(m_firstMs, nowMs) >= ENTER_SAY_MS)
        {
            m_entered = true;
            Say(world.mapKind == MapKind::Raid ? "voice.enter.raid" : "voice.enter.dungeon", ObjectGuid(), VoiceTier::Flavor,
                nowMs, {}, "enter", 30 * MINUTE * IN_MILLISECONDS);
        }

        // Long quiet: a few words (a quiet party never).
        if (settings.level == VoiceLevel::Quiet)
            return;
        uint32 const idleMs = settings.level == VoiceLevel::Chatty ? IDLE_CHATTY_MS : IDLE_NORMAL_MS;
        uint32 const lastMs = std::max({ m_lastLineMs, m_lastFightEndMs, m_firstMs });
        if (WorldTimer::getMSTimeDiff(lastMs, nowMs) >= idleMs)
            Say("voice.idle", ObjectGuid(), VoiceTier::Flavor, nowMs, {}, "idle", idleMs);
    }

    void PartyVoice::Remember(WorldModel const& world, TeamPlan const& plan)
    {
        bool const fighting = world.HasPartyEnemy();
        m_fighting = fighting;
        m_pullPhase = plan.pull.phase;
        m_pullControlSince = plan.pull.control.Active() ? plan.pull.control.sinceMs : 0;
        m_losBreak = plan.pull.losBreak;
        m_patrolWait = plan.pull.patrolWaitingFor;
        m_retreat = plan.retreat.active;
        m_retreatAdvice = plan.retreat.adviceSent;
        m_wipe = plan.wipe.active;
        m_crisisLost = plan.crisis.lost;
        m_patrolsAnnounced = plan.patrolGuard.announced.size();

        std::map<uint64, MemberState> members;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            MemberState s;
            auto const itr = m_members.find(m.guid.GetRawValue());
            float const before = itr != m_members.end() ? itr->second.lowestPct : 100.0f;
            s.alive = m.alive;
            s.level = m.level;
            s.lowestPct = fighting && m.alive ? std::min(before, m.healthPct) : (fighting ? before : 100.0f);
            members[m.guid.GetRawValue()] = s;
        }
        m_members.swap(members);

        std::map<uint64, EnemyState> enemies;
        for (EnemyFacts const& e : world.enemies)
        {
            EnemyState s;
            s.controlSpell = e.control.landing ? 0 : e.control.spellId;
            s.controlCaster = e.control.caster;
            s.controlLeftMs = e.control.remainingMs;
            s.breaksOnDamage = e.control.breaksOnDamage;
            s.fleeing = e.flee.fleeing;
            s.boss = e.boss;
            enemies[e.guid.GetRawValue()] = s;
        }
        m_enemies.swap(enemies);
    }
}
