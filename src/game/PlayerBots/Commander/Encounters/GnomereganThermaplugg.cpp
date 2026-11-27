/*
 * Thermaplugg boss module (header comment: match, phases, plan, proposals).
 *
 * Value estimates (points, ValueModel.h: 1 point = 1% of party max health):
 *   Explosion      = Value::Points(BOMB_VICTIMS x BOMB_DAMAGE).
 *   Press a button = BOMBS_PER_FACE_WINDOW x Explosion, against Value::LostAction for
 *                    (presser -> button + button -> boss) / RUN_YARDS_PER_SEC + USE_SEC.
 *                    Value::LostAction already turns time away from the boss into points.
 *   Bomb intercept = Explosion (InterceptDuty::points, TargetBehavior's switch benefit).
 *
 * Known limits:
 *   - Bombs that are not in combat yet (falling, following the boss) are not in
 *     world.enemies (WorldModelBuilder takes creatures with us on their threat list), so
 *     nobody intercepts them until they pick a target (scenario 8, 3.4 item 2). They usually
 *     do that 14 yd from someone, about 2 s before they explode.
 *   - A pet's melee on a bomb is not stopped (STAMP_NO_MELEE judges the bots only).
 *   - No kiting, no dodging the blast, no crowd control of bombs, no pre-positioning of the
 *     tank before Knock Away, no top-up before the threat wipe (scenario 8, 3.2).
 *   - One presser at a time, sticky while alive, on this map, a damage dealer bot and at
 *     >= PRESSER_STAY_HEALTH_PCT; otherwise another one at >= PRESSER_MIN_HEALTH_PCT is picked
 *     (in every phase). The 10% gap is the hysteresis against two candidates near one
 *     threshold swapping the job back and forth.
 *   - A press is recorded when the presser is in reach of the button on his proposal tick; if
 *     the Arbiter picks another proposal that tick, ReadFaces notices after PRESS_VERIFY_MS
 *     (face still lit, presser not at the button) and frees the button again. BUTTON_REUSE_MS
 *     stays the upper bound (presser at the button but the face does not go dark).
 *   - The door lock at pull (people left outside) is not checked.
 *   - An intercept from FocusAssigner on the presser (Urgent) beats the button run (Assigned).
 *   - Faces / buttons are found by a grid search around the boss and cached; a search that
 *     missed some is repeated every OBJECT_SEARCH_RETRY_MS (reset with the encounter). If the
 *     boss is not loaded (bot far away) no search runs and the next refresh tries again.
 */

#include "Commander/Encounters/GnomereganThermaplugg.h"
#include "Commander/Encounters/EncounterHelpers.h"
#include "Commander/Behaviors/Combat/ThreatBehavior.h"
#include "Commander/Coordination/Duties/FocusAssigner.h"
#include "Commander/Coordination/TeamPlan.h"
#include "Commander/Perception/Spatial/PackResolver.h"
#include "Commander/Perception/Values/ValueModel.h"
#include "Commander/CommanderMgr.h"
#include "TeamBotAI.h"
#include "Creature.h"
#include "GameObject.h"
#include "GridSearchers.h"
#include "Map.h"
#include "Player.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <list>

namespace Commander
{
    namespace
    {
        char const* const SOURCE = "thermaplugg";

        // Scenario 8, 1.4 (gnomeregan.h GO_GNOME_FACE_x / GO_BUTTON_x). Index = script face index.
        uint32 const FACE_ENTRIES[GnomereganThermaplugg::FACE_COUNT] =
            { 142211, 142210, 142209, 142208, 142213, 142212 };
        uint32 const BUTTON_ENTRIES[GnomereganThermaplugg::FACE_COUNT] =
            { 142214, 142215, 142216, 142217, 142218, 142219 };

