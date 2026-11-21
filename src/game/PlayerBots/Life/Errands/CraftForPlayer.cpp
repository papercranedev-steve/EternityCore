#include "Life/Errands/CraftForPlayer.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/Phrases.h"
#include "Shared/BotChat.h"
#include "Shared/Crafting.h"
#include "Shared/Handover.h"
#include "Shared/Inventory.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include "Timer.h"
#include <algorithm>
#include <sstream>

namespace Life
{
namespace CraftForPlayer
{
    namespace
    {
        // The subject of a mail of what he made, when the player's bags are full.
        std::string MailSubject(Player const* player)
        {
            return BotShared::Phrases::Word(BotShared::I18n::Of(player), "craft.mail.subject");
        }

        bool CastsOwnRecipe(Player* me, uint32 spellId)
        {
            Spell const* spell = me->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            return spell && spell->m_spellInfo && spell->m_spellInfo->Id == spellId &&
                   spell->getState() != SPELL_STATE_FINISHED;
        }

        void Say(Player* me, Player* player, std::vector<std::string> const& tags, std::string const& key)
        {
            BotShared::Phrases::Vars vars;
            vars["leader"] = player ? player->GetName() : "";
            BotShared::BotChat::Say(me, BotShared::Phrases::Say(BotShared::I18n::OfParty(me), key, tags, vars));
        }

        // Materials the last cast did not use, back to the player.
        void GiveBack(Player* me, CraftForPlayerState& state, Player* player)
        {
            if (player)
                for (auto const& itr : state.reagentsBefore)
                {
                    uint32 const now = me->GetItemCount(itr.first);
                    if (now > itr.second)
                        BotShared::Handover::MoveCount(me, itr.first, now - itr.second, player, MailSubject(player));
                }
            state.reagentsBefore.clear();
        }

        // What the cast made, to the player: new items as they are (the maker's name on them),
        // the rest from the stacks it went into. Returns how many it made.
        uint32 HandProduct(Player* me, CraftForPlayerState& state, Player* player, uint32 itemId)
        {
            uint32 const now = me->GetItemCount(itemId);
            uint32 const made = now > state.productBefore ? now - state.productBefore : 0;
            uint32 left = made;
            for (Item* item : BotShared::Inventory::BagItems(me))
            {
                if (!left)
                    break;
                if (item->GetEntry() != itemId || state.bagsBefore.count(item->GetObjectGuid()) || item->GetCount() > left)
                    continue;
                uint32 const count = item->GetCount();
                if (BotShared::Handover::MoveItem(me, item, player, MailSubject(player)) != BotShared::Handover::Result::Failed)
                    left -= count;
            }
            if (left)
                BotShared::Handover::MoveCount(me, itemId, left, player, MailSubject(player));
            return made;
        }

        void Send(Player* player, std::string const& line)
        {
            BotShared::BotChat::SystemTo(player, line);
        }
    }

    char const* ReasonKey(Reason reason)
    {
        switch (reason)
        {
            case Reason::Done:        return "done";
            case Reason::NoMaterials: return "nomats";
            case Reason::Far:         return "far";
            case Reason::Combat:      return "combat";
            case Reason::Stopped:     return "stopped";
            case Reason::BotBagsFull: return "bagsfull";
            case Reason::CastFailed:  return "failed";
            case Reason::Cooldown:    return "cooldown";
            case Reason::Gone:
            default:                  return "gone";
        }
    }

    bool IsActive(CraftForPlayerState const& state)
    {
        return state.spellId != 0;
    }

    bool Start(Player* me, CraftForPlayerState& state, Player* player, uint32 spellId, uint32 count,
               std::vector<std::string> const& tags)
    {
        SpellEntry const* recipe = sSpellMgr.GetSpellEntry(spellId);
        if (!recipe || !me->HasSpell(spellId) || !BotShared::Crafting::ProductOf(recipe).itemId ||
            !BotShared::Crafting::CastsFromBags(player, recipe))
            return false;
        if (IsActive(state))
            Stop(me, state, Reason::Stopped);
        state = CraftForPlayerState();
        state.player = player->GetObjectGuid();
        state.spellId = spellId;
        state.wanted = count;
        state.tags = tags;
        state.nextCastMs = WorldTimer::getMSTime() + BETWEEN_CASTS_MS;
        Errands::StopWalk(me);
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[CraftForPlayer] %s makes %s %u x spell %u.", me->GetName(), player->GetName(), count, spellId);
        return true;
    }

