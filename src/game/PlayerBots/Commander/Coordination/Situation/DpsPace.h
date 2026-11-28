/*
 * PartyBot Commander layer 2 (brain), situation: damage dealer bots do not leave the player far
 * behind on his damage meter (user 2026-11-28; .kiro/docs/2026-11-28-dps-pace.md D3). Only a
 * lead that is too big is trimmed: a bot below its cap is never touched, none is pushed below
 * the player.
 *
 * The player: a real player who deals damage and is no tank or healer (the leader first). None,
 * or fewer than MIN_FIGHTS kept fights of his (Perception/Forecast/DamageMeter.h): every bot at 1.
 * Cap per damage dealer bot (not a tank, not a healer):
 *   player dps x class ratio (CLASS_BASE of the bot's class / the player's)
 *              x gear ratio (PartyMemberFacts::gearLevel, bot / player, GEAR_MIN..GEAR_MAX)
 *              x (1 + its area damage share x AREA_BONUS)
 *              x its own float (PERSONAL_MIN..MAX, from its guid: fixed)
 * Step, each time the meter keeps a new fight: above its cap the pace goes down PACE_STEP, below
 * cap x RAISE_BELOW up PACE_STEP; PACE_MIN..1. Pace = the share of its full output: the bot
 * rests (TeamBotAI, the rotation skipped) busy x (1/pace - 1) after each routine damage cast.
 * Between fights only (the meter keeps a fight once it is over).
 * Full speed now (DpsPacePlan::full, every refresh): a boss among the enemies, a crisis, someone
 * about to die (a Save rescue at the last refresh), a healer bot below HEALER_MANA_PCT, and the
 * FULL_AFTER_DEATH fight(s) after one with a death.
 * Numbers are untested guesses; CLASS_BASE is rough vanilla single target damage.
 * Pure.
 */
#ifndef MANGOS_COMMANDER_DPS_PACE_H
#define MANGOS_COMMANDER_DPS_PACE_H
#include "Commander/Coordination/IAssigner.h"
namespace Commander
{
    struct PartyMemberFacts;
    class DpsPaceAssigner : public IAssigner
    {
    public:
        static constexpr uint32 MIN_FIGHTS = 2;
        static constexpr float GEAR_MIN = 0.8f, GEAR_MAX = 1.3f;
        static constexpr float AREA_BONUS = 0.5f;
        static constexpr float PERSONAL_MIN = 0.95f, PERSONAL_MAX = 1.15f;
        static constexpr float PACE_MIN = 0.8f;
        static constexpr float PACE_STEP = 0.05f;
        static constexpr float RAISE_BELOW = 0.9f;
        static constexpr float HEALER_MANA_PCT = 40.0f;
        static constexpr uint8 FULL_AFTER_DEATH = 1;
        char const* GetId() const override { return "dpspace"; }
        void Assign(WorldModel const& world, TeamPlan& plan) const override;
        void Clear(TeamPlan& plan) const override;
        // Rough single target damage of a class (1 = a mage); 1 for an unknown class.
        static float ClassBase(uint8 classId);
        static float Personal(ObjectGuid bot);
        // The bot's cap against `player` (header), 0 when one of the dps is unknown.
        static float CapOf(WorldModel const& world, PartyMemberFacts const& bot, PartyMemberFacts const& player);
        // The real player the pace follows (header), null = none.
        static PartyMemberFacts const* PlayerOf(WorldModel const& world);
        static bool Paced(PartyMemberFacts const& m);
    };
}
#endif
