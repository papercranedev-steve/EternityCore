#include "Rotations/ClassRotation.h"
#include "PartyBotAI.h"

namespace ClassRotations
{
    // Classes not listed here still run their rotation in PartyBotAI.cpp.
    std::unique_ptr<ClassRotation> Create(PartyBotAI& bot)
    {
        switch (bot.me->GetClass())
        {
            case CLASS_MAGE: return MakeMage(bot);
            default:         return nullptr;
        }
    }
}
