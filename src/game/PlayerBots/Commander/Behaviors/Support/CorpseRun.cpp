#include "Commander/Behaviors/Support/CorpseRun.h"
#include "Commander/Behaviors/Movement/Arrival.h"
#include "Commander/Perception/Spatial/TravelTime.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "TeamBotAI.h"
#include "Player.h"
#include "Corpse.h"
#include "Group.h"
#include "Map.h"
#include "GridMap.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "SQLStorages.h"
#include "Timer.h"
#include <algorithm>
#include <ctime>
#include <list>

namespace Commander
{
namespace CorpseRun
{
    namespace
    {
        bool HasSelfRes(Player const* me)
        {
#if SUPPORTED_CLIENT_BUILD >= CLIENT_BUILD_1_6_1
            return me->GetUInt32Value(PLAYER_SELF_RES_SPELL) != 0;
#else
            return me->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_CAN_SELF_RESURRECT);
#endif
        }

        // As WorldSession::HandleSelfResOpcode (the release dialog's Soulstone / Reincarnation button).
        void UseSelfRes(Player* me)
        {
#if SUPPORTED_CLIENT_BUILD >= CLIENT_BUILD_1_6_1
            if (SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(me->GetUInt32Value(PLAYER_SELF_RES_SPELL)))
                me->CastSpell(me, spellInfo, false);
            me->SetUInt32Value(PLAYER_SELF_RES_SPELL, 0);
#else
            if (SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(me->GetResurrectionSpellId()))
                me->CastSpell(me, spellInfo, false);
            me->SetResurrectionSpellId(0);
            me->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_CAN_SELF_RESURRECT);
#endif
        }

        enum class RiseCheck : uint8
        {
            Safe,
            Fight,      // a member or an enemy near the body still fights: wait
            Idle,       // an idle enemy would see it rise (back home after a wipe): wait, then give up
        };

        // Rising now would not put the bot back into the fight: no member fights, no enemy near
        // the corpse is still in combat (Vanish or Feign Death drop the party out of combat, the
        // fight goes on), and no idle enemy has the body within its aggro range (user
        // 2026-11-15, wipe protection: the enemies walk home after a wipe, often to where the
        // party fell).
        RiseCheck SafeToRise(Player* me)
        {
            if (Group* group = me->GetGroup())
                for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
                    if (Player* member = itr->getSource())
                        if (member != me && member->IsInCombat())
                            return RiseCheck::Fight;

            std::list<Unit*> enemies;
            me->GetEnemyListInRadiusAround(me, ENEMY_COMBAT_RADIUS, enemies);
            for (Unit const* enemy : enemies)
                if (enemy->IsInCombat())
                    return RiseCheck::Fight;
            for (Unit const* enemy : enemies)
                if (Creature const* c = enemy->ToCreature())
                    if (c->IsAlive() && !c->HasExtraFlag(CREATURE_FLAG_EXTRA_NO_AGGRO) &&
                        me->IsWithinDistInMap(c, c->GetAttackDistance(me) + RISE_AGGRO_MARGIN))
                        return RiseCheck::Idle;
            return RiseCheck::Safe;
        }

        // TeamPlan::rises (SelfRiseAssigner) names this bot. Same map as the bot: the commander
        // belongs to this map thread.
        bool RiseOrdered(Player* me)
        {
            std::shared_ptr<PartyCommander> commander = sCommanderMgr.FindCommander(me);
            if (!commander)
                return false;
            std::vector<ObjectGuid> const& rises = commander->GetPlan().rises;
            return std::find(rises.begin(), rises.end(), me->GetObjectGuid()) != rises.end();
        }

        WorldLocation Here(WorldObject const* obj)
        {
            return WorldLocation(obj->GetMapId(), obj->GetPositionX(), obj->GetPositionY(), obj->GetPositionZ(),
                                 obj->GetOrientation());
        }

        // As WorldSession::HandleReclaimCorpseOpcode.
        void Rise(Player* me)
        {
            me->ResurrectPlayer(0.5f);
            me->SpawnCorpseBones();
        }

        void Begin(Player* me, CorpseRunState& state, Corpse const* corpse, uint32 nowMs)
        {
            state = CorpseRunState();
            state.phase = CorpseRunState::Phase::Running;
            state.from = Here(me);
            WorldLocation const corpseAt = corpse ? Here(corpse) : state.from;
            if (!TravelTime::OutsidePoint(corpseAt, state.to))
                state.to = state.from;

            float seconds = TravelTime::Seconds(state.from, corpseAt, me->GetSpeed(MOVE_RUN));
            if (seconds < 0.0f)
                seconds = float(UNKNOWN_TRAVEL_SEC);
            seconds = std::max(seconds, float(MIN_TRAVEL_SEC));
            state.startMs = nowMs;
            state.arriveMs = nowMs + uint32(seconds * IN_MILLISECONDS);

            state.fromZone = me->GetCachedZoneId();
            state.toZone = state.to.mapId == state.from.mapId ?
                sTerrainMgr.GetZoneId(state.to.mapId, state.to.x, state.to.y, state.to.z) : state.fromZone;
        }

