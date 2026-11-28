#include "Population/PopulationBotAI.h"
#include "Population/PopulationMgr.h"
#include "Population/StrangerTalk.h"
#include "Companions/CompanionMgr.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Team.h"
#include "Commander/Coordination/Situation/Hunt/HuntAssigner.h"
#include "Shared/ClassTraining.h"
#include "Shared/Inventory.h"
#include "Shared/Mounts.h"
#include "RoleCapability.h"
#include "PlayerBotMgr.h"
#include "Player.h"
#include "GameObject.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Shared/BotChat.h"
#include "Shared/Phrases.h"
#include "Utilities/Random.h"
#include "MotionMaster.h"
#include "GridMap.h"
#include "SpellMgr.h"
#include "Log.h"
#include <algorithm>
#include <limits>
#include <vector>
#include <cmath>

namespace Population
{
    namespace
    {
        // What he says turning down a plain group invite now and then (i18n "popbot.decline"),
        // and why a roster member does not join the player as a companion (DeclineRecruit):
        // "popbot.noslot", "meet.join.toohigh", "popbot.farewell".

        uint32 const g_idleEmotes[] = {
            EMOTE_ONESHOT_TALK, EMOTE_ONESHOT_TALK, EMOTE_ONESHOT_TALK,
            EMOTE_ONESHOT_LAUGH, EMOTE_ONESHOT_POINT, EMOTE_ONESHOT_QUESTION,
            EMOTE_ONESHOT_EXCLAMATION, EMOTE_ONESHOT_BOW, EMOTE_ONESHOT_CHEER, EMOTE_ONESHOT_WAVE,
        };

        // Crafting recipes without tools or a spell focus (checked in the mangos DB, 2026-11-01).
        struct Recipe
        {
            uint32 skill;       // skill line
            uint32 spell;
            uint32 reqSkill;    // skill value the recipe is learned at
        };

        uint32 const SKILL_LINE_FIRST_AID = 129;
        uint32 const SKILL_LINE_TAILORING = 197;
        uint32 const SKILL_LINE_LEATHERWORKING = 165;
        uint32 const SKILL_LINE_ALCHEMY = 171;

        Recipe const g_recipes[] = {
            // First Aid: bandages
            { SKILL_LINE_FIRST_AID, 3275, 1 },   { SKILL_LINE_FIRST_AID, 3276, 40 },  { SKILL_LINE_FIRST_AID, 3277, 80 },
            { SKILL_LINE_FIRST_AID, 3278, 115 }, { SKILL_LINE_FIRST_AID, 7928, 150 }, { SKILL_LINE_FIRST_AID, 7929, 180 },
            { SKILL_LINE_FIRST_AID, 10840, 210 }, { SKILL_LINE_FIRST_AID, 10841, 240 }, { SKILL_LINE_FIRST_AID, 18629, 260 },
            { SKILL_LINE_FIRST_AID, 18630, 290 },
            // Tailoring: bolts of cloth
            { SKILL_LINE_TAILORING, 2963, 1 }, { SKILL_LINE_TAILORING, 2964, 75 }, { SKILL_LINE_TAILORING, 3839, 125 },
            { SKILL_LINE_TAILORING, 3865, 175 }, { SKILL_LINE_TAILORING, 18401, 250 },
            // Leatherworking: leather and armor kits
            { SKILL_LINE_LEATHERWORKING, 2881, 1 }, { SKILL_LINE_LEATHERWORKING, 2152, 1 }, { SKILL_LINE_LEATHERWORKING, 20648, 100 },
            { SKILL_LINE_LEATHERWORKING, 2165, 100 }, { SKILL_LINE_LEATHERWORKING, 20649, 150 }, { SKILL_LINE_LEATHERWORKING, 3780, 150 },
            { SKILL_LINE_LEATHERWORKING, 20650, 200 }, { SKILL_LINE_LEATHERWORKING, 10487, 200 }, { SKILL_LINE_LEATHERWORKING, 19058, 250 },
            // Alchemy: healing / mana potions, elixirs
            { SKILL_LINE_ALCHEMY, 2330, 1 }, { SKILL_LINE_ALCHEMY, 2331, 25 }, { SKILL_LINE_ALCHEMY, 2337, 55 },
            { SKILL_LINE_ALCHEMY, 3447, 110 }, { SKILL_LINE_ALCHEMY, 3173, 120 }, { SKILL_LINE_ALCHEMY, 7181, 155 },
            { SKILL_LINE_ALCHEMY, 11448, 205 }, { SKILL_LINE_ALCHEMY, 11457, 215 }, { SKILL_LINE_ALCHEMY, 17553, 260 },
            { SKILL_LINE_ALCHEMY, 17556, 275 }, { SKILL_LINE_ALCHEMY, 17571, 280 }, { SKILL_LINE_ALCHEMY, 17580, 295 },
        };

        // Skill a player of this level would typically have.
        uint32 CraftSkillForLevel(uint32 level)
        {
            return std::max<uint32>(1, std::min<uint32>(300, level * 5));
        }

        // A premade gear template for the class at level 60 whose name contains `tag`
        // ("phase6", "preraid"), preferring the bot's role.
        uint32 FindGearTemplate(uint8 classId, CombatBotRoles role, char const* tag)
        {
            uint32 anyRole = 0;
            for (auto const& itr : sObjectMgr.GetPlayerPremadeGearTemplates())
            {
                PlayerPremadeGearTemplate const& t = itr.second;
                if (t.requiredClass != classId || t.level != 60 || t.name.find(tag) == std::string::npos)
                    continue;
                if (t.role == role)
                    return t.entry;
                if (!anyRole)
                    anyRole = t.entry;
            }
            return anyRole;
        }
    }

    PopulationBotAI::PopulationBotAI(SpawnRequest const& request, std::string name, std::shared_ptr<BotHandle> handle)
        : TeamBotAI(), m_request(request), m_name(std::move(name)), m_handle(std::move(handle))
    {
        m_handle->ai = this;
        m_updateTimer.Reset(1000);
    }

    PopulationBotAI::~PopulationBotAI()
    {
        if (!m_teamGuid.IsEmpty())
            Commander::Team::LeaveUnit(m_teamGuid);
        m_handle->alive = false;
        m_handle->ai = nullptr;
    }

