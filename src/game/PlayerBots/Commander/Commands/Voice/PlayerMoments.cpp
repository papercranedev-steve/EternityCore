/*
 * The party voice's moments about the human players themselves (PartyVoice.h; design
 * .kiro/docs/2026-11-28-player-voice.md, user 2026-11-28): somebody looks after him when he is
 * in trouble, bots thank him for what he did for them, and now and then one says he played well.
 *
 *  - Down and up: a human dies in a fight (not a wipe: its line says it); when the fight is
 *    over the bot that can raise him says so (still in his body: don't release; a ghost: come
 *    back to the corpse), or nobody can and the party waits; back on his feet near us later.
 *  - In the fight: the tank going for an enemy on a human who is not a tank; a human standing in
 *    an area on the ground or a pulse dangerous to him (HAZARD_SAY_MS).
 *  - His long buffs on bots, out of a fight, gathered into one thanks (BUFF_SETTLE_MS).
 *  - Praise, after a fight, one line at most and PRAISE_GAP_MS apart (praise is worth something
 *    because it is rare): his heal reached a bot below CLOSE_CALL_PCT that lived; his interrupt
 *    (NoteInterrupt, from the bots' SMSG_SPELLLOGEXECUTE) of an enemy the brain had an interrupt
 *    duty for, or of a heal; his control
 *    took an enemy off a non-tank or stopped a runner; he tanked or healed a boss with nobody
 *    dead (the boss kill line's variant).
 * Never blame: the lines say what to do and who helps, not what he did wrong.
 */

