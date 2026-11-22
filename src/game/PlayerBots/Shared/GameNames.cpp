#include "Shared/GameNames.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "Player.h"
#include "Unit.h"
#include "WorldSession.h"

namespace BotShared
{
namespace GameNames
{
    std::string UnitName(Unit const* unit, Player const* reader)
    {
        if (!unit)
            return std::string();
        WorldSession const* session = reader ? reader->GetSession() : nullptr;
        // WorldObject::GetNameForLocaleIdx: a creature's locales_creature name, else its name.
        return session ? unit->GetNameForLocaleIdx(session->GetSessionDbLocaleIndex()) : unit->GetName();
    }

    namespace
    {
        int LocaleIdx(Player const* reader)
        {
            return reader && reader->GetSession() ? reader->GetSession()->GetSessionDbLocaleIndex() : -1;
        }

        std::string Localized(std::vector<std::string> const* names, int idx, std::string const& fallback)
        {
            if (names && idx >= 0 && idx < int(names->size()) && !(*names)[idx].empty())
                return (*names)[idx];
            return fallback;
        }
    }

    std::string CreatureName(uint32 entry, Player const* reader)
    {
        CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(entry);
        if (!info)
            return std::string();
        CreatureLocale const* locale = sObjectMgr.GetCreatureLocale(entry);
        return Localized(locale ? &locale->Name : nullptr, LocaleIdx(reader), info->name);
    }

    std::string ItemName(uint32 itemId, Player const* reader)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        if (!proto)
            return std::string();
        ItemLocale const* locale = sObjectMgr.GetItemLocale(itemId);
        return Localized(locale ? &locale->Name : nullptr, LocaleIdx(reader), proto->Name1 ? proto->Name1 : "");
    }

    std::string QuestTitle(uint32 questId, Player const* reader)
    {
        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
        if (!quest)
            return std::string();
        QuestLocale const* locale = sObjectMgr.GetQuestLocale(questId);
        return Localized(locale ? &locale->Title : nullptr, LocaleIdx(reader), quest->GetTitle());
    }
}
}
