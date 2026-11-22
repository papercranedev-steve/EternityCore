/*
 * PopulationBotAI and the real players around it (population interactions, user 2026-11-22):
 * answering their emotes, raising the dead, buffs for passers-by, leaving them the creature they
 * were about to fight, sorry for a creature it hit first. Map thread of the bot.
 */

#include "Population/PopulationBotAI.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Shared/BotChat.h"
#include "Shared/I18n.h"
#include "Shared/Phrases.h"
#include "Shared/PlayerClaims.h"
#include "Life/Errands/ErrandCommon.h"
#include "Corpse.h"
#include "Creature.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Spell.h"
#include "ThreatManager.h"
#include "Timer.h"
#include "Utilities/Random.h"
#include <mutex>
#include <unordered_map>

namespace Population
{
    namespace
    {
        // Raising a dead player: the bot that claimed him (until the claim's time, all map threads).
        uint32 const RAISE_CLAIM_MS = 45 * IN_MILLISECONDS;
        // He was asked (accept or decline is his): nobody asks again for this long.
        uint32 const RAISE_ASKED_MS = 5 * MINUTE * IN_MILLISECONDS;
        uint32 const MOURN_AGAIN_MS = 10 * MINUTE * IN_MILLISECONDS;
        float const RAISE_MAX_DETOUR = 2.5f;

        std::mutex g_claimLock;
        std::unordered_map<uint64, uint32> g_raiseClaims;   // dead player -> claimed until
        std::unordered_map<uint64, uint32> g_lastBuff;      // player -> a bot buffed him at

        // Lets entries go (their time is up).
        void Prune(std::unordered_map<uint64, uint32>& map, uint32 now, uint32 keepMs, bool isUntil)
        {
            if (map.size() <= 64)
                return;
            for (auto it = map.begin(); it != map.end();)
                it = (isUntil ? now >= it->second : now - it->second > keepMs) ? map.erase(it) : std::next(it);
        }

        bool ClaimRaise(ObjectGuid dead, uint32 now, uint32 forMs)
        {
            std::lock_guard<std::mutex> lock(g_claimLock);
            Prune(g_raiseClaims, now, 0, true);
            auto it = g_raiseClaims.find(dead.GetRawValue());
            if (it != g_raiseClaims.end() && now < it->second)
                return false;
            g_raiseClaims[dead.GetRawValue()] = now + forMs;
            return true;
        }

        void HoldRaise(ObjectGuid dead, uint32 untilMs)
        {
            std::lock_guard<std::mutex> lock(g_claimLock);
            g_raiseClaims[dead.GetRawValue()] = untilMs;
        }

        void ReleaseRaise(ObjectGuid dead)
        {
            std::lock_guard<std::mutex> lock(g_claimLock);
            g_raiseClaims.erase(dead.GetRawValue());
        }

        bool ClaimBuff(ObjectGuid player, uint32 now, uint32 gapMs)
        {
            std::lock_guard<std::mutex> lock(g_claimLock);
            Prune(g_lastBuff, now, gapMs, false);
            auto it = g_lastBuff.find(player.GetRawValue());
            if (it != g_lastBuff.end() && now - it->second < gapMs)
                return false;
            g_lastBuff[player.GetRawValue()] = now;
            return true;
        }

        bool Recently(std::deque<std::pair<ObjectGuid, uint32>>& list, ObjectGuid guid, uint32 now, uint32 keepMs)
        {
            while (!list.empty() && now - list.front().second > keepMs)
                list.pop_front();
            for (auto const& e : list)
                if (e.first == guid)
                    return true;
            return false;
        }

