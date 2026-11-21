#include "Shared/Portals.h"
#include "GameObject.h"
#include "Group.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include <algorithm>

namespace BotShared
{
namespace Portals
{
    namespace
    {
        // The portal's own teleport spell (its gameobject's), 0 = not a party portal.
        uint32 TeleportOf(SpellEntry const* spell)
        {
            if (!spell)
                return 0;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                if (spell->Effect[i] != SPELL_EFFECT_TRANS_DOOR)
                    continue;
                GameObjectInfo const* info = sObjectMgr.GetGameObjectTemplate(uint32(spell->EffectMiscValue[i]));
                if (info && info->type == GAMEOBJECT_TYPE_SPELLCASTER && info->spellcaster.partyOnly)
                    return info->spellcaster.spellId;
            }
            return 0;
        }

        // Within this of a portal's destination: the leader went through it.
        float const ARRIVED_YD = 60.0f;
    }

    bool IsPortalSpell(SpellEntry const* spell)
    {
        uint32 const teleport = TeleportOf(spell);
        return teleport && sSpellMgr.GetSpellTargetPosition(teleport);
    }

    std::vector<SpellEntry const*> Known(Player const* player)
    {
        std::vector<SpellEntry const*> known;
        for (auto const& entry : player->GetSpellMap())
        {
            if (entry.second.state == PLAYERSPELL_REMOVED || entry.second.disabled || !entry.second.active)
                continue;
            SpellEntry const* spell = sSpellMgr.GetSpellEntry(entry.first);
            if (IsPortalSpell(spell))
                known.push_back(spell);
        }
        return known;
    }

    uint32 RuneOf(SpellEntry const* spell)
    {
        return spell && spell->Reagent[0] > 0 ? uint32(spell->Reagent[0]) : 0;
    }

    uint32 CastsFromBags(Player const* player)
    {
        uint32 casts = 0;
        for (SpellEntry const* spell : Known(player))
        {
            uint32 const rune = RuneOf(spell);
            uint32 const n = rune ? player->GetItemCount(rune) / std::max<uint32>(spell->ReagentCount[0], 1) : UINT32_MAX;
            casts = std::max(casts, n);
        }
        return casts;
    }

    GameObject* LeaderWentThrough(Player* bot, Player const* leader, float range)
    {
        Group* group = bot->GetGroup();
        if (!group || !leader)
            return nullptr;
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player const* member = itr->getSource();
            if (!member || !member->IsInWorld() || member->GetMap() != bot->GetMap())
                continue;
            for (SpellEntry const* spell : Known(member))
            {
                GameObject* portal = member->GetGameObject(spell->Id);
                if (!portal || !portal->IsInWorld() || !portal->isSpawned() || !bot->IsWithinDistInMap(portal, range))
                    continue;
                SpellTargetPosition const* to = sSpellMgr.GetSpellTargetPosition(TeleportOf(spell));
                if (to && leader->GetMapId() == to->mapId &&
                    leader->GetDistance(to->x, to->y, to->z, SizeFactor::None) <= ARRIVED_YD)
                    return portal;
            }
        }
        return nullptr;
    }
}
}
