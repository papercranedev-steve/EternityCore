/*
 * What a character can do in a group, from class, talents, gear and known spells.
 * Design: .kiro/docs/2026-11-01-roles-and-tanks.md.
 *
 * Each score is 0..1: how well the character fills that role. Used for
 *  - CombatBotBaseAI::AutoAssignRole (one bot alone, Commander off): the best role;
 *  - the Commander "roles" assigner (party composition: who tanks, who heals);
 *  - players, whose role the server does not know (Commander PartyMemberFacts::role).
 *
 * Talents are counted per talent tab (TalentTabEntry::tabpage 0..2, as LFGMgr::GetTalentTrees);
 * "share" = points in a tab / points spent (0 with no points, e.g. below level 10).
 *
 *  Warrior  tank   0.45 + 0.35 x Protection share + 0.20 shield + defense bonus
 *           melee  0.60 + 0.40 x (Arms + Fury) share
 *  Paladin  tank   0.25 + 0.45 x Protection share + 0.15 shield + 0.15 Righteous Fury known + defense
 *           heal   H + 0.50 x Holy share
 *           melee  0.40 + 0.50 x Retribution share
 *  Druid    tank   (Bear Form known) 0.35 + 0.60 x Feral share + defense bonus
 *           melee  (Cat Form known)  0.30 + 0.60 x Feral share
 *           heal   H + 0.50 x Restoration share
 *           ranged 0.30 + 0.60 x Balance share
 *  Priest   heal   0.60 + 0.40 x (Discipline + Holy) share;  ranged 0.30 + 0.70 x Shadow share
 *  Shaman   heal   H + 0.50 x Restoration share; ranged 0.35 + 0.60 x Elemental share;
 *           melee  0.35 + 0.60 x Enhancement share
 *  Rogue melee 1; Hunter, Mage, Warlock ranged 1.
 *  defense bonus = 0.01 per defense skill point above level x 5, at most 0.15.
 *  H (hybrid heal) = 0.50 with talent points, NO_TALENT_HYBRID_HEAL without (user 2026-11-22).
 *
 * So with no talents (low level): warriors are melee (0.60) but tank-capable with a shield
 * (0.65); priests heal; paladins and shamans are melee, druids ranged (Cat Form comes with
 * talents): a hybrid levels as a damage dealer, as real players do, and heals on the side when
 * nobody else heals (the Commander coverage, MIN_SIDE_HEAL), instead of standing back as the
 * party's healer. From the first talent point the talents decide. A Fury warrior without a
 * shield (0.45) is never a tank (below MIN_TANK).
 */

#ifndef MANGOS_ROLE_CAPABILITY_H
#define MANGOS_ROLE_CAPABILITY_H

#include "Common.h"
#include "SharedDefines.h"

class Player;

struct RoleCapability
{
    float tank = 0.0f;
    float heal = 0.0f;
    float melee = 0.0f;
    float ranged = 0.0f;

    float For(CombatBotRoles role) const;
    // Best role on its own (no party): tank only when at least MIN_TANK and at least as good as melee.
    CombatBotRoles Best() const;
    // Best of healer / melee / ranged (the role of someone who does not tank).
    CombatBotRoles BestNonTank() const;
};

namespace RoleCapabilities
{
    // Below these a character is not given the role by the party assignment.
    float const MIN_TANK = 0.5f;
    float const MIN_HEAL = 0.5f;
    // Can heal at all: given the heal function when nobody else heals (Commander coverage),
    // counted as spare healing (PartyMemberFacts::spareHealRates).
    float const MIN_SIDE_HEAL = 0.2f;
    // A paladin, druid or shaman without talent points: below its damage score (shaman melee
    // 0.35, paladin melee 0.40, druid ranged 0.30) and below MIN_HEAL, above MIN_SIDE_HEAL.
    float const NO_TALENT_HYBRID_HEAL = 0.25f;

    RoleCapability Assess(Player const* player);
}

#endif