        // Where a dead player's body is on `me`'s map (his body unreleased, else his corpse).
        bool BodyOf(Player* me, Player* dead, float& x, float& y, float& z)
        {
            if (dead->GetDeathState() == CORPSE)
            {
                x = dead->GetPositionX();
                y = dead->GetPositionY();
                z = dead->GetPositionZ();
                return true;
            }
            Corpse* corpse = sObjectAccessor.GetCorpseForPlayerGUID(dead->GetObjectGuid());
            if (!corpse || corpse->GetType() == CORPSE_BONES || me->GetMap()->GetCorpse(corpse->GetObjectGuid()) != corpse)
                return false;
            x = corpse->GetPositionX();
            y = corpse->GetPositionY();
            z = corpse->GetPositionZ();
            return true;
        }

        void SayTo(Player* me, Player* reader, char const* key, std::vector<std::string> const& tags)
        {
            BotShared::BotChat::Say(me, BotShared::Phrases::Say(BotShared::I18n::Of(reader), key, tags, {}));
        }
    }

    void PopulationBotAI::ReceiveEmote(Player* from, uint32 textEmote)
    {
        if (!me || !m_initialized || m_phase != Phase::Living)
            return;
        if (!m_emoteReplyInit)
        {
            Life::Pastimes::EmoteReply::Init(m_emoteReply, me->GetObjectGuid().GetRawValue());
            m_emoteReplyInit = true;
        }
        Life::Pastimes::EmoteReply::Heard(me, m_emoteReply, from, textEmote, WorldTimer::getMSTime());
    }

    bool PopulationBotAI::UpdateEmoteReply(bool canStand)
    {
        if (!m_emoteReplyInit)
            return false;
        uint32 const now = WorldTimer::getMSTime();
        bool const dancing = Life::Pastimes::EmoteReply::Update(me, m_emoteReply, canStand, now);
        // Insulted (a timid one): off to another place of the town.
        if (Life::Pastimes::EmoteReply::TakeWalkOff(m_emoteReply) && m_townLife.IsStarted())
            m_townLife.Stop(me);
        return dancing;
    }

    bool PopulationBotAI::UpdateRaiseStranger()
    {
        uint32 const now = WorldTimer::getMSTime();

        if (!m_raising.IsEmpty())
        {
            Player* dead = me->GetMap()->GetPlayer(m_raising);
            float x, y, z;
            if (!dead || dead->IsAlive() || dead->IsRessurectRequested() || !m_resurrectionSpell ||
                now >= m_raiseDeadlineMs || !BodyOf(me, dead, x, y, z))
            {
                if (dead && !dead->IsRessurectRequested())
                    ReleaseRaise(m_raising);
                m_raising.Clear();
                Life::Errands::StopWalk(me);
                return false;
            }
            bool const inReach = me->GetDistance(x, y, z, SizeFactor::None) <= RAISE_CAST_YD &&
                                 me->IsWithinLOS(x, y, z);
            if (!inReach)
            {
                if (!me->IsMoving())
                    me->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_PATHFINDING | MOVE_RUN_MODE | MOVE_EXCLUDE_STEEP_SLOPES);
                return true;
            }
            if (me->IsMoving())
                me->StopMoving();
            if (me->IsMounted())
                me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
            if (CastResurrection(m_resurrectionSpell, m_raising))
            {
                // His to accept now; nobody else asks him for a while.
                HoldRaise(m_raising, now + RAISE_ASKED_MS);
                if (urand(0, 99) < RAISE_SAY_PCT)
                    SayTo(me, dead, "raise.say", m_request.tags);
                m_raising.Clear();
            }
            return true;
        }

        if (now < m_nextRaiseCheckMs)
            return false;
        m_nextRaiseCheckMs = now + RAISE_CHECK_MS;