    void Stop(Player* me, CraftForPlayerState& state, Reason reason)
    {
        if (!IsActive(state))
            return;
        Player* player = me->IsInWorld() ? me->GetMap()->GetPlayer(state.player) : nullptr;
        if (state.casting && CastsOwnRecipe(me, state.spellId))
            me->InterruptSpell(CURRENT_GENERIC_SPELL);
        GiveBack(me, state, player);
        if (player)
        {
            std::ostringstream line;
            line << "PBCRAFT;" << me->GetName() << ";stop;" << ReasonKey(reason);
            Send(player, line.str());
            Say(me, player, state.tags, reason == Reason::Done ? "craft.done" : std::string("craft.stop.") + ReasonKey(reason));
        }
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[CraftForPlayer] %s stops after %u (%s).", me->GetName(), state.made, ReasonKey(reason));
        state = CraftForPlayerState();
    }

    bool Update(Player* me, CraftForPlayerState& state)
    {
        if (!IsActive(state))
            return false;
        Player* player = me->GetMap()->GetPlayer(state.player);
        SpellEntry const* recipe = sSpellMgr.GetSpellEntry(state.spellId);
        if (!player || !player->IsInWorld() || !recipe)
        {
            Stop(me, state, Reason::Gone);
            return false;
        }
        if (me->IsInCombat() || player->IsInCombat())
        {
            Stop(me, state, Reason::Combat);
            return false;
        }

        BotShared::Crafting::Product const product = BotShared::Crafting::ProductOf(recipe);
        uint32 const now = WorldTimer::getMSTime();
        if (state.casting)
        {
            if (CastsOwnRecipe(me, state.spellId))
            {
                if (!me->IsWithinDistInMap(player, REACH_YD))
                {
                    Stop(me, state, Reason::Far);
                    return false;
                }
                return true;
            }
            state.casting = false;
            uint32 const got = HandProduct(me, state, player, product.itemId);
            GiveBack(me, state, player);
            if (!got)
            {
                Stop(me, state, Reason::CastFailed);
                return false;
            }
            ++state.made;
            uint32 const left = state.wanted ? state.wanted - std::min(state.made, state.wanted) :
                                BotShared::Crafting::CastsFromBags(player, recipe);
            std::ostringstream line;
            line << "PBCRAFT;" << me->GetName() << ";made;" << product.itemId << ";" << state.made << ";" << left;
            Send(player, line.str());
            if (state.wanted && state.made >= state.wanted)
            {
                Stop(me, state, Reason::Done);
                return false;
            }
            state.nextCastMs = now + BETWEEN_CASTS_MS;
            return true;
        }

        if (now < state.nextCastMs)
            return true;
        if (!me->IsWithinDistInMap(player, REACH_YD))
        {
            Stop(me, state, Reason::Far);
            return false;
        }
        if (me->IsMoving())
            Errands::StopWalk(me);
        // The global cooldown or another cast of its own: a moment; the recipe's own cooldown
        // (a transmute): it cannot now.
        if (!me->IsSpellReady(recipe) || me->IsNonMeleeSpellCasted(false))
        {
            if (!state.waitSinceMs)
                state.waitSinceMs = now;
            else if (WorldTimer::getMSTimeDiff(state.waitSinceMs, now) > READY_WAIT_MS)
            {
                Stop(me, state, Reason::Cooldown);
                return false;
            }
            return true;
        }
        state.waitSinceMs = 0;
        if (!BotShared::Crafting::CastsFromBags(player, recipe))
        {
            Stop(me, state, state.made && !state.wanted ? Reason::Done : Reason::NoMaterials);
            return false;
        }

        std::vector<BotShared::Crafting::Reagent> const reagents = BotShared::Crafting::ReagentsOf(recipe);
        bool fits = BotShared::Handover::Fits(me, product.itemId, product.perCast);
        for (BotShared::Crafting::Reagent const& r : reagents)
            fits = fits && BotShared::Handover::Fits(me, r.itemId, r.count);
        if (!fits)
        {
            Stop(me, state, Reason::BotBagsFull);
            return false;
        }

        // One cast's materials from the player's bags (never the bot's own).
        state.reagentsBefore.clear();
        for (BotShared::Crafting::Reagent const& r : reagents)
            state.reagentsBefore[r.itemId] = me->GetItemCount(r.itemId);
        for (BotShared::Crafting::Reagent const& r : reagents)
            if (BotShared::Handover::MoveCount(player, r.itemId, r.count, me, "") != BotShared::Handover::Result::Bags)
            {
                Stop(me, state, Reason::BotBagsFull);
                return false;
            }

        state.productBefore = me->GetItemCount(product.itemId);
        state.bagsBefore.clear();
        BotShared::Inventory::CollectBagItemGuids(me, state.bagsBefore);
        if (me->GetStandState() != UNIT_STAND_STATE_STAND)
            me->SetStandState(UNIT_STAND_STATE_STAND);
        SpellCastResult const result = me->CastSpell(me, recipe, false);
        if (result != SPELL_CAST_OK)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[CraftForPlayer] %s cannot cast %u (result %u).", me->GetName(), state.spellId, uint32(result));
            Stop(me, state, Reason::CastFailed);
            return false;
        }
        state.casting = true;
        return true;
    }
}
}