        // The party frames and the map: on the line from the graveyard to the end of the run.
        void Show(Player* me, CorpseRunState const& state, uint32 nowMs)
        {
            if (state.to.mapId != state.from.mapId)
                return;     // another continent: it is shown at the graveyard
            uint32 const total = std::max(1u, state.arriveMs - state.startMs);
            float const t = std::min(1.0f, float(WorldTimer::getMSTimeDiff(state.startMs, nowMs)) / float(total));
            float const x = state.from.x + (state.to.x - state.from.x) * t;
            float const y = state.from.y + (state.to.y - state.from.y) * t;
            me->SetGroupShownPosition(t < 0.5f ? state.fromZone : state.toZone, x, y);
        }

        void Arrive(Player* me, CorpseRunState& state, ArrivalState& arrival, Corpse const* corpse, uint32 nowMs)
        {
            me->ClearGroupShownPosition();
            if (!corpse)
            {
                Rise(me);
                state = CorpseRunState();
                Arrival::Start(arrival, nowMs);
                return;
            }

            MapEntry const* map = sMapStorage.LookupEntry<MapEntry>(corpse->GetMapId());
            if (map && map->IsDungeon())
            {
                state = CorpseRunState();
                Arrival::Start(arrival, nowMs);
                // The ghost was left in the dungeon (no graveyard): rise here.
                if (me->GetMapId() == corpse->GetMapId())
                {
                    Rise(me);
                    return;
                }
                // The engine raises a ghost entering its corpse's dungeon (Player::TeleportTo).
                // A refused teleport leaves a ghost: the next update runs again from here.
                if (AreaTriggerTeleport const* entrance = sObjectMgr.GetMapEntranceTrigger(corpse->GetMapId()))
                    me->TeleportTo(entrance->destination);
                else
                    me->TeleportTo(Here(corpse));
                return;
            }

            state.phase = CorpseRunState::Phase::AtCorpse;
            me->TeleportTo(Here(corpse));
        }

        void Reclaim(Player* me, CorpseRunState& state, ArrivalState& arrival, Corpse const* corpse, uint32 nowMs)
        {
            if (!corpse)
            {
                Rise(me);
                state = CorpseRunState();
                Arrival::Start(arrival, nowMs);
                return;
            }
            if (corpse->GetGhostTime() + me->GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP) > time(nullptr))
                return;
            if (!corpse->IsWithinDistInMap(me, CORPSE_RECLAIM_RADIUS, true))
            {
                me->TeleportTo(Here(corpse));
                return;
            }
            Rise(me);
            state = CorpseRunState();
            Arrival::Start(arrival, nowMs);
        }
    }

    void Update(TeamBotAI* bot, CorpseRunState& state, ArrivalState& arrival)
    {
        Player* me = bot->me;
        uint32 const now = WorldTimer::getMSTime();

        if (me->GetDeathState() == CORPSE)
        {
            if (HasSelfRes(me))
            {
                // The brain lets it rise in the fight: that wins it (SelfRiseAssigner).
                if (RiseOrdered(me))
                {
                    state.idleSinceMs = 0;
                    UseSelfRes(me);
                    return;
                }
                RiseCheck const check = SafeToRise(me);
                if (check == RiseCheck::Safe)
                {
                    state.idleSinceMs = 0;
                    UseSelfRes(me);
                    return;
                }
                if (check == RiseCheck::Fight)
                {
                    state.idleSinceMs = 0;
                    return;
                }
                // Only idle enemies in the way: they stay there. Wait a while (one may be a
                // patrol), then run the corpse like everybody else.
                if (!state.idleSinceMs)
                    state.idleSinceMs = now;
                if (WorldTimer::getMSTimeDiff(state.idleSinceMs, now) < RISE_IDLE_WAIT_MS)
                    return;
            }
            // As WorldSession::HandleRepopRequestOpcode; the bot has no pending movement change,
            // so the graveyard teleport is not scheduled.
            me->BuildPlayerRepop();
            me->RepopAtGraveyard();
            state = CorpseRunState();
            state.phase = CorpseRunState::Phase::Released;
            return;
        }
        if (me->GetDeathState() != DEAD)
            return;

        Corpse const* corpse = me->GetCorpse();
        switch (state.phase)
        {
            case CorpseRunState::Phase::None:       // a ghost already (logged in dead, a refused teleport)
            case CorpseRunState::Phase::Released:   // (PartyBotAI::UpdateAI waits out the graveyard teleport)
                Begin(me, state, corpse, now);
                Show(me, state, now);
                break;
            case CorpseRunState::Phase::Running:
                if (now < state.arriveMs)
                    Show(me, state, now);
                else
                    Arrive(me, state, arrival, corpse, now);
                break;
            case CorpseRunState::Phase::AtCorpse:
                Reclaim(me, state, arrival, corpse, now);
                break;
        }
    }

    void OnAlive(Player* me, CorpseRunState& state)
    {
        if (state.phase == CorpseRunState::Phase::None)
            return;
        me->ClearGroupShownPosition();
        state = CorpseRunState();
    }
}
}
