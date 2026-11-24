#include "Commander/Perception/Values/FightManaSpend.h"
#include "Player.h"
#include "Map.h"
#include <algorithm>

namespace Commander
{
namespace FightManaSpend
{
    namespace
    {
        float ManaShare(Player const* me)
        {
            uint32 const maxMana = me->GetMaxPower(POWER_MANA);
            return maxMana ? float(me->GetPower(POWER_MANA)) / float(maxMana) : 0.0f;
        }
    }

    void Update(Player const* me, FightManaSpendState& state)
    {
        if (!me->GetMaxPower(POWER_MANA))
        {
            state.inFight = false;
            return;
        }
        bool const inCombat = me->IsInCombat();
        if (inCombat && !state.inFight)
        {
            state.inFight = true;
            state.fightInDungeon = me->GetMap()->IsDungeon();
            state.startShare = ManaShare(me);
            return;
        }
        if (inCombat || !state.inFight)
            return;

        state.inFight = false;
        // (died: the fight said nothing about what the next one costs)
        if (!me->IsAlive())
            return;
        float const spent = state.startShare - ManaShare(me);
        if (spent < MIN_SPEND_SHARE)
            return;
        FightManaSpendState::Fights& fights = state.fightInDungeon ? state.dungeon : state.openWorld;
        fights.spent[fights.count % FightManaSpendState::KEPT] = std::min(1.0f, spent);
        ++fights.count;
    }

    bool Average(FightManaSpendState const& state, bool dungeon, uint32_t minFights, float& share)
    {
        FightManaSpendState::Fights const& fights = dungeon ? state.dungeon : state.openWorld;
        if (fights.count < std::max<uint32_t>(1, minFights))
            return false;
        uint32_t const n = std::min(fights.count, FightManaSpendState::KEPT);
        float sum = 0.0f;
        for (uint32_t i = 0; i < n; ++i)
            sum += fights.spent[i];
        share = sum / float(n);
        return true;
    }
}
}
