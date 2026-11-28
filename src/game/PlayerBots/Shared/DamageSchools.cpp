#include "Shared/DamageSchools.h"
#include "Shared/TalentSpread.h"
#include "Player.h"
#include "SharedDefines.h"

namespace BotShared
{
namespace DamageSchools
{
    namespace
    {
        // Talent tab pages (TalentSpread order).
        uint32 const MAGE_FIRE = 1, MAGE_FROST = 2;
        uint32 const WARLOCK_DESTRUCTION = 2;
        uint32 const PRIEST_SHADOW = 2;

        // The tree with most points; `none` without talents or on a tie for first.
        uint32 MainTree(TalentSpread::Points const& points, uint32 none)
        {
            uint32 best = none;
            uint32 most = 0;
            for (uint32 i = 0; i < points.size(); ++i)
            {
                if (points[i] > most)
                {
                    most = points[i];
                    best = i;
                }
                else if (points[i] == most && most)
                    best = none;
            }
            return best;
        }

        bool MageFire(TalentSpread::Points const& points)
        {
            return points[MAGE_FIRE] > points[MAGE_FROST];
        }

        Shares For(uint8 classId, TalentSpread::Points const& points)
        {
            Shares s = {};
            switch (classId)
            {
                case CLASS_MAGE:
                    // the filler, Arcane Explosion and Arcane Missiles, the other school's instants
                    s[MageFire(points) ? SPELL_SCHOOL_FIRE : SPELL_SCHOOL_FROST] = 0.80f;
                    s[SPELL_SCHOOL_ARCANE] = 0.15f;
                    s[MageFire(points) ? SPELL_SCHOOL_FROST : SPELL_SCHOOL_FIRE] = 0.05f;
                    break;
                case CLASS_WARLOCK:
                    // Shadow Bolt, Corruption, Drain; Immolate and Searing Pain (more on destruction)
                    if (MainTree(points, 3) == WARLOCK_DESTRUCTION)
                    {
                        s[SPELL_SCHOOL_SHADOW] = 0.70f;
                        s[SPELL_SCHOOL_FIRE] = 0.30f;
                    }
                    else
                    {
                        s[SPELL_SCHOOL_SHADOW] = 0.80f;
                        s[SPELL_SCHOOL_FIRE] = 0.20f;
                    }
                    break;
                case CLASS_PRIEST:
                    // Shadow: Mind Blast, Mind Flay, Pain; otherwise Smite and Holy Fire too
                    if (MainTree(points, 3) == PRIEST_SHADOW)
                    {
                        s[SPELL_SCHOOL_SHADOW] = 0.90f;
                        s[SPELL_SCHOOL_HOLY] = 0.10f;
                    }
                    else
                    {
                        s[SPELL_SCHOOL_SHADOW] = 0.50f;
                        s[SPELL_SCHOOL_HOLY] = 0.50f;
                    }
                    break;
                case CLASS_SHAMAN:
                    // Lightning Bolt and Chain Lightning, Flame Shock, Frost Shock
                    s[SPELL_SCHOOL_NATURE] = 0.80f;
                    s[SPELL_SCHOOL_FIRE] = 0.15f;
                    s[SPELL_SCHOOL_FROST] = 0.05f;
                    break;
                case CLASS_DRUID:
                    // Wrath; Starfire and Moonfire
                    s[SPELL_SCHOOL_NATURE] = 0.55f;
                    s[SPELL_SCHOOL_ARCANE] = 0.45f;
                    break;
                case CLASS_PALADIN:
                    s[SPELL_SCHOOL_HOLY] = 1.0f;
                    break;
                default:
                    break;
            }
            return s;
        }
    }

    Shares Of(Player const* player)
    {
        if (!player)
            return Shares{};
        return For(player->GetClass(), TalentSpread::Of(player));
    }

    Shares OfClass(uint8 classId)
    {
        return For(classId, TalentSpread::Points{ 0, 0, 0 });
    }

    SpellSchools Main(Player const* player)
    {
        Shares const s = Of(player);
        uint32 best = SPELL_SCHOOL_NORMAL;
        for (uint32 i = 0; i < s.size(); ++i)
            if (s[i] > s[best])
                best = i;
        return SpellSchools(best);
    }

    float Covered(Shares const& shares, uint32 schoolMask)
    {
        float covered = 0.0f;
        for (uint32 i = 0; i < shares.size(); ++i)
            if (schoolMask & (1u << i))
                covered += shares[i];
        return covered;
    }
}
}
