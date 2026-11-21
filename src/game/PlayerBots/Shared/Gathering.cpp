#include "Shared/Gathering.h"
#include "Shared/Professions.h"
#include "Player.h"
#include "Creature.h"
#include "GameObject.h"
#include "SpellMgr.h"
#include "SpellEntry.h"
#include "DBCStores.h"

namespace BotShared
{
namespace Gathering
{
    uint32 ToolOf(uint32 skill)
    {
        if (Professions::KindOf(skill) != Professions::Kind::Gathering)
            return 0;
        for (Professions::Rank const& rank : Professions::RanksOf(skill))
            if (SpellEntry const* spell = sSpellMgr.GetSpellEntry(rank.spellId))
                if (spell->Totem[0])
                    return spell->Totem[0];
        return 0;
    }

    bool HasTool(Player const* player, uint32 skill)
    {
        uint32 const tool = ToolOf(skill);
        return !tool || player->HasItemCount(tool, 1);
    }

    Node NodeOf(GameObject const* go)
    {
        Node node;
        if (!go || !go->isSpawned() || go->getLootState() != GO_READY)
            return node;
        GameObjectInfo const* info = go->GetGOInfo();
        if (info->type != GAMEOBJECT_TYPE_CHEST)
            return node;
        LockEntry const* lock = sLockStore.LookupEntry(info->GetLockId());
        if (!lock)
            return node;
        for (int j = 0; j < MAX_LOCK_CASE; ++j)
        {
            if (lock->Type[j] != LOCK_KEY_SKILL)
                continue;
            SkillType const skill = SkillByLockType(LockType(lock->Index[j]));
            if (skill == SKILL_HERBALISM || skill == SKILL_MINING)
            {
                node.skill = skill;
                node.reqValue = lock->Skill[j];
                return node;
            }
        }
        return node;
    }

    uint32 SkinningReq(Creature const* corpse, uint32 skinnerSkill)
    {
        int32 const level = int32(corpse->GetLevel());
        int32 const req = skinnerSkill < 100 ? (level - 10) * 10 : level * 5;
        return req > 0 ? uint32(req) : 0;
    }

    bool CanSkin(Player const* player, Creature const* corpse)
    {
        if (!corpse || !corpse->IsDead() || !corpse->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE))
            return false;
        if (!corpse->IsSkinnableBy(player) || corpse->lootForSkin || !corpse->loot.isLooted())
            return false;
        uint32 const skill = player->GetSkillValue(SKILL_SKINNING);
        return skill && SkinningReq(corpse, skill) <= skill && HasTool(player, SKILL_SKINNING);
    }

    SpellEntry const* SpellOf(Player const* player, uint32 skill)
    {
        SpellEntry const* best = nullptr;
        for (Professions::Rank const& rank : Professions::RanksOf(skill))
            if (player->HasSpell(rank.spellId))
                best = sSpellMgr.GetSpellEntry(rank.spellId);
        return best;
    }
}
}