        for (auto const& ref : me->GetMap()->GetPlayers())
        {
            Player* p = ref.getSource();
            if (!p || p == me || p->IsBot() || p->IsAlive() || !p->IsInWorld() || p->GetTeam() != me->GetTeam() ||
                p->IsRessurectRequested())
                continue;
            float x, y, z;
            if (!BodyOf(me, p, x, y, z) || me->GetDistance(x, y, z, SizeFactor::None) > RAISE_SEE_YD ||
                !me->IsWithinLOS(x, y, z))
                continue;

            bool const canRaise = m_resurrectionSpell &&
                me->GetPower(Powers(m_resurrectionSpell->powerType)) >= Spell::CalculatePowerCost(m_resurrectionSpell, me);
            if (canRaise && Life::Errands::WalkIsSafe(me, x, y, z, RAISE_MAX_DETOUR) &&
                ClaimRaise(p->GetObjectGuid(), now, RAISE_CLAIM_MS))
            {
                Life::Prowl::Stop(me, m_prowl, true);
                m_raising = p->GetObjectGuid();
                m_raiseDeadlineMs = now + RAISE_GIVE_UP_MS;
                return true;
            }

            // Not one who raises (or another bot does): a moment of sorrow for him, once.
            if (me->GetDistance(x, y, z, SizeFactor::None) <= MOURN_YD &&
                !Recently(m_mourned, p->GetObjectGuid(), now, MOURN_AGAIN_MS))
            {
                m_mourned.emplace_back(p->GetObjectGuid(), now);
                if (urand(0, 99) < MOURN_PCT)
                {
                    if (!me->IsMoving())
                        me->SetFacingTo(me->GetAngle(x, y));
                    BotShared::BotChat::TextEmote(me, urand(0, 1) ? TEXTEMOTE_MOURN : TEXTEMOTE_CRY,
                        p->GetDeathState() == CORPSE ? p : nullptr);
                }
            }
        }
        return false;
    }

    bool PopulationBotAI::UpdatePassingBuff()
    {
        uint32 const now = WorldTimer::getMSTime();
        if (now < m_nextBuffCheckMs)
            return false;
        m_nextBuffCheckMs = now + BUFF_CHECK_MS;
        if (me->IsMounted() || me->GetPowerType() != POWER_MANA || me->GetPowerPercent(POWER_MANA) < float(BUFF_MIN_MANA_PCT))
            return false;

        SpellEntry const* forMana = nullptr;     // a buff for those with mana
        SpellEntry const* forOthers = nullptr;   // for the others
        switch (me->GetClass())
        {
            case CLASS_MAGE:
                forMana = m_spells.mage.pArcaneIntellect;
                break;
            case CLASS_PRIEST:
                forMana = forOthers = m_spells.priest.pPowerWordFortitude;
                break;
            case CLASS_DRUID:
                forMana = forOthers = m_spells.druid.pMarkoftheWild;
                break;
            case CLASS_PALADIN:
                forMana = m_spells.paladin.pBlessingOfWisdom;
                forOthers = m_spells.paladin.pBlessingOfMight;
                break;
            default:
                return false;
        }
        if (!forMana && !forOthers)
            return false;

        for (auto const& ref : me->GetMap()->GetPlayers())
        {
            Player* p = ref.getSource();
            if (!p || p == me || p->IsBot() || !p->IsAlive() || !p->IsInWorld() || p->IsInCombat() ||
                p->GetTeam() != me->GetTeam() || p->GetVisibility() == VISIBILITY_OFF || !me->IsWithinDist(p, BUFF_YD, false))
                continue;
            SpellEntry const* spell = p->GetPowerType() == POWER_MANA ? forMana : forOthers;
            if (!spell || !IsValidBuffTarget(p, spell) || Recently(m_buffed, p->GetObjectGuid(), now, BUFFED_AGAIN_MS) ||
                !me->IsWithinLOSInMap(p) || !CanTryToCastSpell(p, spell))
                continue;
            if (!ClaimBuff(p->GetObjectGuid(), now, BUFF_GAP_MS))
                continue;
            if (DoCastSpell(p, spell) != SPELL_CAST_OK)
                continue;
            m_buffed.emplace_back(p->GetObjectGuid(), now);
            if (urand(0, 99) < BUFF_SAY_PCT)
                SayTo(me, p, "buff.say", m_request.tags);
            // A wave, a blown kiss ... at him once he looks.
            if (urand(0, 99) < BUFF_EMOTE_PCT)
            {
                static uint32 const emotes[] = { TEXTEMOTE_WAVE, TEXTEMOTE_WAVE, TEXTEMOTE_KISS, TEXTEMOTE_SMILE,
                                                 TEXTEMOTE_SALUTE, TEXTEMOTE_BOW, TEXTEMOTE_NOD, TEXTEMOTE_CHEER };
                Life::Pastimes::Gesture::Want(m_gesture, p->GetObjectGuid(),
                    emotes[urand(0, uint32(sizeof(emotes) / sizeof(emotes[0])) - 1)], now);
            }
            return true;
        }
        return false;
    }

    void PopulationBotAI::UpdateYield()
    {
        ObjectGuid to;
        if (m_prowl.yieldedAtMs && m_prowl.yieldedAtMs != m_prowlYieldSeenMs)
        {
            m_prowlYieldSeenMs = m_prowl.yieldedAtMs;
            to = m_prowl.yieldedTo;
        }
        if (std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me))
        {
            Commander::HuntPlan const& hunt = commander->GetPlan().hunt;
            if (hunt.yieldedAtMs && hunt.yieldedAtMs != m_huntYieldSeenMs)
            {
                m_huntYieldSeenMs = hunt.yieldedAtMs;
                to = hunt.yieldedTo;
            }
        }
        uint32 const now = WorldTimer::getMSTime();
        if (to.IsEmpty() || now < m_nextYieldReactMs)
            return;
        Player* p = me->GetMap()->GetPlayer(to);
        if (!p || !p->IsInWorld() || !me->IsWithinDist(p, REACT_YD, false))
            return;
        m_nextYieldReactMs = now + YIELD_REACT_GAP_MS;
        if (urand(0, 99) < YIELD_SAY_PCT)
            SayTo(me, p, "prey.yield", m_request.tags);
        else
            Life::Pastimes::Gesture::Want(m_gesture, p->GetObjectGuid(), urand(0, 1) ? TEXTEMOTE_NOD : TEXTEMOTE_BOW, now);
    }

    bool PopulationBotAI::UpdateGesture(bool inCombat)
    {
        if (inCombat)
        {
            Life::Pastimes::Gesture::Drop(m_gesture);
            return false;
        }
        if (!Life::Pastimes::Gesture::Update(me, m_gesture, WorldTimer::getMSTime()))
            return false;
        // Its walk was ended: the prowl and the road send a new one afterwards.
        Life::Prowl::Stop(me, m_prowl, false);
        Life::Trip::RoadWalk::Pause(me, m_road);
        return true;
    }

    void PopulationBotAI::WatchSharedPrey()
    {
        Unit* victim = me->GetVictim();
        if (!victim || !victim->IsCreature())
            return;
        Creature* c = static_cast<Creature*>(victim);
        if (!c->IsTappedBy(me))
            return;
        for (HostileReference* ref : c->GetThreatManager().getThreatList())
        {
            Unit* u = ref->getTarget();
            Player* p = u ? u->ToPlayer() : nullptr;
            if (p && p != me && BotShared::PlayerClaims::OnPlayersSide(p))
            {
                m_sharedPreyWith = p->GetObjectGuid();
                return;
            }
        }
    }

    void PopulationBotAI::ApologizeForPrey()
    {
        ObjectGuid const guid = m_sharedPreyWith;
        m_sharedPreyWith.Clear();
        uint32 const now = WorldTimer::getMSTime();
        if (guid.IsEmpty() || now < m_nextApologyMs)
            return;
        Player* p = me->GetMap()->GetPlayer(guid);
        if (!p || !p->IsInWorld() || !p->IsAlive() || !me->IsWithinDist(p, REACT_YD, false))
            return;
        m_nextApologyMs = now + APOLOGY_GAP_MS;
        if (urand(0, 99) < APOLOGY_EMOTE_PCT)
            Life::Pastimes::Gesture::Want(m_gesture, p->GetObjectGuid(), TEXTEMOTE_APOLOGIZE, now);
        else
            SayTo(me, p, "prey.sorry", m_request.tags);
    }
}