        // Which face is spawning bombs.
        // instance_gnomeregan::DoActivateBombFace / DoDeactivateBombFace toggle the face through
        // ScriptedInstance::DoUseDoorOrButton -> GameObject::UseDoorOrButton / ResetDoorOrButton
        // -> SwitchDoorOrButton, which flips GO_STATE_READY <-> GO_STATE_ACTIVE. The faces are
        // GAMEOBJECT_TYPE_BUTTON (type 1) spawned with state 1 = GO_STATE_READY (world DB
        // `gameobject` rows 6388, 11994, 9265, 9593, 28327, 6755) and autoCloseTime 0
        // (gameobject_template data2 = 0), so they never flip back by themselves: GO_STATE_ACTIVE
        // = lit (m_bActivated true in the script), GO_STATE_READY = dark. Scenario 8, 3.6 still
        // lists checking this in game.
        bool FaceIsLit(GameObject const* face)
        {
            return face && face->GetGoState() == GO_STATE_ACTIVE;
        }

        float Distance2d(Position3 const& a, Position3 const& b)
        {
            float const dx = a.x - b.x;
            float const dy = a.y - b.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        Position3 PositionOf(WorldObject const* o)
        {
            Position3 p;
            p.x = o->GetPositionX();
            p.y = o->GetPositionY();
            p.z = o->GetPositionZ();
            return p;
        }

        bool IsDamageDealerBot(PartyMemberFacts const& m, uint8 role)
        {
            return m.isBot && !m.isPet && m.CanAct() && m.role == role;
        }

        // The current presser keeps the job. Being controlled (stun, fear) is short, he keeps
        // it then; ProposePresser waits until he can act again. Lower health bar than a new
        // pick (PRESSER_STAY_HEALTH_PCT < PRESSER_MIN_HEALTH_PCT, hysteresis).
        bool CanStayPresser(PartyMemberFacts const& m)
        {
            return m.isBot && !m.isPet && m.alive &&
                (m.role == ROLE_MELEE_DPS || m.role == ROLE_RANGE_DPS) &&
                m.healthPct >= GnomereganThermaplugg::PRESSER_STAY_HEALTH_PCT;
        }
    }

    bool GnomereganThermaplugg::Matches(WorldModel const& world, uint32 mapId) const
    {
        if (mapId != MAP_ID)
            return false;
        for (EnemyFacts const& e : world.enemies)
            if (e.entry == NPC_THERMAPLUGG || e.entry == NPC_WALKING_BOMB)
                return true;
        return false;
    }

    void GnomereganThermaplugg::OnReset()
    {
        *this = GnomereganThermaplugg();
    }

    float GnomereganThermaplugg::ExplosionLoss(WorldModel const& world) const
    {
        return Value::Points(world, BOMB_VICTIMS * BOMB_DAMAGE);
    }

    void GnomereganThermaplugg::FindObjects(TeamBotAI* bot, uint32 nowMs)
    {
        if (m_objectsFound || !bot || !bot->me->IsInWorld())
            return;
        uint32 const retryMs = OBJECT_SEARCH_RETRY_MS;
        if (m_searched && WorldTimer::getMSTimeDiff(m_lastSearchMs, nowMs) < retryMs)
            return;     // last search missed some objects: back off

        // Search around the boss (faces and buttons form a circle around his spawn point).
        // Boss not loaded: only a map lookup, try again next refresh.
        Creature* boss = m_bossAlive ? bot->me->GetMap()->GetCreature(m_boss) : nullptr;
        if (!boss)
            return;
        m_searched = true;
        m_lastSearchMs = nowMs;

        std::vector<uint32> entries(FACE_ENTRIES, FACE_ENTRIES + FACE_COUNT);
        entries.insert(entries.end(), BUTTON_ENTRIES, BUTTON_ENTRIES + FACE_COUNT);
        std::list<GameObject*> found;
        GetGameObjectListWithEntryInGrid(found, boss, entries, OBJECT_SEARCH_RADIUS);

        for (GameObject* go : found)
        {
            for (uint32 i = 0; i < FACE_COUNT; ++i)
            {
                if (go->GetEntry() == FACE_ENTRIES[i])
                    m_faces[i].face = go->GetObjectGuid();
                else if (go->GetEntry() == BUTTON_ENTRIES[i])
                {
                    m_faces[i].button = go->GetObjectGuid();
                    m_faces[i].buttonPos = PositionOf(go);
                }
            }
        }

        m_objectsFound = true;
        for (Face const& f : m_faces)
            if (f.face.IsEmpty() || f.button.IsEmpty())
                m_objectsFound = false;
    }

