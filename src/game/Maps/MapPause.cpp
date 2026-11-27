/*
 * Single-player pause: the PartyBot addon's "Pause game" button in the Esc menu.
 *
 * .pause [on | off | state]       no argument: toggle
 *   -> PBPAUSE;1                  the player's map is paused
 *   -> PBPAUSE;0                  it runs
 *   -> PBERR;;pauseplayers        not paused: another real player is on this map
 *   -> PBERR;;pausetaxi           not paused: on a flight path
 *   -> PBERR;;usage;.pause [on|off|state]
 *
 * While a map is paused nothing on it updates: Map::UpdatePlayers and the cell updates skip every
 * object but move its update clock along, so the paused time never reaches it. Auras, DoTs, casts,
 * swings, regen, creature and bot AI, movement and instance scripts all wait.
 * The players' packets are still read (chat and addon commands work). The player who paused is
 * rooted, and the packet handlers refuse casting, using items and objects, attacking, looting and
 * pet orders (IsRefusedWhilePaused). Bags, gear, talents, quests and talking to NPCs still work.
 *
 * What runs on wall time is fixed up on resume or left alone:
 * - cooldowns, GCDs and school lockouts are pushed back by the paused time (every player on the
 *   map, and the creatures the pauser can see);
 * - respawns, corpse decay, mail and the auction house are not paused (the player doesn't see them).
 *
 * The client keeps its own clocks: on pause, units in sight that are moving get a stop so the
 * client doesn't play their path out. On resume the pauser's client gets its cooldowns, aura
 * durations and cast bar again. Other units' cast bars and the client's own animations can't be
 * stopped.
 */

#include "MapPause.h"
#include "Map.h"
#include "Player.h"
#include "Creature.h"
#include "Chat.h"
#include "World.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "MoveSpline.h"
#include <cstring>

namespace
{
    TimePoint Now()
    {
        return std::chrono::time_point_cast<std::chrono::milliseconds>(Clock::now());
    }

    // living units the player can see
    std::list<Unit*> UnitsInSight(Player* player)
    {
        float const range = player->GetMap()->GetVisibilityDistance();
        std::list<Unit*> units;
        MaNGOS::AnyUnitInObjectRangeCheck check(player, range);
        MaNGOS::UnitListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(units, check);
        Cell::VisitAllObjects(player, searcher, range);
        return units;
    }

    void SendState(ChatHandler& chat, bool paused)
    {
        chat.PSendSysMessage("PBPAUSE;%u", paused ? 1u : 0u);
    }
}

MapPauseResult Map::Pause(Player* by)
{
    if (IsPaused())
        return MapPauseResult::Ok;

    for (auto const& ref : m_mapRefManager)
    {
        Player* other = ref.getSource();
        if (other && other != by && !other->IsBot())
            return MapPauseResult::OtherPlayers;
    }
    if (by->IsTaxiFlying())
        return MapPauseResult::Taxi;

    m_pausedBy = by->GetObjectGuid();
    m_pausedSince = Now();

    // a root, not a stun: the character just stands
    by->SetRooted(true);

    for (Unit* unit : UnitsInSight(by))
        if (unit != by && !unit->movespline->Finalized() && !unit->movespline->IsUninterruptible())
            unit->StopMoving(true);

    return MapPauseResult::Ok;
}

void Map::Resume()
{
    if (!IsPaused())
        return;

    auto const paused = std::chrono::duration_cast<std::chrono::milliseconds>(Now() - m_pausedSince);
    Player* by = nullptr;
    for (auto const& ref : m_mapRefManager)
    {
        Player* player = ref.getSource();
        if (!player)
            continue;
        if (player->GetObjectGuid() == m_pausedBy)
            by = player;
        player->DelayCooldowns(paused);
    }
    m_pausedBy.Clear();
    m_pausedSince = TimePoint();

    if (!by)
        return;

    for (Unit* unit : UnitsInSight(by))
        if (!unit->IsPlayer())
            unit->DelayCooldowns(paused);

    // the client counted on while paused
    by->SendCooldownsLeft();
    for (auto const& holder : by->GetSpellAuraHolderMap())
        holder.second->UpdateAuraDuration();
    for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
        if (Spell const* spell = by->GetCurrentSpell(type))
            spell->SendCastBarAfterPause(uint32(paused.count()));

    // a root or stun from an aura stays
    if (!by->HasUnitState(UNIT_STATE_ROOT | UNIT_STATE_PENDING_ROOT | UNIT_STATE_STUNNED | UNIT_STATE_PENDING_STUNNED))
        by->SetRooted(false);
}

bool IsRefusedWhilePaused(Player* player)
{
    if (!player || !player->IsInWorld() || !player->GetMap()->IsPaused())
        return false;

    ChatHandler chat(player);
    SendState(chat, true);
    return true;
}

bool ChatHandler::HandlePauseCommand(char* args)
{
    Player* player = m_session ? m_session->GetPlayer() : nullptr;
    if (!player || !player->IsInWorld())
        return false;

    Map* map = player->GetMap();
    char* arg = ExtractLiteralArg(&args);
    bool pause = !map->IsPaused();
    if (arg && strcmp(arg, "on") == 0)
        pause = true;
    else if (arg && strcmp(arg, "off") == 0)
        pause = false;
    else if (arg && strcmp(arg, "state") == 0)
        pause = map->IsPaused();
    else if (arg)
    {
        SendSysMessage("PBERR;;usage;.pause [on|off|state]");
        SetSentErrorMessage(true);
        return false;
    }

    if (pause && !map->IsPaused())
    {
        switch (map->Pause(player))
        {
            case MapPauseResult::Ok:
                break;
            case MapPauseResult::OtherPlayers:
                SendSysMessage("PBERR;;pauseplayers");
                SetSentErrorMessage(true);
                return false;
            case MapPauseResult::Taxi:
                SendSysMessage("PBERR;;pausetaxi");
                SetSentErrorMessage(true);
                return false;
        }
    }
    else if (!pause)
        map->Resume();

    SendState(*this, map->IsPaused());
    return true;
}
