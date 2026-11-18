#include "Commander/Behaviors/Movement/BlastBehavior.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Profiles/Tags.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/Perception/WorldModel.h"
#include "TeamBotAI.h"
#include "Map.h"
#include <algorithm>
#include <cmath>

namespace Commander
{
    namespace
    {
        struct Threat
        {
            EnemyFacts const* enemy = nullptr;
            BlastFacts blast;
            float distance = 0.0f;
        };

        // The nearest stand-clear blast that reaches `self`.
        Threat Nearest(WorldModel const& world, TeamPlan const& plan, PartyMemberFacts const& self)
        {
            Threat best;
            for (DoctrineEnemy const& x : plan.doctrine.enemies)
            {
                if (!(x.stamps & STAMP_STAND_CLEAR))
                    continue;
                EnemyFacts const* e = world.FindEnemy(x.guid);
                if (!e || !e->health || e->victim == self.guid)
                    continue;
                BlastFacts const b = Tags::BlastOf(*e);
                if (!b.valid || (b.latestMs && int32(world.refreshedAtMs - b.latestMs) > 0))
                    continue;
                float const d = PackResolver::Distance(self.pos, e->pos);
                if (d >= b.radius + BlastBehavior::CLEAR_MARGIN_YD)
                    continue;
                if (!best.enemy || d < best.distance)
                {
                    best.enemy = e;
                    best.blast = b;
                    best.distance = d;
                }
            }
            return best;
        }
    }

    bool BlastBehavior::AppliesTo(TeamBotAI const* bot) const
    {
        return bot->GetRole() != ROLE_INVALID;
    }

    bool BlastBehavior::WantsTickNow(TeamBotAI const* bot, WorldModel const& world, TeamPlan const& plan) const
    {
        PartyMemberFacts const* self = world.FindMember(bot->me->GetObjectGuid());
        return self && self->alive && Nearest(world, plan, *self).enemy;
    }

    void BlastBehavior::Propose(TeamBotAI const* bot, WorldModel const& world,
                                TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        Player* me = bot->me;
        PartyMemberFacts const* self = world.FindMember(me->GetObjectGuid());
        if (!self || !self->alive)
            return;
        Threat const t = Nearest(world, plan, *self);
        if (!t.enemy)
            return;

        Position3 here;
        here.x = me->GetPositionX();
        here.y = me->GetPositionY();
        here.z = me->GetPositionZ();
        float const want = t.blast.radius + CLEAR_MARGIN_YD;
        float dx = here.x - t.enemy->pos.x;
        float dy = here.y - t.enemy->pos.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.5f)
        {
            // On top of it: any side (away from where it faces is not known).
            dx = 1.0f;
            dy = 0.0f;
            len = 1.0f;
        }
        Position3 dest;
        dest.x = t.enemy->pos.x + dx / len * want;
        dest.y = t.enemy->pos.y + dy / len * want;
        float const ground = me->GetMap()->GetHeight(dest.x, dest.y, here.z + 5.0f);
        dest.z = ground > INVALID_HEIGHT ? ground : here.z;

        float const speed = std::max(1.0f, me->GetSpeed(MOVE_RUN));
        float const walkSec = std::max(0.0f, want - len) * PATH_FACTOR / speed;
        float const blast = t.blast.damage *
            (t.blast.physical ? 1.0f - Value::ArmorReduction(uint32(self->armor), t.enemy->level) : 1.0f);
        float benefit = Value::Points(world, blast);
        if (blast >= float(self->health))
            benefit += Value::DeathPenalty(*self);

        uint32 const untilMs = t.blast.latestMs ? std::max(0, int32(t.blast.latestMs - world.refreshedAtMs)) : 0;
        Proposal p;
        p.tier = ProposalTier::Urgent;
        p.source = "blast";
        p.reason = "step out of a self-destruct's blast";
        p.action.type = BotActionType::MoveTo;
        p.action.x = dest.x;
        p.action.y = dest.y;
        p.action.z = dest.z;
        p.action.holdMs = std::min(HOLD_MAX_MS, std::max(uint32(walkSec * 1000.0f), untilMs) + HOLD_EXTRA_MS);
        p.SetValue(benefit, Value::LostAction(world, *self, walkSec * 2.0f));
        out.push_back(p);
    }
}
