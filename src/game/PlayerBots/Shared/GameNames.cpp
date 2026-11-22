#include "Shared/GameNames.h"
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
}
}
