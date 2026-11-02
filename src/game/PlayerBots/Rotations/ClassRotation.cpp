#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"

namespace ClassRotations
{
    // Classes / roles not listed here still run their rotation in PartyBotAI.cpp.
    std::unique_ptr<ClassRotation> Create(PartyBotAI& bot)
    {
        switch (bot.me->GetClass())
        {
            case CLASS_MAGE:    return MakeMage(bot);
            case CLASS_WARRIOR: return bot.GetRole() == ROLE_TANK ? MakeWarriorTank(bot) : nullptr;
            default:            return nullptr;
        }
    }
}
