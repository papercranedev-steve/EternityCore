#include "Companions/Account/Naming.h"
#include "Population/PopulationMgr.h"
#include "Player.h"
#include "Group.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "World.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"

namespace Companions
{
namespace Naming
{
    std::string Check(uint32 charGuid, std::string& name)
    {
        if (!normalizePlayerName(name))
            return "name_empty";
        std::string current;
        if (sObjectMgr.GetPlayerNameByGUID(ObjectGuid(HIGHGUID_PLAYER, charGuid), current) && current == name)
            return "";

        switch (ObjectMgr::CheckPlayerName(name, true))
        {
            case CHAR_NAME_SUCCESS:             break;
            case CHAR_NAME_TOO_SHORT:           return "name_short";
            case CHAR_NAME_TOO_LONG:            return "name_long";
            case CHAR_NAME_THREE_CONSECUTIVE:   return "name_repeat";
            case CHAR_NAME_INVALID_CHARACTER:
            case CHAR_NAME_MIXED_LANGUAGES:
            case CHAR_NAME_INVALID_APOSTROPHE:
            case CHAR_NAME_MULTIPLE_APOSTROPHES:
            case CHAR_NAME_INVALID_SPACE:
            case CHAR_NAME_CONSECUTIVE_SPACES:  return "name_chars";
            default:                            return "name_reserved";
        }
        // (players may not take these at creation either; a GM could, his companion not)
        if (sObjectMgr.IsReservedName(name))
            return "name_reserved";

        ObjectGuid const other = sObjectMgr.GetPlayerGuidByName(name);
        if (other && other.GetCounter() != charGuid)
            return "name_taken";
        std::string escaped = name;
        CharacterDatabase.escape_string(escaped);
        if (std::unique_ptr<QueryResult> taken{ CharacterDatabase.PQuery(
                "SELECT 1 FROM `characters` WHERE `name` = '%s' AND `guid` <> %u", escaped.c_str(), charGuid) })
            return "name_taken";
        if (sPopulationMgr.IsNameInUse(name))
            return "name_taken";
        return "";
    }

    void Rename(Player* owner, uint32 charGuid, uint32 memberId, std::string const& oldName, std::string const& newName)
    {
        std::string escaped = newName;
        CharacterDatabase.escape_string(escaped);
        CharacterDatabase.PExecute("UPDATE `characters` SET `name` = '%s' WHERE `guid` = %u", escaped.c_str(), charGuid);
        sObjectMgr.ChangePlayerNameInCache(charGuid, oldName, newName);

        ObjectGuid const guid(HIGHGUID_PLAYER, charGuid);
        // (online: his next save writes his name too, Player::SaveToDB)
        Player* bot = sObjectAccessor.FindPlayerNotInWorld(guid);
        if (bot)
            bot->SetName(newName);
        if (Group* group = bot ? bot->GetGroup() : nullptr)
            group->ChangeMemberName(guid, newName);
        else if (Group* ownerGroup = owner ? owner->GetGroup() : nullptr)
            ownerGroup->ChangeMemberName(guid, newName);   // an offline member of the party
        if (memberId)
            sPopulationMgr.RenameMember(memberId, newName);
        sWorld.InvalidatePlayerDataToAllClient(guid);

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[Companion] Companion %u (%s) is called %s from now on.",
            charGuid, oldName.c_str(), newName.c_str());
    }
}
}
