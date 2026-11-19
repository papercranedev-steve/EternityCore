#include "Population/PlaceAnchors.h"
#include "ObjectMgr.h"
#include "GameObjectDefines.h"
#include "SharedDefines.h"
#include "UnitDefines.h"

namespace Population
{
    char const* NpcAnchorType(CreatureInfo const* info)
    {
        // Critters carry quest flags too (Northshire's chickens).
        if (!info || info->type == CREATURE_TYPE_CRITTER)
            return nullptr;
        uint32 const npcFlags = info->npc_flags;
        if (npcFlags & UNIT_NPC_FLAG_INNKEEPER)
            return "inn";
        if (npcFlags & UNIT_NPC_FLAG_REPAIR)
            return "smith";
        if (npcFlags & UNIT_NPC_FLAG_FLIGHTMASTER)
            return "flight";
        if (npcFlags & UNIT_NPC_FLAG_TRAINER)
            return "trainer";
        if (npcFlags & UNIT_NPC_FLAG_VENDOR)
            return "vendor";
        // Where people crowd in a start area (Deputy Willem, Marshal McBride).
        if (npcFlags & UNIT_NPC_FLAG_QUESTGIVER)
            return "quest";
        return nullptr;
    }

    char const* ObjectAnchorType(GameObjectInfo const* info)
    {
        if (!info)
            return nullptr;
        switch (info->type)
        {
            case GAMEOBJECT_TYPE_CHAIR:
                return "chair";
            case GAMEOBJECT_TYPE_MAILBOX:
                return "mail";
            case GAMEOBJECT_TYPE_SPELL_FOCUS:
                // SpellFocusObject.dbc: 1 anvil, 3 forge, 4 cooking fire.
                if (info->spellFocus.focusId == 1 || info->spellFocus.focusId == 3)
                    return "smith";
                if (info->spellFocus.focusId == 4)
                    return "fire";
                return nullptr;
        }
        return nullptr;
    }

    uint32 VisitWeight(std::string const& type, bool startArea)
    {
        if (type == "ah" || type == "bank" || type == "smith" || type == "quest")
            return 3;
        if (type == "chair")
            return startArea ? 1 : 3;
        if (type == "inn" || type == "vendor" || type == "trainer" || type == "mail" || type == "idle" || type == "fire")
            return 2;
        if (type == "flight" || type == "showcase")
            return 1;
        return 0;   // route points
    }
}
