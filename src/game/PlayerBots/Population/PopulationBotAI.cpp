#include "Population/PopulationBotAI.h"
#include "Population/PopulationMgr.h"
#include "PlayerBotMgr.h"
#include "Player.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Utilities/Random.h"

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

        uint32 const g_idleEmotes[] = {
            EMOTE_ONESHOT_TALK, EMOTE_ONESHOT_TALK, EMOTE_ONESHOT_TALK,
            EMOTE_ONESHOT_LAUGH, EMOTE_ONESHOT_POINT, EMOTE_ONESHOT_QUESTION,
            EMOTE_ONESHOT_EXCLAMATION, EMOTE_ONESHOT_BOW, EMOTE_ONESHOT_CHEER, EMOTE_ONESHOT_WAVE,
        };

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
        : CombatBotBaseAI(), m_request(request), m_name(std::move(name)), m_handle(std::move(handle))
    {
        m_handle->ai = this;
        m_updateTimer.Reset(1000);
    }

    PopulationBotAI::~PopulationBotAI()
    {
        m_handle->alive = false;
        m_handle->ai = nullptr;
    }

    bool PopulationBotAI::OnSessionLoaded(PlayerBotEntry* /*entry*/, WorldSession* sess)
    {
        return SpawnNewPlayer(sess, m_request.classId, m_request.race, m_request.mapId, 0,
            m_request.x, m_request.y, m_request.z, m_request.o, nullptr, m_name.c_str());
    }

    void PopulationBotAI::OnPlayerLogin()
    {
        if (!m_initialized)
            me->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);
    }

    void PopulationBotAI::UpdateAI(uint32 const diff)
    {
        UpdateSocial(diff);

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

        UpdateIdle();
    }

    void PopulationBotAI::Initialize()
    {
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

        ResetSpellData();
        PopulateSpellData();

        me->SetHealthPercent(100.0f);
        me->SetPowerPercent(me->GetPowerType(), 100.0f);

        uint32 newzone, newarea;
        me->GetZoneAndAreaId(newzone, newarea);
        me->UpdateZone(newzone, newarea);

        if (!m_request.faceGuid.IsEmpty())
            if (Unit* pFace = me->GetMap()->GetUnit(m_request.faceGuid))
                me->SetFacingToObject(pFace);

        me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);
        RefreshAppearanceForNearbyPlayers();

        m_nextIdleAtMs = WorldTimer::getMSTime() + urand(5000, 20000);
        m_initialized = true;
    }

    void PopulationBotAI::EquipForTier()
    {
        uint32 templateEntry = 0;
        if (me->GetLevel() >= 60)
        {
            char const* tag = m_request.tier == GearTier::Showcase ? "phase6" : "preraid";
            templateEntry = FindGearTemplate(me->GetClass(), m_role, tag);
        }

        if (templateEntry)
            sObjectMgr.ApplyPremadeGearTemplateToPlayer(templateEntry, me);
        else
            EquipRandomGearInEmptySlots();

        UpdateVisualHonorRankBasedOnItems();
    }

    // Same client workaround as PartyBotAI: re-send the bot so every equipped item is visible.
    void PopulationBotAI::RefreshAppearanceForNearbyPlayers()
    {
        me->SetVisibility(VISIBILITY_OFF);
        for (auto const& ref : me->GetMap()->GetPlayers())
        {
            Player* pPlayer = ref.getSource();
            if (pPlayer && pPlayer != me && !pPlayer->IsBot() && pPlayer->IsWithinDist(me, 200.0f))
                pPlayer->UpdateVisibilityOf(pPlayer, me);
        }
        me->SetVisibility(VISIBILITY_ON);
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
            return;

        Player* pInviter = sObjectAccessor.FindPlayer(pGroup->GetLeaderGuid());
        me->GetSession()->QueuePacket(std::make_unique<NullClientPacket>(CMSG_GROUP_DECLINE));

        if (pInviter && !pInviter->IsBot() && urand(0, 99) < 30)
        {
            auto msg = std::make_unique<WorldPackets::Chat::ChatMessage>();
            msg->type = CHAT_MSG_WHISPER;
            msg->lang = me->GetTeam() == HORDE ? LANG_ORCISH : LANG_COMMON;
            msg->whisperTargetOrChannel = pInviter->GetName();
            msg->message = g_declineLines[urand(0, uint32(sizeof(g_declineLines) / sizeof(g_declineLines[0])) - 1)];
            me->GetSession()->QueuePacket(std::move(msg));
        }
    }

    void PopulationBotAI::OnPacketReceived(WorldPacket const* packet)
    {
        switch (packet->GetOpcode())
        {
            case SMSG_GROUP_INVITE:
            {
                if (me && !m_declineInviteInMs)
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