    bool PopulationBotAI::OnSessionLoaded(PlayerBotEntry* /*entry*/, WorldSession* sess)
    {
        if (m_request.hasEntrance)
        {
            float const o = std::atan2(m_request.y - m_request.entranceY, m_request.x - m_request.entranceX);
            return SpawnNewPlayer(sess, m_request.classId, m_request.race, m_request.mapId, 0,
                m_request.entranceX, m_request.entranceY, m_request.entranceZ, o < 0.0f ? o + 2.0f * float(M_PI) : o,
                nullptr, m_name.c_str(), m_request.hasLook ? &m_request.look : nullptr);
        }
        return SpawnNewPlayer(sess, m_request.classId, m_request.race, m_request.mapId, 0,
            m_request.x, m_request.y, m_request.z, m_request.o, nullptr, m_name.c_str(),
            m_request.hasLook ? &m_request.look : nullptr);
    }

    void PopulationBotAI::OnPlayerLogin()
    {
        if (!m_initialized)
            me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);
    }

    void PopulationBotAI::UpdateAI(uint32 const diff)
    {
        UpdateSocial(diff);

        // Someone is talking with him: he stands and listens.
        if (m_talkHoldUntilMs && UpdateTalkHold())
            return;

        if (m_request.kind == BotKind::Field || m_request.kind == BotKind::Traveller)
        {
            UpdateField(diff);
            return;
        }

        m_updateTimer.Update(diff);
        if (!m_updateTimer.Passed())
            return;
        m_updateTimer.Reset(sPopulationMgr.GetUpdateInterval());

        BotTickCostScope cost;

        if (!me->IsInWorld() || me->IsBeingTeleported())
            return;

        if (!m_initialized)
        {
            Initialize();
            return;
        }

        if (me->IsDead())
            return;

        if (m_handle->leave && m_phase != Phase::Leaving && m_phase != Phase::Gone)
            BeginLeaving();

        switch (m_phase)
        {
            case Phase::Entering: UpdateEntering(); return;
            case Phase::Leaving:  UpdateLeaving(); return;
            case Phase::Gone:     return;
            case Phase::Living:   break;
        }

        if (m_request.kind == BotKind::Herald)
        {
            UpdateHerald();
            return;
        }

        if (m_reviewUntilMs)
        {
            if (WorldTimer::getMSTime() < m_reviewUntilMs)
                return;
            EndReview();
        }

        // A real player's emote at him: answered (a dance along holds him still).
        if (UpdateEmoteReply(!me->IsMoving()))
            return;

        if (LivesTownLife())
        {
            uint32 const now = WorldTimer::getMSTime();
            if (!m_townLife.IsStarted())
                m_townLife.Begin(me, m_request, now);
            m_townLife.Update(me, now);
            // Done what it came for: the manager walks it out of sight (Retire).
            if (m_townLife.WantsToLeave())
                m_handle->done = true;
            return;
        }

        switch (m_request.role)
        {
            case CityRole::Crafter: UpdateCrafter(); break;
            default:                UpdateIdle(); break;
        }
    }

    bool PopulationBotAI::LivesTownLife() const
    {
        if (m_request.kind != BotKind::City && m_request.kind != BotKind::Town)
            return false;
        if (!m_request.places || m_request.places->empty())
            return false;
        return m_request.role == CityRole::Stand || m_request.role == CityRole::Mover || m_request.role == CityRole::Sitter ||
               m_request.role == CityRole::Gatherer;
    }

    void PopulationBotAI::InitializeField()
    {
        // Below the premade specs (19) a population bot knows no class spell: the trainer's
        // lessons for its level, free (what it learned before we met it).
        BotShared::ClassTraining::LearnAvailable(me, false);
        // Its best role, as the Commander gives a team of one (RoleAssigner). A healer alone
        // deals damage too (TeamPlan::coverage, built in its first fight if it is attacked right
        // after login: a druid once healed itself under a wolf for minutes with no target).
        m_role = RoleCapabilities::Assess(me).Best();
        ResetSpellData();
        PopulateSpellData();
        m_rotationCreated = false;
        Rotation();
        SummonPetIfNeeded();
        m_fightsToLeave = urand(FIELD_FIGHTS_MIN, FIELD_FIGHTS_MAX);

        // A team of one for the Commander.
        m_teamGuid = me->GetObjectGuid();
        Commander::Team::JoinUnit(m_teamGuid, m_request.unitId);

        // Acting out a quest (FieldDirector::BuildQuestRequest): where it starts.
        if (m_request.kind == BotKind::Field && m_request.quest.step != QuestStep::None)
            BeginQuestStep(m_request.quest.step, m_request.quest.startAlong);

        if (m_request.kind == BotKind::Traveller)
        {
            // On its race's mount from level 40 (most of them), already riding: it logged in out of sight.
            if (m_request.rides && me->GetLevel() >= BotShared::Mounts::FIRST_MOUNT_LEVEL)
                m_mountSpell = BotShared::Mounts::RacialMountSpell(me->GetRace(), me->GetGUIDLow());
            if (m_mountSpell)
                BotShared::Mounts::Mount(me, m_mountSpell, true);
            std::vector<Commander::Roads::RoadPoint> line;
            for (Waypoint const& w : m_request.route)
                line.push_back({ w.x, w.y, w.z });
            // Walking with someone: the leader lets the follower find him.
            if (m_request.party && m_request.partyLeader)
                m_request.party->leader = me->GetObjectGuid();
            // Players run; a few walk (two together always run: the follower keeps up).
            bool const run = m_mountSpell || m_request.party || urand(0, 99) < TRAVELLER_RUN_PCT;
            Life::Trip::RoadWalk::Begin(m_road, std::move(line), run, WorldTimer::getMSTime());
        }
    }

    void PopulationBotAI::UpdateRoad()
    {
        if (m_roadDone)
            return;
        uint32 const now = WorldTimer::getMSTime();

        // Walking with someone: by his side while he is here (else on along the road alone).
        if (m_request.party && !m_request.partyLeader && !m_request.party->leader.IsEmpty())
        {
            Player* leader = me->GetMap()->GetPlayer(m_request.party->leader);
            if (leader && leader->IsInWorld() && leader->IsAlive() && me->IsWithinDistInMap(leader, PARTY_KEEP_YD))
            {
                if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                    me->GetMotionMaster()->MoveFollow(leader, PAIR_FOLLOW_YD, float(M_PI) / 2.0f);
                return;
            }
        }

        // Back on the mount after a fight or a meal (a real cast: it stands still for it).
        if (m_mountSpell && !me->IsMounted() && m_mountTries < MOUNT_TRIES)
        {
            if (me->IsNonMeleeSpellCasted(false))
                return;
            Life::Trip::RoadWalk::Pause(me, m_road);
            if (me->IsMoving())
                return;
            if (BotShared::Mounts::Mount(me, m_mountSpell, false))
                m_mountTries = 0;
            else
                ++m_mountTries;
            return;
        }

        switch (Life::Trip::RoadWalk::Update(me, m_road, now))
        {
            case Life::Trip::RoadWalkResult::Walking:
                return;
            case Life::Trip::RoadWalkResult::Arrived:
            case Life::Trip::RoadWalkResult::Stuck:
                // At the road's end (in town), or no way on: done here, the manager lets it go
                // once nobody sees it (walking into the town out of sight first if someone does).
                m_roadDone = true;
                m_handle->done = true;
                if (me->IsMounted())
                    me->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
                return;
        }
    }

    void PopulationBotAI::UpdateField(uint32 diff)
    {
        // Every frame: the Commander's mid-cast checks, movement hands, stance swap.
        if (m_initialized && me->IsInWorld() && UpdateFrameHands(diff))
            return;

        m_updateTimer.Update(diff);
        if (!m_updateTimer.Passed())
            return;
        m_updateTimer.Reset(sCommanderMgr.GetBotUpdateInterval());

        BotTickCostScope cost;

        if (!me->IsInWorld() || me->IsBeingTeleported())
            return;

        if (!m_initialized)
        {
            Initialize();
            return;
        }

        if (m_resetSpellData)
        {
            ResetSpellData();
            PopulateSpellData();
            m_resetSpellData = false;
        }

        sCommanderMgr.OnBotUpdate(this);

        // Falling, controlled, dead (the corpse run), auto-repeating, casting.
        if (UpdateBody())
            return;

        // A fight over: one more done; enough of them, or nothing left to hunt here: moving on.
        bool const inCombat = me->IsInCombat();
        bool const traveller = m_request.kind == BotKind::Traveller;
        bool const quester = m_questStep != QuestStep::None;
        if (quester)
            CountQuestKills(inCombat);
        if (inCombat)
            WatchSharedPrey();
        uint32 const tickMs = WorldTimer::getMSTime();
        if (m_wasInCombat && !inCombat)
        {
            ++m_fights;
            ApologizeForPrey();
            // (user 2026-11-28: off to the next thing the instant the creature fell looked
            // like a machine)
            m_afterFightUntilMs = tickMs + urand(AFTER_FIGHT_MIN_MS, AFTER_FIGHT_MAX_MS);
        }
        if (inCombat)
            m_afterFightUntilMs = 0;
        m_wasInCombat = inCombat;
        bool const afterFight = !inCombat && tickMs < m_afterFightUntilMs;
        if (quester && !inCombat && !afterFight)
            AdvanceQuest();

        // Done (a traveller at the road's end, a field bot moving on) in someone's sight: out of
        // sight first (the manager picked the spot: PopulationMgr::RetireTravellers / RetireFieldBots).
        if (!inCombat && m_handle->leave && m_phase != Phase::Gone)
        {
            Life::Prowl::Stop(me, m_prowl, false);
            if (m_phase != Phase::Leaving)
                BeginLeaving();
            else
                UpdateLeaving();
            return;
        }

        if (!traveller && !quester && !inCombat && !m_handle->done)
        {
            std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
            uint32 const idleSince = commander ? commander->GetPlan().hunt.idleSinceMs : 0;
            if (m_fights >= m_fightsToLeave ||
                (idleSince && WorldTimer::getMSTimeDiff(idleSince, WorldTimer::getMSTime()) >= FIELD_IDLE_LEAVE_MS))
                m_handle->done = true;
        }

        bool const commanderOutOfCombat = !inCombat && sCommanderMgr.WantsOutOfCombatControl(this);

        // Eating and drinking are the Commander's (RestBehavior), as for party bots.
        EndRestWhenDone();
        bool const needsRest = !inCombat && NeedsRest();
        bool const resting = !inCombat && IsResting();
        if (!resting && StandUpBeat(inCombat))
            return;

        if (!inCombat && !me->IsMounted() && !resting)
        {
            UpdateOutOfCombatAI();      // buffs, pet
            if (m_isBuffing || me->IsNonMeleeSpellCasted())
                return;
        }

        // Real players around it: answers to their emotes, the dead raised, buffs for passers-by,
        // a nod for the creature left to one of them.
        UpdatePendingLine();
        if (inCombat)
        {
            UpdateGesture(true);
            DropPassingBuff();
        }
        else
        {
            // (it stops for the answer: the prowl and the road send a new walk afterwards)
            if (UpdateEmoteReply(!resting))
            {
                Life::Prowl::Stop(me, m_prowl, false);
                Life::Trip::RoadWalk::Pause(me, m_road);
                return;
            }
            if (!resting && !me->GetVictim() && UpdateGesture(false))
                return;
            if (!resting && !me->GetVictim())
            {
                if (UpdateRaiseStranger())
                    return;
                if (UpdatePassingBuff())
                    return;
            }
            UpdateYield();
        }

        // A fight just over: it stands a moment before the loot, the way back, the next fight.
        if (afterFight && !me->GetVictim())
        {
            if (!me->IsStopped())
                me->StopMoving();
            return;
        }

        // After a fight: its loot, as a player (not with a new victim chosen already).
        if (!inCombat && !resting && !me->GetVictim() && Life::LootRun::Update(me, m_lootRun, nullptr))
            return;
        if (inCombat)
        {
            Life::LootRun::Stop(me, m_lootRun);
            Life::Prowl::Stop(me, m_prowl, false);
        }
        else if (!HuntIdle())
            Life::Prowl::Stop(me, m_prowl, false);     // the hunt found a fight: the Commander walks it there

        // The Commander picks the next fight (HuntAssigner; not for a traveller) and fights it.
        if (RunCommanderHook(commanderOutOfCombat, needsRest, resting))
            return;

        StopTakenOverMovement();
        if (traveller && !inCombat && !resting && !me->GetVictim())
        {
            UpdateRoad();
            return;
        }
        if (!inCombat && !resting && !me->GetVictim() && UpdateProwl())
            return;
        if (quester && !inCombat && !resting && !me->GetVictim())
        {
            UpdateQuestWalk();
            return;
        }
        FightVictim(me->GetVictim());
    }

    std::vector<Commander::Roads::RoadPoint> PopulationBotAI::QuestLine(bool towardsArea, float from) const
    {
        std::vector<Waypoint> const& way = m_request.quest.way;
        std::vector<Commander::Roads::RoadPoint> line;
        if (way.size() < 2)
            return line;
        std::vector<float> along(way.size(), 0.0f);
        for (size_t i = 1; i < way.size(); ++i)
            along[i] = along[i - 1] + std::sqrt((way[i].x - way[i - 1].x) * (way[i].x - way[i - 1].x) +
                                                (way[i].y - way[i - 1].y) * (way[i].y - way[i - 1].y));
        from = std::min(std::max(from, 0.0f), along.back());

        // Where it is now, then the way's points on from there.
        line.push_back({ me->GetPositionX(), me->GetPositionY(), me->GetPositionZ() });
        if (towardsArea)
        {
            for (size_t i = 0; i < way.size(); ++i)
                if (along[i] > from)
                    line.push_back({ way[i].x, way[i].y, way[i].z });
        }
        else
        {
            for (size_t i = way.size(); i-- > 0;)
                if (along[i] < from)
                    line.push_back({ way[i].x, way[i].y, way[i].z });
        }
        return line;
    }

    void PopulationBotAI::BeginQuestStep(QuestStep step, float from)
    {
        uint32 const now = WorldTimer::getMSTime();
        m_questStep = step;
        m_questStepSinceMs = now;
        switch (step)
        {
            case QuestStep::ToArea:
                Life::Trip::RoadWalk::Begin(m_road, QuestLine(true, from), true, now);
                break;
            case QuestStep::ToGiver:
                if (m_request.quest.way.size() < 2)
                {
                    // No way back known: done here (logs out out of sight, as a field bot).
                    m_questStep = QuestStep::AtGiver;
                    m_questDoneAtMs = now;
                    m_handle->done = true;
                    return;
                }
                Life::Trip::RoadWalk::Begin(m_road, QuestLine(false, from), true, now);
                break;
            case QuestStep::Hunt:
                break;
            case QuestStep::AtGiver:
            {
                // Handing it in: by the giver a moment, talking.
                FacePoint(m_request.quest.giverX, m_request.quest.giverY);
                me->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
                m_questDoneAtMs = now + urand(AT_GIVER_MIN_MS, AT_GIVER_MAX_MS);
                break;
            }
            case QuestStep::None:
                break;
        }
    }

    void PopulationBotAI::CountQuestKills(bool inCombat)
    {
        if (inCombat)
        {
            // Its quest's creatures it fights now (counted once the fight is over and they are dead).
            Unit* victim = me->GetVictim();
            if (victim && victim->GetTypeId() == TYPEID_UNIT && victim->GetEntry() == m_request.quest.preyEntry &&
                std::find(m_preyFought.begin(), m_preyFought.end(), victim->GetObjectGuid()) == m_preyFought.end())
                m_preyFought.push_back(victim->GetObjectGuid());
            return;
        }
        for (ObjectGuid const& guid : m_preyFought)
        {
            Creature* creature = me->GetMap()->GetCreature(guid);
            if (!creature || !creature->IsAlive())
                ++m_questKills;
        }
        m_preyFought.clear();
    }

    void PopulationBotAI::AdvanceQuest()
    {
        uint32 const now = WorldTimer::getMSTime();
        if (m_questStep == QuestStep::Hunt)
        {
            // Enough of them, or long enough at it (the quest is acted: it goes back anyway).
            if (m_questKills >= m_request.quest.need ||
                WorldTimer::getMSTimeDiff(m_questStepSinceMs, now) >= HUNT_GIVE_UP_MS)
                BeginQuestStep(QuestStep::ToGiver, std::numeric_limits<float>::max());
            return;
        }
        if (m_questStep == QuestStep::AtGiver && !m_handle->done && now >= m_questDoneAtMs)
            m_handle->done = true;     // handed in: the manager lets it go out of sight
    }

    void PopulationBotAI::UpdateQuestWalk()
    {
        uint32 const now = WorldTimer::getMSTime();
        switch (m_questStep)
        {
            case QuestStep::ToArea:
            case QuestStep::ToGiver:
            {
                Life::Trip::RoadWalkResult const r = Life::Trip::RoadWalk::Update(me, m_road, now);
                if (r == Life::Trip::RoadWalkResult::Walking)
                    return;
                if (m_questStep == QuestStep::ToArea)
                    BeginQuestStep(QuestStep::Hunt, 0.0f);
                else if (r == Life::Trip::RoadWalkResult::Arrived)
                    BeginQuestStep(QuestStep::AtGiver, 0.0f);
                else
                {
                    m_questStep = QuestStep::AtGiver;   // stuck on the way back: done here
                    m_questDoneAtMs = now;
                }
                return;
            }
            case QuestStep::Hunt:       // nothing to hunt: UpdateProwl walks it about the area
            case QuestStep::AtGiver:
            case QuestStep::None:
                return;
        }
    }

    bool PopulationBotAI::HuntIdle() const
    {
        std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
        if (!commander)
            return false;
        Commander::HuntPlan const& hunt = commander->GetPlan().hunt;
        return !hunt.Active() && hunt.idleSinceMs &&
               WorldTimer::getMSTimeDiff(hunt.idleSinceMs, WorldTimer::getMSTime()) >= PROWL_AFTER_MS;
    }

    bool PopulationBotAI::UpdateProwl()
    {
        // Field bots hunting (on their own, or a quest's creatures); not on a walk of the quest.
        if (m_request.kind != BotKind::Field || (m_questStep != QuestStep::None && m_questStep != QuestStep::Hunt) ||
            !HuntIdle())
        {
            Life::Prowl::Stop(me, m_prowl, false);
            return false;
        }

        uint32 const level = me->GetLevel();
        Life::ProwlRules rules;
        rules.minLevel = level > Commander::HuntAssigner::LEVEL_BELOW ? level - Commander::HuntAssigner::LEVEL_BELOW : 1;
        rules.maxLevel = level + Commander::HuntAssigner::LEVEL_ABOVE;
        rules.nearYd = Commander::HuntAssigner::MAX_APPROACH_YD;
        if (m_questStep == QuestStep::Hunt)
        {
            QuestPlan const& q = m_request.quest;
            rules.preyEntry = q.preyEntry;
            rules.areaX = q.areaX;
            rules.areaY = q.areaY;
            rules.areaZ = q.areaZ;
            rules.areaRadius = std::max(q.areaRadius, QUEST_WANDER_MIN_YD);
        }
        else
        {
            rules.areaX = m_homeX;
            rules.areaY = m_homeY;
            rules.areaZ = m_homeZ;
            rules.areaRadius = FIELD_WANDER_YD;
        }
        return Life::Prowl::Update(me, m_prowl, rules);
    }

    void PopulationBotAI::SetupProfession()
    {
        // First Aid and Tailoring are the most common sights, then Leatherworking and Alchemy.
        uint32 const roll = urand(0, 99);
        m_professionSkill = roll < 30 ? SKILL_LINE_FIRST_AID
                          : roll < 60 ? SKILL_LINE_TAILORING
                          : roll < 80 ? SKILL_LINE_LEATHERWORKING
                          : SKILL_LINE_ALCHEMY;

        uint32 const value = CraftSkillForLevel(me->GetLevel());
        me->SetSkill(uint16(m_professionSkill), uint16(value), 300);
        for (Recipe const& r : g_recipes)
            if (r.skill == m_professionSkill && r.reqSkill <= value && !me->HasSpell(r.spell) && sSpellMgr.GetSpellEntry(r.spell))
                me->LearnSpell(r.spell, false);

        PickRecipe();
        m_batchLeft = urand(3, 10);
        m_craftRestUntilMs = WorldTimer::getMSTime() + urand(2, 8) * IN_MILLISECONDS;
    }

    // One of the three best recipes for the skill, like a player levelling the profession.
    void PopulationBotAI::PickRecipe()
    {
        uint32 const value = CraftSkillForLevel(me->GetLevel());
        std::vector<uint32> known;
        for (Recipe const& r : g_recipes)
            if (r.skill == m_professionSkill && r.reqSkill <= value && me->HasSpell(r.spell))
                known.push_back(r.spell);   // table is ordered by skill
        if (known.empty())
        {
            m_recipeSpell = 0;
            return;
        }
        size_t const first = known.size() > 3 ? known.size() - 3 : 0;
        m_recipeSpell = known[first + urand(0, uint32(known.size() - first) - 1)];
    }

    void PopulationBotAI::ClearCraftedItems()
    {
        for (Recipe const& r : g_recipes)
        {
            if (r.skill != m_professionSkill)
                continue;
            SpellEntry const* pSpell = sSpellMgr.GetSpellEntry(r.spell);
            if (!pSpell)
                continue;
            uint32 const item = uint32(pSpell->EffectItemType[EFFECT_INDEX_0]);
            if (item)
                if (uint32 const count = me->GetItemCount(item))
                    me->DestroyItemCount(item, count, true);
        }
    }

    void PopulationBotAI::UpdateCrafter()
    {
        if (me->IsNonMeleeSpellCasted(false))
            return;

        uint32 const now = WorldTimer::getMSTime();
        if (!m_recipeSpell || now < m_craftRestUntilMs)
        {
            UpdateIdle();
            return;
        }

        if (!m_batchLeft)
        {
            // Batch done: put things away, rest, maybe switch to another recipe.
            ClearCraftedItems();
            m_craftRestUntilMs = now + urand(20, 60) * IN_MILLISECONDS;
            m_nextIdleAtMs = now + urand(3, 10) * IN_MILLISECONDS;
            m_batchLeft = urand(3, 10);
            if (urand(0, 2) == 0)
                PickRecipe();
            return;
        }

        SpellEntry const* pSpell = sSpellMgr.GetSpellEntry(m_recipeSpell);
        if (!pSpell)
        {
            m_recipeSpell = 0;
            return;
        }

        for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
        {
            if (pSpell->Reagent[i] <= 0 || !pSpell->ReagentCount[i])
                continue;
            uint32 const item = uint32(pSpell->Reagent[i]);
            if (!me->HasItemCount(item, pSpell->ReagentCount[i]))
                AddItemToInventory(item, pSpell->ReagentCount[i] * 2);
        }

        if (me->IsSittingDown())
            me->SetStandState(UNIT_STAND_STATE_STAND);
        if (me->IsMoving())
            return;

        SpellCastResult const result = me->CastSpell(me, pSpell, false);
        if (result == SPELL_CAST_OK)
        {
            --m_batchLeft;
            m_craftFailures = 0;
            return;
        }

        // Bags full, missing reagent, ...: tidy up and try again after a rest; give up after 3 failures.
        ClearCraftedItems();
        m_batchLeft = 0;
        if (++m_craftFailures >= 3)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[PopBot] %s gives up crafting spell %u (cast result %u).", m_name.c_str(), m_recipeSpell, uint32(result));
            m_recipeSpell = 0;
        }
    }

    void PopulationBotAI::MoveTo(float x, float y, float z)
    {
        if (me->IsSittingDown())
            me->SetStandState(UNIT_STAND_STATE_STAND);
        m_targetX = x;
        m_targetY = y;
        m_targetZ = z;
        m_moveDeadlineMs = WorldTimer::getMSTime() + 120 * IN_MILLISECONDS;
        me->GetMotionMaster()->MovePoint(1, x, y, z, MOVE_PATHFINDING | MOVE_RUN_MODE | MOVE_EXCLUDE_STEEP_SLOPES);
    }

    bool PopulationBotAI::ReachedTarget() const
    {
        if (me->GetDistance2d(m_targetX, m_targetY, SizeFactor::None) < 1.5f)
            return true;
        // Point movement finished or was replaced (path not found, pushed away, ...).
        return !me->IsMoving() && me->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE;
    }

    void PopulationBotAI::FacePoint(float x, float y)
    {
        if (std::fabs(x - me->GetPositionX()) < 0.1f && std::fabs(y - me->GetPositionY()) < 0.1f)
            return;
        me->SetFacingTo(me->GetAngle(x, y));
    }

    void PopulationBotAI::Initialize()
    {
        // The bot logs in at level 1 and is levelled here. A client that already sees it plays
        // the level-up glow when its level field goes up, so nearby players stop seeing it until
        // it is ready (shown again, at its final level and gear, at the end).
        HideFromNearbyPlayers();

        if (m_request.level && m_request.level != me->GetLevel())
        {
            me->GiveLevel(m_request.level);
            me->InitTalentForLevel();
            me->SetUInt32Value(PLAYER_XP, 0);
        }

        // Premade specs exist for 19 / 29 / 39 / 49 / 60 (lower template if between).
        if (me->GetLevel() >= 19)
            LearnPremadeSpecForClass();
        else if (me->GetLevel() >= 10)
            LearnRandomTalents();

        if (m_role == ROLE_INVALID)
            AutoAssignRole();

        EquipForTier();
        me->UpdateSkillsToMaxSkillsForLevel();
        if (m_request.hasGearPlan)
            me->SetMoney(m_request.money);

        ResetSpellData();
        PopulateSpellData();
        if (m_request.kind == BotKind::Field || m_request.kind == BotKind::Traveller)
            InitializeField();

        me->SetHealthPercent(100.0f);
        me->SetPowerPercent(me->GetPowerType(), 100.0f);

        uint32 newzone, newarea;
        me->GetZoneAndAreaId(newzone, newarea);
        me->UpdateZone(newzone, newarea);

        if (!m_request.faceGuid.IsEmpty())
        {
            if (Unit* pFace = me->GetMap()->GetUnit(m_request.faceGuid))
                me->SetFacingToObject(pFace);
        }
        else if (m_request.hasFacePoint && !m_request.hasEntrance)
            FacePoint(m_request.faceX, m_request.faceY);

        // The spot, also when logged in at an entrance out of sight.
        m_homeX = m_request.hasEntrance ? m_request.x : me->GetPositionX();
        m_homeY = m_request.hasEntrance ? m_request.y : me->GetPositionY();
        m_homeZ = m_request.hasEntrance ? m_request.z : me->GetPositionZ();

        if (m_request.role == CityRole::Crafter)
            SetupProfession();

        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);
        // Re-sent whole: also the client workaround that makes every equipped item visible.
        me->SetVisibility(VISIBILITY_ON);

        m_nextIdleAtMs = WorldTimer::getMSTime() + urand(5000, 20000);
        m_initialized = true;

        if (m_request.hasEntrance)
        {
            m_phase = Phase::Entering;
            MoveTo(m_homeX, m_homeY, m_homeZ);
        }
    }

    void PopulationBotAI::UpdateEntering()
    {
        if (!ReachedTarget() && WorldTimer::getMSTime() < m_moveDeadlineMs)
            return;

        if (me->GetDistance2d(m_homeX, m_homeY, SizeFactor::None) <= 3.0f)
        {
            ArriveAtSpot();
            return;
        }

        // No path, pushed off, stuck: once more, then give up (still near the entrance, out of sight).
        if (m_walkRetries++ < 1)
        {
            MoveTo(m_homeX, m_homeY, m_homeZ);
            return;
        }
        sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[PopBot] %s could not walk in from (%.1f, %.1f, %.1f), logging out.",
            m_name.c_str(), m_request.entranceX, m_request.entranceY, m_request.entranceZ);
        LogOut();
    }

    void PopulationBotAI::ArriveAtSpot()
    {
        m_phase = Phase::Living;
        uint32 const now = WorldTimer::getMSTime();
        if (m_request.hasFacePoint)
            FacePoint(m_request.faceX, m_request.faceY);
        // (a town life starts on the next living tick, at this spot)
        m_nextIdleAtMs = now + urand(3, 9) * IN_MILLISECONDS;
    }

    void PopulationBotAI::BeginLeaving()
    {
        Life::Pastimes::EmoteReply::Stop(me, m_emoteReply);
        m_raising.Clear();     // (its claim runs out by itself)
        if (m_townLife.IsStarted())
            m_townLife.Stop(me);
        m_phase = Phase::Leaving;
        m_walkRetries = 0;
        if (me->IsNonMeleeSpellCasted(false))
            me->InterruptNonMeleeSpells(false);
        MoveTo(m_handle->leaveX, m_handle->leaveY, m_handle->leaveZ);
    }

    void PopulationBotAI::UpdateLeaving()
    {
        if (!ReachedTarget() && WorldTimer::getMSTime() < m_moveDeadlineMs)
            return;
        // Not there (a fight on the way ended the walk, pushed off): on again, a few times.
        if (me->GetDistance2d(m_handle->leaveX, m_handle->leaveY, SizeFactor::None) > 3.0f &&
            m_walkRetries++ < LEAVE_WALK_RETRIES && WorldTimer::getMSTime() < m_moveDeadlineMs)
        {
            MoveTo(m_handle->leaveX, m_handle->leaveY, m_handle->leaveZ);
            return;
        }
        // There, or no way there: log out (the manager also forces it after a while).
        LogOut();
    }

    void PopulationBotAI::LogOut()
    {
        m_phase = Phase::Gone;
        me->GetMotionMaster()->MoveIdle();
        if (botEntry)
            botEntry->requestRemoval = true;
    }

    void PopulationBotAI::EquipForTier()
    {
        if (m_request.hasGearPlan)
        {
            // Roster member: plain gear for the level, never above the owner's team.
            static_assert(sizeof(GearPlan::qualityWeights) / sizeof(uint32) == MAX_ITEM_QUALITY, "GearPlan quality count");
            GearPlan const& plan = m_request.gear;
            RandomGearOptions options;
            options.allowPvP = false;
            options.allowCosmetic = false;
            options.maxItemLevel = plan.maxItemLevel;
            for (uint32 q = 0; q < MAX_ITEM_QUALITY; ++q)
                options.qualityWeights[q] = plan.qualityWeights[q];
            options.skipSlotMask = plan.skipSlotMask;
            options.seed = plan.seed;
            EquipRandomGearInEmptySlots(options);
            UpdateVisualHonorRankBasedOnItems();
            return;
        }

        uint32 templateEntry = 0;
        if (me->GetLevel() >= 60)
        {
            char const* tag = m_request.tier == GearTier::Showcase ? "phase6" : "preraid";
            templateEntry = FindGearTemplate(me->GetClass(), m_role, tag);
        }

        if (templateEntry)
        {
            sObjectMgr.ApplyPremadeGearTemplateToPlayer(templateEntry, me);
            BotShared::Inventory::WearPlainShirt(me, urand(0, 2));
        }
        else
            EquipRandomGearInEmptySlots();

        UpdateVisualHonorRankBasedOnItems();
    }

    // Destroyed on nearby clients now (not at the next visibility update), so no field change
    // made while hidden reaches them; SetVisibility(VISIBILITY_ON) sends the bot again whole.
    void PopulationBotAI::HideFromNearbyPlayers()
    {
        me->SetVisibility(VISIBILITY_OFF);
        for (auto const& ref : me->GetMap()->GetPlayers())
        {
            Player* pPlayer = ref.getSource();
            if (pPlayer && pPlayer != me && !pPlayer->IsBot() && pPlayer->IsWithinDist(me, 200.0f))
                pPlayer->UpdateVisibilityOf(pPlayer, me);
        }
    }

    void PopulationBotAI::UpdateIdle()
    {
        if (me->IsInCombat() || me->IsMoving())
            return;

        uint32 const now = WorldTimer::getMSTime();
        if (now < m_nextIdleAtMs)
            return;
        m_nextIdleAtMs = now + urand(15000, 45000);

        uint32 const roll = urand(0, 99);
        if (me->IsSittingDown())
        {
            if (roll < 40)
                me->SetStandState(UNIT_STAND_STATE_STAND);
            return;
        }

        if (roll < 45)
            me->HandleEmoteCommand(g_idleEmotes[urand(0, uint32(sizeof(g_idleEmotes) / sizeof(g_idleEmotes[0])) - 1)]);
        else if (roll < 60)
            me->SetStandState(UNIT_STAND_STATE_SIT);
        else if (roll < 85)
            me->SetFacingTo(me->GetOrientation() + frand(-1.2f, 1.2f));
    }

    void PopulationBotAI::PlayForReview(ReviewKind kind, uint32 id, Player* viewer)
    {
        m_reviewUntilMs = WorldTimer::getMSTime() + REVIEW_HOLD_MS;
        if (m_townLife.IsStarted())
            m_townLife.Stop(me);

        if (!me->IsStopped())
            me->StopMoving();
        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();

        // The previous action off: a bot's state is not cleared by moving like a client's.
        me->HandleEmoteState(EMOTE_ONESHOT_NONE);
        if (me->GetStandState() != UNIT_STAND_STATE_STAND)
            me->SetStandState(UNIT_STAND_STATE_STAND);
        if (me->GetSheath() != SHEATH_STATE_UNARMED && kind != ReviewKind::Sheath)
            me->SetSheath(SHEATH_STATE_UNARMED);
        if (viewer && viewer != me)
            me->SetFacingToObject(viewer);

        switch (kind)
        {
            case ReviewKind::OneShot:
                me->HandleEmoteCommand(id);
                break;
            case ReviewKind::State:
                me->HandleEmoteState(id);
                break;
            case ReviewKind::Stand:
                if (id < MAX_UNIT_STAND_STATE)
                    me->SetStandState(uint8(id));
                break;
            case ReviewKind::Sheath:
                if (id < MAX_SHEATH_STATE)
                    me->SetSheath(SheathState(id));
                break;
            case ReviewKind::Text:
                BotShared::BotChat::TextEmote(me, id, viewer);
                break;
        }
    }

    void PopulationBotAI::EndReview()
    {
        m_reviewUntilMs = 0;
        me->HandleEmoteState(EMOTE_ONESHOT_NONE);
        if (me->GetStandState() != UNIT_STAND_STATE_STAND)
            me->SetStandState(UNIT_STAND_STATE_STAND);
        if (m_request.hasFacePoint)
            FacePoint(m_request.faceX, m_request.faceY);
    }

    void PopulationBotAI::UpdateSocial(uint32 diff)
    {
        if (m_declineInviteInMs)
        {
            if (m_declineInviteInMs <= diff)
            {
                m_declineInviteInMs = 0;
                DeclineInviteNow();
            }
            else
                m_declineInviteInMs -= diff;
        }

        if (m_cancelTradeInMs)
        {
            if (m_cancelTradeInMs <= diff)
            {
                m_cancelTradeInMs = 0;
                me->GetSession()->QueuePacket(std::make_unique<NullClientPacket>(CMSG_CANCEL_TRADE));
            }
            else
                m_cancelTradeInMs -= diff;
        }
    }

    void PopulationBotAI::DeclineInviteNow()
    {
        Group* pGroup = me->GetGroupInvite();
        if (!pGroup)
        {
            // He was asked in the talk window (no invite): he says why to the one who asked.
            Player* pAsker = m_declineTo.IsEmpty() ? nullptr : sObjectAccessor.FindPlayer(m_declineTo);
            if (pAsker && pAsker->IsInWorld() && !m_declineKey.empty())
            {
                std::string const line = DeclineLine(pAsker);
                if (pAsker->IsInMap(me) && pAsker->IsWithinDistInMap(me, StrangerTalk::MEET_YD))
                    BotShared::BotChat::Say(me, line);
                else
                    Whisper(pAsker, line);
            }
            m_declineKey.clear();
            m_declineTo.Clear();
            return;
        }

        Player* pInviter = sObjectAccessor.FindPlayer(pGroup->GetLeaderGuid());
        me->GetSession()->QueuePacket(std::make_unique<NullClientPacket>(CMSG_GROUP_DECLINE));

        // A real player invited him out of the blue: no line, he asks why (StrangerTalk).
        if (!m_invitedBy.IsEmpty() && pInviter && pInviter->GetObjectGuid() == m_invitedBy)
        {
            m_invitedBy.Clear();
            StrangerTalk::OnInvite(pInviter, me);
            return;
        }
        m_invitedBy.Clear();

        std::string key;
        std::swap(key, m_declineKey);
        if (key.empty() && urand(0, 99) < 30)
            key = "popbot.decline";
        std::string const line = key.empty() || !pInviter ? std::string() : DeclineLine(pInviter, key);
        if (pInviter && !pInviter->IsBot() && !line.empty())
            Whisper(pInviter, line);
    }

    std::string PopulationBotAI::DeclineLine(Player* reader, std::string const& key) const
    {
        std::string const& which = key.empty() ? m_declineKey : key;
        if (which == "meet.join.toohigh")
            return StrangerTalk::TooHighLine(reader, me, m_request.tags);
        return BotShared::Phrases::Say(BotShared::I18n::Of(reader), which, m_request.tags, {});
    }

    void PopulationBotAI::Whisper(Player* to, std::string const& line)
    {
        auto msg = std::make_unique<WorldPackets::Chat::ChatMessage>();
        msg->type = CHAT_MSG_WHISPER;
        msg->lang = me->GetTeam() == HORDE ? LANG_ORCISH : LANG_COMMON;
        msg->whisperTargetOrChannel = to->GetName();
        msg->message = line;
        me->GetSession()->QueuePacket(std::move(msg));
    }

    void PopulationBotAI::DeclineRecruit(DeclineReason reason)
    {
        switch (reason)
        {
            case DeclineReason::NoSlot:
                m_declineKey = "popbot.noslot";
                break;
            case DeclineReason::TooHigh:
                m_declineKey = "meet.join.toohigh";
                break;
            case DeclineReason::Farewell:
            default:
                m_declineKey = "popbot.farewell";
                break;
        }
        // (a moment to "think", as a person would)
        m_declineInviteInMs = urand(1500, 3000);
    }

    void PopulationBotAI::HoldForTalk(Player* player)
    {
        if (!me || m_request.kind == BotKind::Herald)
            return;
        m_talkHoldUntilMs = std::max<uint32>(WorldTimer::getMSTime() + TALK_HOLD_MS, 1);
        if (m_townLife.IsStarted())
            m_townLife.Stop(me);
        if (!me->IsStopped())
            me->StopMoving();
        me->GetMotionMaster()->Clear(false, true);
        me->GetMotionMaster()->MoveIdle();
        if (player && !me->IsSittingDown())
            me->SetFacingToObject(player);
    }

    bool PopulationBotAI::UpdateTalkHold()
    {
        if (WorldTimer::getMSTime() >= m_talkHoldUntilMs || me->IsInCombat() || m_handle->leave)
        {
            m_talkHoldUntilMs = 0;
            return false;
        }
        return true;
    }

    void PopulationBotAI::Join(Player* player, bool bonded)
    {
        if (!player || !CanJoin())
            return;
        m_handle->recruiter = player->GetObjectGuid();
        m_handle->bonded = bonded;
        m_declineTo = player->GetObjectGuid();
    }

    bool PopulationBotAI::IsHeraldFor(Player const* player) const
    {
        return player && m_request.kind == BotKind::Herald && m_request.seeks == player->GetObjectGuid() &&
            m_heraldStep != HeraldStep::Leaving && m_heraldStep != HeraldStep::Joined;
    }

    void PopulationBotAI::HeraldAnswer(Player* player, bool yes)
    {
        if (!IsHeraldFor(player))
            return;
        if (yes)
        {
            m_heraldStep = HeraldStep::Joined;
            Join(player, true);
            return;
        }
        HeraldLeave();
    }

    void PopulationBotAI::HeraldLeave()
    {
        m_heraldStep = HeraldStep::Leaving;
        // Back where he came from, out of everyone's sight (he logged in there).
        m_handle->leaveX = m_homeX;
        m_handle->leaveY = m_homeY;
        m_handle->leaveZ = m_homeZ;
        BeginLeaving();
    }

    void PopulationBotAI::UpdateHerald()
    {
        uint32 const now = WorldTimer::getMSTime();
        Player* pPlayer = sObjectAccessor.FindPlayer(m_request.seeks);
        if (m_heraldStep == HeraldStep::Joined)
            return;     // (the manager recruits him: he logs out and in again by the player)
        if (!pPlayer || !pPlayer->IsInWorld() || !pPlayer->IsInMap(me) || pPlayer->IsBeingTeleported())
        {
            HeraldLeave();
            return;
        }
        if (!m_heraldDeadlineMs)
            m_heraldDeadlineMs = now + HERALD_APPROACH_MS;
        if (me->IsInCombat())
            return;     // (his body fights back: TeamBotAI is not his, he waits it out)

        float const dist = me->GetDistance(pPlayer);
        switch (m_heraldStep)
        {
            case HeraldStep::Approach:
            {
                if (now >= m_heraldDeadlineMs)
                {
                    HeraldLeave();
                    return;
                }
                // Close, and the player is not fighting: he starts to talk.
                if (dist <= HERALD_TALK_YD && !pPlayer->IsInCombat() && pPlayer->IsAlive())
                {
                    if (!me->IsStopped())
                        me->StopMoving();
                    me->GetMotionMaster()->Clear(false, true);
                    me->GetMotionMaster()->MoveIdle();
                    me->SetFacingToObject(pPlayer);
                    m_heraldStep = HeraldStep::Speak;
                    m_heraldNextMs = now;
                    return;
                }
                // Runs to him (again when he moved on); keeps away from his fight.
                if (now >= m_heraldNextMs)
                {
                    m_heraldNextMs = now + 1500;
                    float const keep = pPlayer->IsInCombat() ? 12.0f : 2.0f;
                    if (dist > keep + 1.0f)
                    {
                        float x, y, z;
                        pPlayer->GetNearPoint(me, x, y, z, 0.0f, keep, pPlayer->GetAngle(me));
                        MoveTo(x, y, z);
                    }
                }
                return;
            }
            case HeraldStep::Speak:
            {
                // He walked off while being talked to: after him, then on with the words.
                if (dist > HERALD_TALK_YD * 3.0f)
                {
                    m_heraldStep = HeraldStep::Approach;
                    m_heraldNextMs = now;
                    return;
                }
                if (now < m_heraldNextMs)
                    return;
                me->SetFacingToObject(pPlayer);
                if (StrangerTalk::HeraldSpeaks(pPlayer, me, m_heraldLine++))
                {
                    m_heraldNextMs = now + HERALD_LINE_GAP_MS;
                    return;
                }
                // The words are said: the offer page went to the player.
                m_heraldStep = HeraldStep::Waiting;
                m_heraldDeadlineMs = now + HERALD_ANSWER_MS;
                return;
            }
            case HeraldStep::Waiting:
            {
                if (now >= m_heraldDeadlineMs)
                {
                    StrangerTalk::HeraldGivesUp(pPlayer, me);
                    HeraldLeave();
                    return;
                }
                // Stays by him while he thinks it over.
                if (dist > 8.0f && now >= m_heraldNextMs)
                {
                    m_heraldNextMs = now + 1500;
                    float x, y, z;
                    pPlayer->GetNearPoint(me, x, y, z, 0.0f, 2.5f, pPlayer->GetAngle(me));
                    MoveTo(x, y, z);
                }
                else if (dist <= 8.0f && !me->IsMoving())
                    me->SetFacingToObject(pPlayer);
                return;
            }
            default:
                return;
        }
    }

    void PopulationBotAI::OnPacketReceived(WorldPacket const* packet)
    {
        switch (packet->GetOpcode())
        {
            case SMSG_GROUP_INVITE:
            {
                if (!me || m_declineInviteInMs)
                    return;
                // Nobody joins a stranger who just invites him (recruiting doc §2.1): he declines
                // the invite and asks the player why (StrangerTalk); joining goes through that
                // talk. The player his first companion came for: that is his yes.
                if (CanJoin() && m_handle->recruiter.IsEmpty() && sCompanionMgr.IsEnabled())
                {
                    Group* pGroup = me->GetGroupInvite();
                    Player* pInviter = pGroup ? sObjectAccessor.FindPlayer(pGroup->GetLeaderGuid()) : nullptr;
                    if (pInviter && !pInviter->IsBot())
                    {
                        if (IsHeraldFor(pInviter))
                        {
                            StrangerTalk::HeraldAccepted(pInviter, me);
                            HeraldAnswer(pInviter, true);
                            m_declineInviteInMs = 15000;    // (the decline only if the recruit did not happen)
                            return;
                        }
                        m_invitedBy = pInviter->GetObjectGuid();
                        m_declineInviteInMs = urand(1000, 2000);
                        return;
                    }
                }
                m_declineInviteInMs = urand(2000, 8000);
                return;
            }
            case SMSG_TRADE_STATUS:
            {
                // Do not let CombatBotBaseAI open and accept the trade.
                if (!me)
                    return;
                uint32 const status = *((uint32*)(*packet).contents());
                if ((status == TRADE_STATUS_BEGIN_TRADE || status == TRADE_STATUS_OPEN_WINDOW) && !m_cancelTradeInMs)
                    m_cancelTradeInMs = urand(1000, 3000);
                return;
            }
            case SMSG_DUEL_REQUESTED:
            {
                if (!me)
                    return;
                auto data = std::make_unique<WorldPackets::Duel::DuelCancelled>();
                data->playerGuid = me->GetObjectGuid();
                me->GetSession()->QueuePacket(std::move(data));
                return;
            }
        }

        CombatBotBaseAI::OnPacketReceived(packet);
    }
}
