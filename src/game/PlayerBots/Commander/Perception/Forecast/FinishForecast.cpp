#include "Commander/Perception/Forecast/FinishForecast.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "SharedDefines.h"
#include <algorithm>

namespace Commander
{
namespace FinishForecast
{
    namespace
    {
        // Melee and pets lose a running enemy (FleePredictor::MELEE_CONTACT_SEC: at once once it runs).
        bool MeleeLike(PartyMemberFacts const& m)
        {
            return m.isPet || m.role == ROLE_MELEE_DPS || m.role == ROLE_TANK;
        }

        float Trust(WorldModel const& world, PartyMemberFacts const& m)
        {
            if (m.isBot)
                return 1.0f;
            if (m.isPet)
            {
                PartyMemberFacts const* owner = world.FindMember(m.ownerGuid);
                return owner && owner->isBot ? 1.0f : PLAYER_TRUST;
            }
            return PLAYER_TRUST;
        }

        float MeasuredDps(EnemyFacts const& enemy, ObjectGuid source)
        {
            for (DealtRate const& r : enemy.dealtBy)
                if (r.source == source)
                    return r.dps;
            return 0.0f;
        }
    }

    uint32 FlightMs(Position3 const& from, EnemyFacts const& enemy, float speed)
    {
        if (speed <= 0.0f)
            return 0;
        float const distance = std::max(5.0f, PackResolver::Distance(from, enemy.pos) - enemy.boundingRadius);
        return uint32(distance / speed * 1000.0f);
    }

    uint32 FillerLandMs(PartyMemberFacts const& self, EnemyFacts const& enemy)
    {
        if (self.pendingHit.target == enemy.guid)
            return self.pendingHit.landMs;
        if (!self.fillerCastMs)
            return 0;
        uint32 const busy = self.casting ? self.castRemainingMs : 0;
        return busy + self.fillerCastMs + FlightMs(self.pos, enemy, self.fillerSpeed);
    }

    float OthersWithin(WorldModel const& world, EnemyFacts const& enemy, ObjectGuid self, uint32 landMs)
    {
        float damage = 0.0f;
        for (PartyMemberFacts const& m : world.party)
        {
            if (m.guid == self || !m.alive)
                continue;
            if (enemy.flee.fleeing && MeleeLike(m))
                continue;
            float const trust = Trust(world, m);
            uint32 rateFromMs = 0;
            if (!m.pendingHit.target.IsEmpty())
            {
                if (m.pendingHit.target == enemy.guid && m.pendingHit.landMs < landMs)
                    damage += m.pendingHit.damage * trust;
                rateFromMs = m.pendingHit.landMs;
            }
            if (m.casting)
                rateFromMs = std::max(rateFromMs, m.castRemainingMs);
            if (m.victim != enemy.guid || m.controlled || rateFromMs >= landMs)
                continue;
            damage += MeasuredDps(enemy, m.guid) * float(landMs - rateFromMs) / 1000.0f * trust;
        }
        return damage;
    }

    bool Finished(WorldModel const& world, EnemyFacts const& enemy, ObjectGuid self, uint32 landMs)
    {
        if (!enemy.acting || !enemy.health || !landMs)
            return false;
        return OthersWithin(world, enemy, self, landMs) >= float(enemy.health) * COVER_MARGIN;
    }
}
}
