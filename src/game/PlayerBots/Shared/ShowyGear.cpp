#include "Shared/ShowyGear.h"
#include "Item.h"
#include "ItemPrototype.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SharedDefines.h"

namespace BotShared
{
    namespace ShowyGear
    {
        namespace
        {
            struct Slot
            {
                uint8 slot;
                Look look;
            };

            Slot const g_visible[] =
            {
                { EQUIPMENT_SLOT_HEAD, Look::Head },
                { EQUIPMENT_SLOT_SHOULDERS, Look::Shoulders },
                { EQUIPMENT_SLOT_CHEST, Look::Chest },
                { EQUIPMENT_SLOT_BACK, Look::Back },
                { EQUIPMENT_SLOT_MAINHAND, Look::Weapon },
                { EQUIPMENT_SLOT_OFFHAND, Look::Weapon },
                { EQUIPMENT_SLOT_RANGED, Look::Weapon },
            };

            uint8 PointsOf(ItemPrototype const* proto, uint32 level)
            {
                if (proto->Quality >= ITEM_QUALITY_LEGENDARY)
                    return 4;
                if (proto->Quality == ITEM_QUALITY_EPIC)
                    return proto->ItemSet ? 3 : 2;
                if (proto->Quality == ITEM_QUALITY_RARE && level < 60)
                {
                    uint32 const above = level < MOUNT_FROM_LEVEL ? BLUE_ABOVE_LOW : BLUE_ABOVE_MID;
                    if (proto->ItemLevel >= level + above)
                        return 1;
                }
                return 0;
            }
        }

        std::vector<Thing> Of(Player const* p)
        {
            std::vector<Thing> out;
            uint32 const level = p->GetLevel();
            for (Slot const& s : g_visible)
            {
                Item const* item = p->GetItemByPos(INVENTORY_SLOT_BAG_0, s.slot);
                ItemPrototype const* proto = item ? item->GetProto() : nullptr;
                if (!proto)
                    continue;
                if (uint8 const points = PointsOf(proto, level))
                    out.push_back({ proto->ItemId, points, s.look });
            }

            if (level >= MOUNT_FROM_LEVEL && p->IsMounted())
            {
                Unit::AuraList const& mounts = p->GetAurasByType(SPELL_AURA_MOUNTED);
                uint32 const spell = mounts.empty() ? 0 : mounts.front()->GetId();
                bool const epic = p->GetMaxPositiveAuraModifier(SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED) >= 100;
                uint8 const points = epic ? 2 : level < 60 ? 1 : 0;
                if (spell && points)
                    out.push_back({ spell | MOUNT_KEY, points, Look::Mount });
            }
            return out;
        }
    }
}
