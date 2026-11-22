/*
 * Copyright (C) 2005-2010 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2010 MaNGOSZero <http://github.com/mangoszero/mangoszero/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef MANGOS_PLAYERAI_H
#define MANGOS_PLAYERAI_H

#include "Common.h"
#include "ObjectGuid.h"

class WorldObject;
class Unit;
class Creature;
class Player;
class SpellEntry;
struct ItemPrototype;

class PlayerAI
{
    public:
        explicit PlayerAI(Player* pPlayer) : me(pPlayer), enablePositiveSpells(false) {}
        virtual ~PlayerAI();
        void SetPlayer(Player* player) { me = player; }
        virtual void Remove();

        // Called at World update tick
        virtual void UpdateAI(uint32 const /*diff*/);
        virtual void MovementInform(uint32 MovementType, uint32 Data = 0) {}
        // `from` did a text emote (TEXTEMOTE_*) aimed at this player (as CreatureAI::ReceiveEmote).
        virtual void ReceiveEmote(Player* /*from*/, uint32 /*textEmote*/) {}

        // == Small changes to the controlled player's own numbers ==========
        // Core code asks these for a player that has an AI (a human playing the character has
        // none). Bot AIs override them (companion friend traits: PlayerBots/Companions/Affinity/
        // TraitEffects.h). Defaults: no change. Rates multiply; chances are 0..1.
        virtual void ChangeDamageTaken(Unit* /*attacker*/, uint32& /*damage*/, SpellEntry const* /*spell*/) {}
        virtual void ChangeHealingTaken(uint32& /*heal*/) {}
        // An aura landing on him (diminishing returns already applied); keep it above 0.
        virtual void ChangeAuraDuration(SpellEntry const* /*spell*/, bool /*positive*/, int32& /*duration*/) {}
        // Resurrected by someone else (health and mana he gets up with).
        virtual void ChangeResurrection(uint32& /*health*/, uint32& /*mana*/) {}
        virtual float RegenRate() const { return 1.0f; }                   // health and mana, out of combat
        virtual float CastTimeRate(SpellEntry const* /*spell*/) const { return 1.0f; }
        virtual float PushbackRate() const { return 1.0f; }                // cast delay when hit
        virtual float LockoutRate() const { return 1.0f; }                 // school lockout when interrupted
        virtual float FallDamageRate() const { return 1.0f; }
        virtual float BreathRate() const { return 1.0f; }
        virtual float SwimSpeedRate() const { return 1.0f; }
        virtual float ReputationRate() const { return 1.0f; }              // gains only
        virtual float RepairCostRate() const { return 1.0f; }
        virtual float SellPriceRate(ItemPrototype const* /*proto*/) const { return 1.0f; }
        virtual float BuyPriceRate(ItemPrototype const* /*proto*/) const { return 1.0f; }
        virtual float LootMoneyRate() const { return 1.0f; }
        virtual float DurabilityLossRate() const { return 1.0f; }
        virtual float DetectedRangeRate() const { return 1.0f; }           // how far creatures notice him
        virtual int32 StealthDetectBonus() const { return 0; }
        // A recipe that makes several: chance of one more.
        virtual float CraftExtraChance(SpellEntry const* /*spell*/) const { return 0.0f; }
        // The reagent this cast does not use up, 0 = none (rolled once per cast).
        virtual uint32 SavedReagent(SpellEntry const* /*spell*/) { return 0; }
        // A herb, ore or skinning loot just made for him: chance of one more of its first item.
        virtual float GatherExtraChance() const { return 0.0f; }

        // == Helpers =====================================
        bool CanCastSpell(Unit* pTarget, SpellEntry const* pSpell, bool isTriggered, bool checkControlled = true);

        // == Fields =======================================

        // Pointer to controlled by AI player
        Player* me;
        bool enablePositiveSpells;
};

class PlayerControlledAI: public PlayerAI
{
    public:
        explicit PlayerControlledAI(Player* pPlayer, Unit* caster = nullptr);

        ~PlayerControlledAI() override;

        // Called at World update tick
        void UpdateAI(uint32 const /*diff*/) override;
        Unit* FindController();
        void UpdateTarget(Unit* victim);

        // == Fields =======================================
        ObjectGuid controllerGuid;
        uint32 uiGlobalCD;
        std::vector<uint32> usableSpells;
        bool bIsMelee;
        bool isHealer;
        
};

#endif
