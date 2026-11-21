#include "Population/PopulationBotAI.h"
#include "Population/PopulationMgr.h"
#include "Population/StrangerTalk.h"
#include "Companions/CompanionMgr.h"
#include "Commander/CommanderMgr.h"
#include "Commander/PartyCommander.h"
#include "Commander/Team.h"
#include "Shared/ClassTraining.h"
#include "Shared/Inventory.h"
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
#include "Utilities/Random.h"
#include "MotionMaster.h"
#include "GridMap.h"
#include "SpellMgr.h"
#include "Log.h"
#include <algorithm>
#include <vector>
#include <cmath>

namespace Population
{
    namespace
    {
        char const* const g_declineLines[] = {
            "no thanks",
            "no ty",
            "sorry, busy atm",
            "nah im good, thx",
            "soloing this one, ty",
            "maybe later",
        };

        // Why a roster member does not join the player as a companion (DeclineRecruit).
        char const* const g_noSlotLines[] = {
            "你身边已经够热闹了，再多我一个怕是照顾不过来。",
            "你的队伍人手够多了，等你那边宽松些再来找我吧。",
            "你带的人已经不少了，下回吧。",
        };
        char const* const g_farewellLines[] = {
            "我们已经道过别了。各自保重吧。",
            "当初说好了不再同行的。祝你一路顺利。",
        };

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

        if (m_request.kind == BotKind::Field)
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

        if (LivesTownLife())
        {
            uint32 const now = WorldTimer::getMSTime();
            if (!m_townLife.IsStarted())
                m_townLife.Begin(me, m_request, now);
            m_townLife.Update(me, now);
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
        return m_request.role == CityRole::Stand || m_request.role == CityRole::Mover || m_request.role == CityRole::Sitter;
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
        if (m_wasInCombat && !inCombat)
            ++m_fights;
        m_wasInCombat = inCombat;
        if (!inCombat && !m_handle->done)
        {
            std::shared_ptr<Commander::PartyCommander> commander = sCommanderMgr.FindCommander(me);
            uint32 const idleSince = commander ? commander->GetPlan().hunt.idleSinceMs : 0;
            if (m_fights >= m_fightsToLeave ||
                (idleSince && WorldTimer::getMSTimeDiff(idleSince, WorldTimer::getMSTime()) >= FIELD_IDLE_LEAVE_MS))
                m_handle->done = true;
        }

        bool const commanderOutOfCombat = !inCombat && sCommanderMgr.WantsOutOfCombatControl(this);

        // Eating and drinking are the Commander's (RestBehavior), as for party bots.
        EndRestWhenFull();
        bool const needsRest = !inCombat && NeedsRest();
        bool const resting = !inCombat && IsResting();
        if (me->GetStandState() != UNIT_STAND_STATE_STAND && !resting)
            me->SetStandState(UNIT_STAND_STATE_STAND);

        if (!inCombat && !me->IsMounted() && !resting)
        {
            UpdateOutOfCombatAI();      // buffs, pet
            if (m_isBuffing || me->IsNonMeleeSpellCasted())
                return;
        }

        // After a fight: its loot, as a player (not with a new victim chosen already).
        if (!inCombat && !resting && !me->GetVictim() && Life::LootRun::Update(me, m_lootRun, nullptr))
            return;
        if (inCombat)
            Life::LootRun::Stop(me, m_lootRun);

        // The Commander picks the next fight (HuntAssigner) and fights it.
        if (RunCommanderHook(commanderOutOfCombat, needsRest, resting))
            return;

        StopTakenOverMovement();
        FightVictim(me->GetVictim());
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
        if (m_request.kind == BotKind::Field)
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
            if (pAsker && pAsker->IsInWorld() && !m_declineLine.empty())
            {
                if (pAsker->IsInMap(me) && pAsker->IsWithinDistInMap(me, StrangerTalk::MEET_YD))
                    BotShared::BotChat::Say(me, m_declineLine);
                else
                    Whisper(pAsker, m_declineLine);
            }
            m_declineLine.clear();
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

        std::string line;
        std::swap(line, m_declineLine);
        if (line.empty() && urand(0, 99) < 30)
            line = g_declineLines[urand(0, uint32(sizeof(g_declineLines) / sizeof(g_declineLines[0])) - 1)];
        if (pInviter && !pInviter->IsBot() && !line.empty())
            Whisper(pInviter, line);
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
                m_declineLine = g_noSlotLines[urand(0, uint32(sizeof(g_noSlotLines) / sizeof(g_noSlotLines[0])) - 1)];
                break;
            case DeclineReason::TooHigh:
                m_declineLine = StrangerTalk::TooHighLine(me, m_request.tags);
                break;
            case DeclineReason::Farewell:
            default:
                m_declineLine = g_farewellLines[urand(0, uint32(sizeof(g_farewellLines) / sizeof(g_farewellLines[0])) - 1)];
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
