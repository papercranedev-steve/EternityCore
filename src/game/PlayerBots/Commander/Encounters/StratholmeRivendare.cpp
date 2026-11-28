/*
 * Baron Rivendare module (header comment: script, sealed room, limits).
 */

#include "Commander/Encounters/StratholmeRivendare.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "TeamBotAI.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "GridSearchers.h"
#include "Map.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <list>

namespace Commander
{
    namespace
    {
        // The aura's pulse radius from the spell data (30 yd).
        float AuraRadius()
        {
            SpellEntry const* pulse = sSpellMgr.GetSpellEntry(StratholmeRivendare::SPELL_UNHOLY_AURA_PULSE);
            float const r = pulse ? Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(pulse->EffectRadiusIndex[0])) : 0.0f;
            return r > 0.0f ? r : StratholmeRivendare::AURA_FALLBACK_YD;
        }
    }

    SealedRoom StratholmeRivendare::Room()
    {
        SealedRoom room;
        room.active = true;
        room.door.x = GATE_X;
        room.door.y = GATE_Y;
        room.door.z = GATE_Z;
        room.boss.x = HOME_X;
        room.boss.y = HOME_Y;
        room.boss.z = HOME_Z;
        room.pullYd = AuraRadius();
        return room;
    }

    bool StratholmeRivendare::Matches(WorldModel const& world, uint32 mapId) const
    {
        return mapId == MAP_ID && EncounterHelpers::HasEnemyEntry(world, NPC_RIVENDARE);
    }

    void StratholmeRivendare::OnReset()
    {
        // (the gate's guid stays: same instance)
        m_lockedOutSaid = false;
    }

    void StratholmeRivendare::BeforePull(WorldModel const& world, std::vector<BossPrep>& out) const
    {
        for (PackFacts const& pack : world.packs)
            for (PackCreature const& c : pack.members)
                if (c.entry == NPC_RIVENDARE)
                {
                    BossPrep prep;
                    prep.boss = c.guid;
                    prep.bossPos = c.pos;
                    prep.seal = Room();
                    out.push_back(prep);
                    return;
                }
    }

    void StratholmeRivendare::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        if (m_lockedOutSaid || !bot || !bot->me->IsInWorld())
            return;
        EnemyFacts const* baron = EncounterHelpers::FindEnemyByEntry(world, NPC_RIVENDARE);
        if (!baron)
            return;

        Map* map = bot->me->GetMap();
        if (m_gate.IsEmpty())
        {
            uint32 const now = WorldTimer::getMSTime();
            uint32 const retryMs = GATE_SEARCH_RETRY_MS;
            if (m_lastSearchMs && WorldTimer::getMSTimeDiff(m_lastSearchMs, now) < retryMs)
                return;
            m_lastSearchMs = now;
            std::list<GameObject*> found;
            GetGameObjectListWithEntryInGrid(found, bot->me, GO_GATE, GATE_SEARCH_YD);
            if (found.empty())
                return;
            m_gate = found.front()->GetObjectGuid();
        }
        GameObject const* gate = map->GetGameObject(m_gate);
        if (!gate || gate->GetGoState() != GO_STATE_READY)     // READY = shut
            return;

        SealedRoom const room = Room();
        for (PartyMemberFacts const& m : world.party)
        {
            if (!m.alive || m.isPet || room.Inside(m.pos) || room.DoorDistance(m.pos) > LOCKED_OUT_YD)
                continue;
            m_lockedOutSaid = true;
            Say(EncounterHelpers::Line("voice.boss.sealed.locked_out", VoiceTier::Essential,
                                       m.isBot ? m.guid : ObjectGuid()));
            Note("Rivendare's gate shut with a member outside: it waits at the gate until the fight is over.");
            return;
        }
    }
}
