#include "Commander/Perception/Profiles/StandIn.h"
#include "SharedDefines.h"

namespace Commander
{
namespace StandIn
{
    StandInFacts Of(PartyMemberFacts const& m)
    {
        StandInFacts out;
        if (!m.CanAct())
            return out;
        if (m.isPet)
        {
            if (m.canOffTank)
                out.way = StandInWay::Pet;
            return out;
        }
        if (!m.isBot || m.role == ROLE_HEALER)
            return out;
        switch (m.classId)
        {
            case CLASS_WARRIOR:
                out.way = StandInWay::Stance;
                break;
            case CLASS_DRUID:
                if (m.cap.tank > 0.0f)
                {
                    out.way = StandInWay::Bear;
                    if (!m.tankStance)
                        out.armorMult = m.level >= DIRE_BEAR_LEVEL ? DIRE_BEAR_ARMOR_MULT : BEAR_ARMOR_MULT;
                }
                break;
            case CLASS_PALADIN:
                if (m.cap.tank > 0.0f)
                    out.way = StandInWay::Fury;
                break;
            default:
                break;
        }
        return out;
    }

    char const* Describe(StandInWay way)
    {
        switch (way)
        {
            case StandInWay::Pet:    return "the pet";
            case StandInWay::Stance: return "the warrior (Defensive Stance)";
            case StandInWay::Bear:   return "the druid (bear form)";
            case StandInWay::Fury:   return "the paladin (Righteous Fury)";
            default:                 return "nobody";
        }
    }
}
}
