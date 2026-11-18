#include "Commander/Classes/ClassKit.h"
#include "TeamBotAI.h"

namespace Commander
{
namespace ClassKits
{
    // The only switch on the class in the Commander: everything else asks the kit.
    std::unique_ptr<ClassKit> Create(TeamBotAI const& bot)
    {
        switch (bot.me->GetClass())
        {
            case CLASS_WARRIOR: return MakeWarrior(bot);
            case CLASS_PALADIN: return MakePaladin(bot);
            case CLASS_ROGUE:   return MakeRogue(bot);
            case CLASS_PRIEST:  return MakePriest(bot);
            case CLASS_SHAMAN:  return MakeShaman(bot);
            case CLASS_MAGE:    return MakeMage(bot);
            case CLASS_DRUID:   return MakeDruid(bot);
            case CLASS_WARLOCK: return MakeWarlock(bot);
            case CLASS_HUNTER:  return MakeHunter(bot);
            default:            return std::make_unique<ClassKit>(bot);
        }
    }
}
}