    bool GnomereganThermaplugg::PresserAtButton(TeamBotAI* bot, Face const& f) const
    {
        if (!bot || m_presser.IsEmpty() || f.button.IsEmpty())
            return false;
        Map* map = bot->me->GetMap();
        Player* presser = map->GetPlayer(m_presser);
        GameObject* button = map->GetGameObject(f.button);
        return presser && button && presser->IsWithinDist(button, INTERACTION_DISTANCE);
    }

    void GnomereganThermaplugg::ReadFaces(TeamBotAI* bot)
    {
        // Same clock as Face::pressedAtMs (ProposePresser).
        uint32 const nowMs = WorldTimer::getMSTime();
        uint32 const verifyMs = PRESS_VERIFY_MS;
        m_litCount = 0;
        for (Face& f : m_faces)
        {
            GameObject const* go = (bot && !f.face.IsEmpty()) ? bot->me->GetMap()->GetGameObject(f.face) : nullptr;
            f.lit = m_bossAlive && FaceIsLit(go);   // all faces switch off when the boss dies
            if (!f.lit)
            {
                f.pressed = false;                  // dark again: its button may be used next time it lights
                continue;
            }
            ++m_litCount;
            // Recorded press that did not happen: the face should have gone dark by now, and
            // the presser is not standing at the button (the Arbiter ran another proposal on
            // the press tick, or he walked off). Free the button. If he is still at it, keep
            // the mark: the use may be under way, BUTTON_REUSE_MS bounds the wait.
            if (f.pressed && WorldTimer::getMSTimeDiff(f.pressedAtMs, nowMs) >= verifyMs && !PresserAtButton(bot, f))
                f.pressed = false;
        }
    }

    void GnomereganThermaplugg::PickPresser(WorldModel const& world)
    {
        // Sticky: keep the presser while he can still do the job. world.party only holds members
        // on this map, so a presser who died, left or dropped too low is replaced here.
        if (PartyMemberFacts const* current = world.FindMember(m_presser))
            if (CanStayPresser(*current))
                return;
        m_presser = ObjectGuid();
        m_presserNoted = false;

        // Melee first: he stands in the blast anyway and cannot shoot far bombs. Closest to
        // the boss, healthy enough to leave the healer's side.
        for (uint8 role : { uint8(ROLE_MELEE_DPS), uint8(ROLE_RANGE_DPS) })
        {
            PartyMemberFacts const* best = nullptr;
            float bestDistance = 0.0f;
            for (PartyMemberFacts const& m : world.party)
            {
                if (!IsDamageDealerBot(m, role) || m.healthPct < PRESSER_MIN_HEALTH_PCT)
                    continue;
                float const d = PackResolver::Distance(m.pos, m_bossPos);
                if (!best || d < bestDistance)
                {
                    best = &m;
                    bestDistance = d;
                }
            }
            if (best)
            {
                m_presser = best->guid;
                return;
            }
        }
    }

