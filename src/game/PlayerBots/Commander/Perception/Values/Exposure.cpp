#include "Commander/Perception/Values/Exposure.h"
#include "Commander/Perception/Values/ValueModel.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
namespace Exposure
{
    float Gap(EnemyFacts const& e, Position3 const& p)
    {
        float const dx = p.x - e.pos.x, dy = p.y - e.pos.y;
        return std::max(0.0f, std::sqrt(dx * dx + dy * dy) - e.boundingRadius);
    }

    float Loss(WorldModel const& world, PartyMemberFacts const& member, EnemyFacts const& e, float gapYd)
    {
        float const exposed = MAX_GAIN_SEC - gapYd / ENEMY_RUN_SPEED;
        if (exposed <= 0.0f)
            return 0.0f;
        return TURN_CHANCE * (Value::HitLoss(world, e, member, exposed) + Value::LostAction(world, member, exposed));
    }

    float LossAt(WorldModel const& world, PartyMemberFacts const& member,
                 std::vector<EnemyFacts const*> const& enemies, Position3 const& p)
    {
        float loss = 0.0f;
        for (EnemyFacts const* e : enemies)
            loss += Loss(world, member, *e, Gap(*e, p));
        return loss;
    }

    std::vector<EnemyFacts const*> Turnable(WorldModel const& world, ObjectGuid member)
    {
        std::vector<EnemyFacts const*> out;
        for (EnemyFacts const& e : world.enemies)
            if (e.acting && !e.onAllyOnly && e.victim != member)
                out.push_back(&e);
        return out;
    }
}
}