#include "Commander/Commands/Voice/PartyVoice.h"
#include "Commander/CommanderTypes.h"
#include "Commander/Perception/WorldModel.h"
#include "Commander/Perception/Tanks.h"
#include "Commander/Perception/Spatial/HazardZones.h"
#include "Map.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        constexpr uint32 BUFF_LOOK_MS = 1000;
        // A dead human not back near us by then: no "back" line.
        constexpr uint32 BACK_GIVE_UP_MS = 10 * MINUTE * IN_MILLISECONDS;
        constexpr uint32 BUFF_ONCE_MS = 20 * MINUTE * IN_MILLISECONDS;

        std::string G(ObjectGuid guid) { return std::to_string(guid.GetRawValue()); }

        bool IsHuman(PartyMemberFacts const& m) { return !m.isBot && !m.isPet; }

        // A healer bot that is up, else empty (the voice then picks the tank or anyone).
        ObjectGuid HealerBot(WorldModel const& world)
        {
            for (PartyMemberFacts const& m : world.party)
                if (m.alive && m.HealsTeam())
                    return m.guid;
            return ObjectGuid();
        }

        bool AnyBotUp(WorldModel const& world)
        {
            for (PartyMemberFacts const& m : world.party)
                if (m.isBot && !m.isPet && m.alive)
                    return true;
            return false;
        }

        float Distance(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    }

    void PartyVoice::PlayerMoments(Map* map, WorldModel const& world, TeamPlan const& plan, bool fighting, bool fightEnded,
                                   uint32 nowMs)
    {
        if (fighting && !m_fighting)
        {
            m_deathThisFight = false;
            m_savedBy.clear();
            m_controlPraiseBy.Clear();
            m_controlPraiseSpeaker.Clear();
            m_interruptPraiseBy.Clear();
            m_hazardSince.clear();
        }
        HumanDeaths(map, world, plan, fighting, fightEnded, nowMs);
        if (fighting)
            HumanInFight(map, world, plan, nowMs);
        if (fightEnded)
            Praise(map, world, nowMs);
        HumanBuffs(map, world, fighting, nowMs);

        m_interruptsSeen.clear();
        m_interruptNeeds.clear();
        for (InterruptDuty const& d : plan.interrupts)
            m_interruptNeeds.insert(d.enemy.GetRawValue());
    }

    void PartyVoice::NoteInterrupt(ObjectGuid caster, ObjectGuid target, uint32 spellId, uint32 nowMs)
    {
        for (SeenInterrupt const& s : m_interruptsSeen)
            if (s.caster == caster && s.target == target && s.spellId == spellId)
                return;
        if (m_interruptsSeen.size() >= 16)
            return;
        m_interruptsSeen.push_back({ caster, target, spellId, nowMs });
    }

    void PartyVoice::HumanDeaths(Map* map, WorldModel const& world, TeamPlan const& plan, bool fighting, bool fightEnded,
                                 uint32 nowMs)
    {
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.isPet)
                continue;
            auto const prev = m_members.find(m.guid.GetRawValue());
            bool const died = prev != m_members.end() && prev->second.alive && !m.alive;
            if (!died)
                continue;
            if (fighting || fightEnded)
                m_deathThisFight = true;
            if (!IsHuman(m))
                continue;
            m_humansDown[m.guid.GetRawValue()] = nowMs;
            // (a wipe: the wipe line says it)
            if (fighting && !plan.wipe.active)
                Say("voice.player.down", HealerBot(world), VoiceTier::Essential, nowMs, { { "name", MemberName(map, m.guid) } },
                    "pdown:" + G(m.guid), 30000);
        }

        // The fight is over and a human lies dead: who raises him, or that nobody can.
        if (fightEnded && AnyBotUp(world))
        {
            for (auto const& down : m_humansDown)
            {
                ObjectGuid const guid(down.first);
                PartyMemberFacts const* m = world.FindMember(guid);
                if (m && m->alive)
                    continue;
                if (m && m->selfRes)
                    continue;   // a Soulstone or Reincarnation: he gets up by himself
                CorpseFacts const* corpse = nullptr;
                for (CorpseFacts const& c : world.corpses)
                    if (c.guid == guid)
                        corpse = &c;
                if (!m && !corpse)
                    continue;
                Position3 const at = corpse ? corpse->pos : m->pos;
                bool const released = corpse ? corpse->released : false;

                PartyMemberFacts const* reviver = nullptr;
                float best = 0.0f;
                for (PartyMemberFacts const& b : world.party)
                {
                    if (!b.isBot || b.isPet || !b.alive || !b.resurrects)
                        continue;
                    float const d = Distance(b.pos, at);
                    if (!reviver || d < best)
                    {
                        reviver = &b;
                        best = d;
                    }
                }
                std::vector<std::pair<std::string, std::string>> const name = { { "name", MemberName(map, guid) } };
                if (reviver)
                    Say(released ? "voice.player.res.run" : "voice.player.res.wait", reviver->guid, VoiceTier::Essential, nowMs,
                        name, "pres:" + G(guid), 120000);
                else
                    Say("voice.player.res.none", ObjectGuid(), VoiceTier::Essential, nowMs, name, "pres:" + G(guid), 120000);
            }
        }

        // Back on his feet near us.
        for (auto itr = m_humansDown.begin(); itr != m_humansDown.end();)
        {
            ObjectGuid const guid(itr->first);
            PartyMemberFacts const* m = world.FindMember(guid);
            if (m && m->alive)
            {
                bool botClose = false;    // ("near" is a Windows macro)
                for (PartyMemberFacts const& b : world.party)
                    if (b.isBot && !b.isPet && b.alive && Distance(b.pos, m->pos) <= BACK_YD)
                        botClose = true;
                if (botClose)
                {
                    Say("voice.player.back", ObjectGuid(), VoiceTier::Flavor, nowMs, { { "name", MemberName(map, guid) } },
                        "pback:" + G(guid), 60000);
                    itr = m_humansDown.erase(itr);
                    continue;
                }
            }
            if (WorldTimer::getMSTimeDiff(itr->second, nowMs) > BACK_GIVE_UP_MS)
                itr = m_humansDown.erase(itr);
            else
                ++itr;
        }
    }

    void PartyVoice::HumanInFight(Map* map, WorldModel const& world, TeamPlan const& plan, uint32 nowMs)
    {
        std::string const fight = std::to_string(m_fightStartMs);

        // The tank goes for an enemy on a human who does not hold enemies himself.
        for (RescueDuty const& d : plan.rescues)
        {
            if (d.way != RescueWay::Taunt && d.way != RescueWay::Threat)
                continue;
            PartyMemberFacts const* victim = world.FindMember(d.victim);
            if (!victim || !IsHuman(*victim) || !victim->alive || (victim->functions & FUNCTION_HOLD))
                continue;
            Say("voice.player.rescue", d.bot, VoiceTier::Essential, nowMs, { { "name", MemberName(map, victim->guid) } },
                "prescue:" + G(victim->guid) + ":" + fight, 10 * MINUTE * IN_MILLISECONDS);
        }

        // A human standing in an area that stays there (on the ground, a pulse) and hurts him.
        std::vector<HazardZone> const& zones = HazardZones::Get(world);
        std::map<std::string, uint32> standing;
        for (PartyMemberFacts const& m : world.party)
        {
            if (!IsHuman(m) || !m.alive)
                continue;
            uint8 roleBit = HazardZones::RoleBit(m.role);
            if (!roleBit)
                roleBit = HAZARD_HEALER | HAZARD_RANGED | HAZARD_MELEE;
            for (HazardZone const& z : zones)
            {
                if ((z.kind != HazardKind::Ground && z.kind != HazardKind::Pulse) || z.damage <= 0.0f ||
                    !z.DangerousTo(roleBit) || !HazardZones::Covers(z, m.pos))
                    continue;
                std::string const key = G(m.guid) + ":" + G(z.enemy) + ":" + std::to_string(z.hitSpellId);
                auto const was = m_hazardSince.find(key);
                uint32 const since = was != m_hazardSince.end() ? was->second : nowMs;
                standing[key] = since;
                if (WorldTimer::getMSTimeDiff(since, nowMs) >= HAZARD_SAY_MS)
                    Say("voice.player.hazard", HealerBot(world), VoiceTier::Urgent, nowMs, { { "name", MemberName(map, m.guid) } },
                        "phz:" + key + ":" + fight, 10 * MINUTE * IN_MILLISECONDS);
            }
        }
        m_hazardSince.swap(standing);

        // Praise to come: a human's heal on a bot near death.
        for (PartyMemberFacts const& b : world.party)
        {
            if (!b.isBot || b.isPet || !b.alive || b.healthPct >= CLOSE_CALL_PCT)
                continue;
            for (PartyMemberFacts const& h : world.party)
                if (IsHuman(h) && h.alive && h.pendingHeal.target == b.guid)
                    m_savedBy[b.guid.GetRawValue()] = h.guid;
        }

        // ... a human's interrupt of a cast that mattered: the brain wanted that enemy stopped,
        // or it was a heal.
        for (SeenInterrupt const& seen : m_interruptsSeen)
        {
            if (!m_interruptPraiseBy.IsEmpty())
                break;
            PartyMemberFacts const* caster = world.FindMember(seen.caster);
            if (!caster || !IsHuman(*caster))
                continue;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(seen.spellId);
            bool const heal = spell && (spell->HasEffect(SPELL_EFFECT_HEAL) || spell->HasEffect(SPELL_EFFECT_HEAL_MAX_HEALTH));
            if (heal || m_interruptNeeds.count(seen.target.GetRawValue()))
                m_interruptPraiseBy = caster->guid;
        }

        // ... a human's control on an enemy that hit a non-tank, or ran for help.
        if (m_controlPraiseBy.IsEmpty())
        {
            for (EnemyFacts const& e : world.enemies)
            {
                if (!e.control.spellId || e.control.landing)
                    continue;
                PartyMemberFacts const* caster = world.FindMember(e.control.caster);
                if (!caster || !IsHuman(*caster))
                    continue;
                auto const prevItr = m_enemies.find(e.guid.GetRawValue());
                if (prevItr == m_enemies.end() || prevItr->second.controlSpell == e.control.spellId)
                    continue;   // not new this refresh
                EnemyState const& prev = prevItr->second;
                PartyMemberFacts const* victim = world.FindMember(prev.victim);
                bool const offFriend = victim && !victim->isPet && victim->guid != caster->guid && !Tanks::IsTank(*victim);
                if (!offFriend && !prev.fleeing)
                    continue;
                m_controlPraiseBy = caster->guid;
                m_controlPraiseSpeaker = offFriend && victim->isBot ? victim->guid : ObjectGuid();
                break;
            }
        }
    }

    void PartyVoice::Praise(Map* map, WorldModel const& world, uint32 nowMs)
    {
        if (!AnyBotUp(world))
            return;
        if (m_lastPraiseMs && WorldTimer::getMSTimeDiff(m_lastPraiseMs, nowMs) < PRAISE_GAP_MS)
            return;
        std::string const fight = std::to_string(m_fightStartMs);

        // A bot a human healed back from near death thanks him.
        for (auto const& saved : m_savedBy)
        {
            PartyMemberFacts const* b = world.FindMember(ObjectGuid(saved.first));
            if (!b || !b->alive)
                continue;
            m_lastPraiseMs = nowMs;
            Say("voice.player.saved_me", b->guid, VoiceTier::Social, nowMs, { { "name", MemberName(map, saved.second) } },
                "praise:" + fight);
            return;
        }

        if (!m_interruptPraiseBy.IsEmpty())
        {
            PartyMemberFacts const* tank = Tanks::Main(world, true);
            m_lastPraiseMs = nowMs;
            Say("voice.player.interrupt", tank && tank->alive ? tank->guid : ObjectGuid(), VoiceTier::Social, nowMs,
                { { "name", MemberName(map, m_interruptPraiseBy) } }, "praise:" + fight);
            return;
        }

        if (!m_controlPraiseBy.IsEmpty())
        {
            m_lastPraiseMs = nowMs;
            Say("voice.player.control", m_controlPraiseSpeaker, VoiceTier::Social, nowMs,
                { { "name", MemberName(map, m_controlPraiseBy) } }, "praise:" + fight);
            return;
        }

        // A boss the human held or healed, nobody dead: the boss kill line names him
        // (MemberMoments says the plain one when m_bossDied is still set).
        if (m_bossDied && !m_deathThisFight)
        {
            PartyMemberFacts const* tank = nullptr;
            PartyMemberFacts const* healer = nullptr;
            for (PartyMemberFacts const& m : world.party)
            {
                if (!IsHuman(m) || !m.alive)
                    continue;
                if (!tank && (m.functions & FUNCTION_HOLD))
                    tank = &m;
                else if (!healer && (m.functions & FUNCTION_HEAL))
                    healer = &m;
            }
            if (tank || healer)
            {
                m_lastPraiseMs = nowMs;
                m_bossDied = false;
                Say(tank ? "voice.boss.kill.player_tank" : "voice.boss.kill.player_healer", ObjectGuid(), VoiceTier::Social, nowMs,
                    { { "name", MemberName(map, (tank ? tank : healer)->guid) } }, "bosskill:" + fight);
            }
        }
    }

    void PartyVoice::HumanBuffs(Map* map, WorldModel const& world, bool fighting, uint32 nowMs)
    {
        // Gathered news: one line for the round.
        if (m_buffNewsMs && !fighting && WorldTimer::getMSTimeDiff(m_buffNewsMs, nowMs) >= BUFF_SETTLE_MS)
        {
            if (!m_buffedBots.empty())
            {
                ObjectGuid const speaker = m_buffedBots[urand(0, uint32(m_buffedBots.size()) - 1)];
                Say("voice.player.buffed", speaker, VoiceTier::Social, nowMs, { { "name", MemberName(map, m_buffBy) } },
                    "pbuff:" + G(m_buffBy), BUFF_ONCE_MS);
            }
            m_buffNewsMs = 0;
            m_buffedBots.clear();
            m_buffBy.Clear();
        }
        if (fighting || nowMs < m_nextBuffLookMs)
            return;
        m_nextBuffLookMs = nowMs + BUFF_LOOK_MS;

        std::set<std::pair<uint64, uint32>> now;
        for (PartyMemberFacts const& b : world.party)
        {
            if (!b.isBot || b.isPet || !b.alive)
                continue;
            Player const* p = map->GetPlayer(b.guid);
            if (!p)
                continue;
            for (auto const& entry : p->GetSpellAuraHolderMap())
            {
                SpellAuraHolder const* holder = entry.second;
                if (!holder || !holder->IsPositive() || holder->GetAuraMaxDuration() < int32(BUFF_MIN_MS))
                    continue;
                PartyMemberFacts const* caster = world.FindMember(holder->GetCasterGuid());
                if (!caster || !IsHuman(*caster))
                    continue;
                std::pair<uint64, uint32> const key(b.guid.GetRawValue(), holder->GetId());
                now.insert(key);
                if (!m_humanBuffsSeen || m_humanBuffs.count(key))
                    continue;
                // New since the last look.
                if (!m_buffNewsMs)
                {
                    m_buffNewsMs = nowMs;
                    m_buffBy = caster->guid;
                }
                if (caster->guid == m_buffBy &&
                    std::find(m_buffedBots.begin(), m_buffedBots.end(), b.guid) == m_buffedBots.end())
                    m_buffedBots.push_back(b.guid);
            }
        }
        m_humanBuffs.swap(now);
        m_humanBuffsSeen = true;
    }
}
