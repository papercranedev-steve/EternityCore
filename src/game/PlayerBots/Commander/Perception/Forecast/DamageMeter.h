/*
 * PartyBot Commander layer 1 (eyes): how much damage each team member deals, as the player's
 * damage meter shows it (DPS pace doc D1, .kiro/docs/2026-11-28-dps-pace.md).
 *
 * Fed by the bots' combat log packets (TeamBotAI::OnDamageSeen: SMSG_ATTACKERSTATEUPDATE,
 * SMSG_SPELLNONMELEEDAMAGELOG, SMSG_PERIODICAURALOG; the server already sends them to every
 * bot in sight). Pets count for their owner. Every bot reports its own damage; a real
 * player's is reported by one bot only (the team's lowest guid bot on the map), so nothing is
 * counted twice.
 *
 * A fight runs from the first damage or the first refresh with a party enemy until a refresh
 * without one, at least END_QUIET_MS after the last damage. Fights of MIN_FIGHT_SEC and more
 * are kept, the last KEEP_FIGHTS. Per member:
 *   dps        damage of the kept fights / their seconds (fights it was in the team for)
 *   areaShare  of that damage, the share from area spells (SpellEntry::IsAreaOfEffectSpell)
 * The fight going on is shown apart (WorldModel::damage.current*).
 * Facts only. Map thread of the team, one per PartyCommander.
 */
#ifndef MANGOS_COMMANDER_DAMAGE_METER_H
#define MANGOS_COMMANDER_DAMAGE_METER_H

#include "Common.h"
#include "ObjectGuid.h"
#include <deque>
#include <vector>

namespace Commander
{
    struct WorldModel;

    struct MemberDamage
    {
        ObjectGuid guid;
        float dps = 0.0f;           // kept fights
        float areaShare = 0.0f;     // 0..1
        uint32 fights = 0;          // kept fights it was in
        uint32 currentDamage = 0;   // the fight going on
    };

    struct DamageMeterFacts
    {
        std::vector<MemberDamage> members;
        uint32 keptTotal = 0;       // fights kept since the start (+1 each: a new one is in)
        bool fighting = false;
        float currentSec = 0.0f;
        MemberDamage const* Find(ObjectGuid guid) const
        {
            for (MemberDamage const& m : members)
                if (m.guid == guid)
                    return &m;
            return nullptr;
        }
    };

    class DamageMeter
    {
    public:
        static constexpr float MIN_FIGHT_SEC = 15.0f;
        static constexpr size_t KEEP_FIGHTS = 5;
        static constexpr uint32 END_QUIET_MS = 1500;

        // `member`: the team member (pets already mapped to their owner).
        void Note(ObjectGuid member, uint32 damage, bool area, uint32 nowMs);
        // Each refresh: ends the fight, writes WorldModel::damage.
        void Annotate(WorldModel& world, uint32 nowMs);

    private:
        struct Entry
        {
            ObjectGuid guid;
            uint32 damage = 0;
            uint32 area = 0;
        };
        struct Fight
        {
            uint32 startMs = 0;
            uint32 lastMs = 0;
            float sec = 0.0f;
            std::vector<Entry> entries;
            Entry& Of(ObjectGuid guid);
        };
        bool m_active = false;
        uint32 m_keptTotal = 0;
        Fight m_current;
        std::deque<Fight> m_kept;   // newest first
    };
}

#endif
