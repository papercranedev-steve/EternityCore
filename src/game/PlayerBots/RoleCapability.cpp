#include "RoleCapability.h"
#include "Player.h"
#include "Item.h"
#include "DBCStores.h"
#include <algorithm>

namespace
{
    uint32 const SPELL_BEAR_FORM = 5487;
    uint32 const SPELL_DIRE_BEAR_FORM = 9634;
    uint32 const SPELL_CAT_FORM = 768;
    uint32 const SPELL_RIGHTEOUS_FURY = 25780;

    // Points spent per talent tab page (0..2), same count as LFGMgr::GetTalentTrees.
    void TalentPoints(Player const* player, float points[3])
    {
        points[0] = points[1] = points[2] = 0.0f;
        uint32 const classMask = player->GetClassMask();
        for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
        {
            TalentEntry const* talent = sTalentStore.LookupEntry(i);
            if (!talent)
                continue;
            TalentTabEntry const* tab = sTalentTabStore.LookupEntry(talent->TalentTab);
            if (!tab || !(classMask & tab->ClassMask) || tab->tabpage > 2)
                continue;
            for (int32 rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
            {
                if (talent->RankID[rank] && player->HasSpell(talent->RankID[rank]))
                {
                    points[tab->tabpage] += float(rank + 1);
                    break;
                }
            }
        }
    }

    bool WearsShield(Player const* player)
    {
        Item const* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
        return item && item->GetProto() && item->GetProto()->InventoryType == INVTYPE_SHIELD;
    }

    float DefenseBonus(Player const* player)
    {
        float const above = float(player->GetSkillValue(SKILL_DEFENSE)) - 5.0f * float(player->GetLevel());
        return std::min(0.15f, std::max(0.0f, above * 0.01f));
    }
}

float RoleCapability::For(CombatBotRoles role) const
{
    switch (role)
    {
        case ROLE_TANK:      return tank;
        case ROLE_HEALER:    return heal;
        case ROLE_MELEE_DPS: return melee;
        case ROLE_RANGE_DPS: return ranged;
        default:             return 0.0f;
    }
}

CombatBotRoles RoleCapability::BestNonTank() const
{
    if (heal >= melee && heal >= ranged && heal > 0.0f)
        return ROLE_HEALER;
    return melee >= ranged ? ROLE_MELEE_DPS : ROLE_RANGE_DPS;
}

CombatBotRoles RoleCapability::Best() const
{
    CombatBotRoles const other = BestNonTank();
    if (tank >= RoleCapabilities::MIN_TANK && tank >= For(other))
        return ROLE_TANK;
    return other;
}

namespace RoleCapabilities
{
    RoleCapability Assess(Player const* player)
    {
        RoleCapability c;
        float points[3];
        TalentPoints(player, points);
        float const total = points[0] + points[1] + points[2];
        auto share = [&](uint32 tab) { return total > 0.0f ? points[tab] / total : 0.0f; };

        switch (player->GetClass())
        {
            case CLASS_WARRIOR:
                c.tank = 0.45f + 0.35f * share(2) + (WearsShield(player) ? 0.20f : 0.0f) + DefenseBonus(player);
                c.melee = 0.60f + 0.40f * (share(0) + share(1));
                break;
            case CLASS_PALADIN:
                c.tank = 0.25f + 0.45f * share(1) + (WearsShield(player) ? 0.15f : 0.0f) +
                         (player->HasSpell(SPELL_RIGHTEOUS_FURY) ? 0.15f : 0.0f) + DefenseBonus(player);
                c.heal = 0.50f + 0.50f * share(0);
                c.melee = 0.40f + 0.50f * share(2);
                break;
            case CLASS_DRUID:
                if (player->HasSpell(SPELL_BEAR_FORM) || player->HasSpell(SPELL_DIRE_BEAR_FORM))
                    c.tank = 0.35f + 0.60f * share(1) + DefenseBonus(player);
                if (player->HasSpell(SPELL_CAT_FORM))
                    c.melee = 0.30f + 0.60f * share(1);
                c.heal = 0.50f + 0.50f * share(2);
                c.ranged = 0.30f + 0.60f * share(0);
                break;
            case CLASS_PRIEST:
                c.heal = 0.60f + 0.40f * (share(0) + share(1));
                c.ranged = 0.30f + 0.70f * share(2);
                break;
            case CLASS_SHAMAN:
                c.heal = 0.50f + 0.50f * share(2);
                c.ranged = 0.35f + 0.60f * share(0);
                c.melee = 0.35f + 0.60f * share(1);
                break;
            case CLASS_ROGUE:
                c.melee = 1.0f;
                break;
            case CLASS_HUNTER:
            case CLASS_MAGE:
            case CLASS_WARLOCK:
                c.ranged = 1.0f;
                break;
            default:
                c.melee = 0.5f;
                break;
        }
        c.tank = std::min(1.0f, c.tank);
        return c;
    }
}