    void GnomereganThermaplugg::Refresh(TeamBotAI* bot, WorldModel const& world)
    {
        uint32 const now = world.refreshedAtMs;

        EnemyFacts const* boss = EncounterHelpers::FindEnemyByEntry(world, NPC_THERMAPLUGG);
        bool const wasAlive = m_bossAlive;
        m_bossAlive = boss != nullptr;
        if (boss)
        {
            m_boss = boss->guid;
            m_bossPct = boss->healthPct;
            m_bossPos = boss->pos;
        }

        m_bombCount = 0;
        for (EnemyFacts const& e : world.enemies)
            if (e.entry == NPC_WALKING_BOMB)
                ++m_bombCount;

        Phase const previous = m_phase;
        if (!m_bossAlive)
            m_phase = Phase::BossDead;
        else if (m_bossPct < PHASE_TWO_PCT)
            m_phase = Phase::PhaseTwo;
        else
            m_phase = Phase::PhaseOne;

        if (m_phase == Phase::PhaseTwo && previous != Phase::PhaseTwo)
        {
            Note("Thermaplugg below 50%: area Knock Away every 12 s, faces light up faster.");
            Say(EncounterHelpers::Line("voice.boss.thermaplugg.phase2", VoiceTier::Urgent));
        }
        if (m_phase == Phase::BossDead && wasAlive && m_bombCount)
        {
            char buf[160];
            snprintf(buf, sizeof(buf), "Thermaplugg dead, %u walking bomb(s) left: ranged kill them, melee stay off.", m_bombCount);
            Note(buf);
            Say(EncounterHelpers::Line("voice.boss.thermaplugg.bombs", VoiceTier::Urgent));
        }

        FindObjects(bot, now);
        ReadFaces(bot);

        if (m_bossAlive)
        {
            PickPresser(world);
            if (!m_presser.IsEmpty() && !m_presserNoted && bot)
            {
                m_presserNoted = true;
                char buf[160];
                Player* presser = bot->me->GetMap()->GetPlayer(m_presser);
                snprintf(buf, sizeof(buf), "Thermaplugg: %s presses the buttons of lit gnome faces.",
                    presser ? presser->GetName() : "a damage dealer");
                Note(buf);
                Say(EncounterHelpers::Line("voice.boss.thermaplugg.presser", VoiceTier::Essential, m_presser));
            }
        }
        else
            m_presser = ObjectGuid();
    }

    void GnomereganThermaplugg::OfferDuties(WorldModel const& world, TeamPlan const& /*plan*/,
                                            std::vector<DutyNeed>& needs) const
    {
        float const points = ExplosionLoss(world);
        auto addKey = [](std::vector<DutyKey>& keys, DutyKey const& key)
        {
            if (std::find(keys.begin(), keys.end(), key) == keys.end())
                keys.push_back(key);
        };

        for (EnemyFacts const& e : world.enemies)
        {
            // Crowd-controlled bombs count too: whether a stunned bomb still explodes is not
            // known (scenario 8, 1.3), it has to die either way.
            if (e.entry != NPC_WALKING_BOMB || !e.acting || !e.health)
                continue;

            // FocusAssigner may intercept the same bomb (a self-destruct): one bot kills it,
            // whichever of the two needs is answered first.
            DutyKey const handled{ DutyKeyKind::EnemyHandled, e.guid };
            for (DutyNeed& n : needs)
            {
                if (n.kind != DutyKind::Intercept || n.target != e.guid)
                    continue;
                addKey(n.blockedBy, handled);
                addKey(n.marks, handled);
            }

            DutyNeed need;
            need.kind = DutyKind::Intercept;
            need.target = e.guid;
            need.now = true;
            need.points = points;
            need.reason = "thermaplugg: walking bomb";
            need.blockedBy.push_back(handled);
            need.marks.push_back(handled);
            // Equal points: closest to its victim first (same order as FocusAssigner).
            if (PartyMemberFacts const* victim = world.FindMember(e.victim))
                need.tieMs = uint32(PackResolver::Distance(e.pos, victim->pos) * 1000.0f);

            // Ranged first, then melee close to the bomb, the closest first; never the presser.
            for (PartyMemberFacts const& m : world.party)
            {
                bool const ranged = IsDamageDealerBot(m, ROLE_RANGE_DPS);
                if ((!ranged && !IsDamageDealerBot(m, ROLE_MELEE_DPS)) || m.guid == m_presser)
                    continue;
                float const range = ranged ? FocusAssigner::INTERCEPT_RANGE : FocusAssigner::MELEE_INTERCEPT_RANGE;
                float const d = PackResolver::Distance(m.pos, e.pos);
                if (d > range)
                    continue;
                DutyCandidate c;
                c.bot = m.guid;
                c.rank = (ranged ? RANGED_FIRST_RANK : 0.0f) - d;
                need.candidates.push_back(c);
            }
            if (!need.candidates.empty())
                needs.push_back(std::move(need));
        }
    }

    void GnomereganThermaplugg::Propose(TeamBotAI const* bot, WorldModel const& world,
                                        TeamPlan const& plan, std::vector<Proposal>& out) const
    {
        if (!m_presser.IsEmpty() && bot->me->GetObjectGuid() == m_presser)
            ProposePresser(bot, world, out);
    }

    void GnomereganThermaplugg::ProposePresser(TeamBotAI const* bot, WorldModel const& world,
                                               std::vector<Proposal>& out) const
    {
        if (!m_bossAlive || !m_litCount)
            return;
        Player* me = bot->me;
        PartyMemberFacts const* myFacts = world.FindMember(me->GetObjectGuid());
        // Same bar as keeping the job (CanStayPresser): a presser between the stay and the pick
        // threshold keeps pressing.
        if (!myFacts || !myFacts->CanAct() || myFacts->healthPct < PRESSER_STAY_HEALTH_PCT)
            return;

        Map* map = me->GetMap();
        uint32 const nowMs = WorldTimer::getMSTime();
        uint32 const reuseMs = BUTTON_REUSE_MS;
        Position3 const myPos = PositionOf(me);
        Face const* target = nullptr;
        float targetDistance = 0.0f;
        for (Face const& f : m_faces)
        {
            if (!f.lit || f.button.IsEmpty())
                continue;
            if (f.pressed && WorldTimer::getMSTimeDiff(f.pressedAtMs, nowMs) < reuseMs)
                continue;   // just used and still lit: give the face time to switch off
            // Live state: the button switches its face off at once, f.lit is only re-read on
            // the next Refresh and the presser ticks in between.
            if (!FaceIsLit(map->GetGameObject(f.face)))
                continue;
            float const d = Distance2d(myPos, f.buttonPos);
            if (!target || d < targetDistance)
            {
                target = &f;
                targetDistance = d;
            }
        }
        if (!target)
            return;

        // In reach and free (same checks as Arbiter::ExecuteUseObject): this tick's proposal
        // uses the button, so record the press.
        GameObject* button = map->GetGameObject(target->button);
        if (button && me->IsWithinDist(button, INTERACTION_DISTANCE) && !me->IsNonMeleeSpellCasted(false, false, true))
        {
            target->pressed = true;
            target->pressedAtMs = nowMs;
        }

        float const awaySec = (targetDistance + Distance2d(target->buttonPos, m_bossPos)) / RUN_YARDS_PER_SEC + USE_SEC;
        Proposal p = EncounterHelpers::MakeReplace(SOURCE, ProposalTier::Assigned, BotActionType::UseObject, target->button);
        // holdMs left 0: the arbiter's UseObject hold (Arbiter::MOVEMENT_HOLD_MS) applies.
        p.reason = "gnome face lit, press its button";
        p.SetValue(BOMBS_PER_FACE_WINDOW * ExplosionLoss(world), Value::LostAction(world, *myFacts, awaySec));
        out.push_back(p);
    }

    void GnomereganThermaplugg::ShapeDoctrine(WorldModel const& world, TeamPlan& plan)
    {
        // A bomb blows up next to whoever hits it in melee, and the blast takes the whole melee
        // pile: ranged kill them (their intercepts: OfferDuties). A melee bot on one drops it
        // and gets another target (TeamBotAI::StopHandsOffVictims, TargetAssigner).
        for (EnemyFacts const& e : world.enemies)
            if (e.entry == NPC_WALKING_BOMB && e.health)
                plan.doctrine.Enemy(e.guid).stamps |= STAMP_NO_MELEE;
    }
}
