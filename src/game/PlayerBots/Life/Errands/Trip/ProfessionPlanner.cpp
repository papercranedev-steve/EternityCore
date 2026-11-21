#include "Life/Errands/Trip/ProfessionPlanner.h"
#include "Life/Errands/TownTrip.h"
#include "Life/Errands/ErrandCommon.h"
#include "Shared/Phrases.h"
#include "Shared/Capitals.h"
#include "Shared/ClassTraining.h"
#include "Shared/NpcDirectory.h"
#include "Shared/Professions.h"
#include "Player.h"
#include "Creature.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "WorldSession.h"
#include "DBCStores.h"
#include "Log.h"
#include <map>

namespace Life
{
namespace Trip
{
    namespace
    {
        namespace P = BotShared::Professions;
        using BotShared::NpcDirectory::ServiceNpc;

        // The money the class trainer wants for the important lessons (as VendorTask keeps).
        uint32 ClassReserve(Player* me, Lessons::Context const* lessons)
        {
            if (!lessons)
                return BotShared::ClassTraining::CostOfAvailable(me);
            return Lessons::CostUpTo(Lessons::Available(me, *lessons), Lessons::IMPORTANT);
        }

        uint32 Spendable(Player* me, Lessons::Context const* lessons)
        {
            uint32 const reserve = ClassReserve(me, lessons);
            return me->GetMoney() > reserve ? me->GetMoney() - reserve : 0;
        }

        class ProfessionAction final : public IStopAction
        {
        public:
            explicit ProfessionAction(Lessons::Context const* lessons) : m_lessons(lessons) {}
            char const* Name() const override { return "profession trainer"; }
            uint32 Do(Player* me, WorldObject* target) override
            {
                Creature* npc = target->ToCreature();
                if (!npc)
                    return 0;
                P::Paid const paid = P::LearnAt(me, npc, ClassReserve(me, m_lessons));
                sLog.Out(LOG_BASIC, LOG_LVL_DETAIL, "[Life] %s at profession trainer %s: %u lessons for %u copper.",
                    me->GetName(), npc->GetName(), paid.learned, paid.cost);
                return paid.learned;
            }

        private:
            Lessons::Context const* m_lessons;
        };

        // Trainers that may do: spawns, live and friendly where the grid is loaded, and in a
        // capital anywhere in the city (farSpawn), else near the leader with a safe walk.
        struct Candidate
        {
            ServiceNpc npc;
            bool farSpawn;
        };

        std::vector<Candidate> Candidates(Player* me, Player const* leader)
        {
            std::vector<Candidate> out;
            Map* map = me->GetMap();
            BotShared::Capitals::Capital const* capital = BotShared::Capitals::OfZone(leader->GetZoneId());
            bool const inCapital = capital && capital->mapId == map->GetId();
            if (inCapital && capital->team != me->GetTeam())
                return out;     // (an enemy city: no trainer of ours)
            float const range = inCapital ? 2.0f * capital->radius : TownTrip::TRIP_YD;
            for (ServiceNpc const& n : BotShared::NpcDirectory::Near(map->GetId(), leader->GetPositionX(), leader->GetPositionY(), range, UNIT_NPC_FLAG_TRAINER))
            {
                CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(n.entry);
                if (!info || info->trainer_type != TRAINER_TYPE_TRADESKILLS)
                    continue;
                if (inCapital)
                {
                    float const dx = n.x - capital->x;
                    float const dy = n.y - capital->y;
                    if (dx * dx + dy * dy > capital->radius * capital->radius)
                        continue;
                }
                Creature* npc = map->GetCreature(BotShared::NpcDirectory::GuidOf(n));
                if (npc && (!npc->IsAlive() || npc->IsInCombat() || me->IsHostileTo(npc)))
                    continue;
                if (!inCapital && (!npc || !Errands::WalkIsSafe(me, npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ(), TownTrip::MAX_PATH_DETOUR)))
                    continue;
                out.push_back({ n, inCapital });
            }
            return out;
        }

        std::string SkillName(uint32 skill, LocaleConstant locale)
        {
            SkillLineEntry const* line = sSkillLineStore.LookupEntry(skill);
            if (!line)
                return std::string();
            char const* name = line->name[locale] && *line->name[locale] ? line->name[locale] : line->name[0];
            return name ? name : "";
        }
    }

    bool ProfessionPlanner::Worth(Player* me)
    {
        uint32 const spendable = Spendable(me, m_lessons);
        for (P::Lesson const& lesson : P::Available(me))
            if (lesson.rank && lesson.cost <= spendable)
                return true;
        return false;
    }

    void ProfessionPlanner::PlanStops(Player* me, Player const* leader, TripPlanStops& plan)
    {
        m_planned.clear();
        uint32 const spendable = Spendable(me, m_lessons);
        // By skill: its affordable lessons, the rank first (Available's order).
        std::map<uint32, std::vector<P::Lesson>> bySkill;
        for (P::Lesson const& lesson : P::Available(me))
            if (lesson.cost <= spendable)
                bySkill[lesson.skill].push_back(lesson);
        if (bySkill.empty())
            return;

        std::vector<Candidate> const candidates = Candidates(me, leader);
        std::set<uint32> used;      // spawn db guids
        for (auto const& itr : bySkill)
        {
            // The trainer that teaches the most important lesson (a rank before recipes), nearest first.
            Candidate const* best = nullptr;
            for (P::Lesson const& lesson : itr.second)
            {
                for (Candidate const& c : candidates)
                    if (!used.count(c.npc.dbGuid) && P::Teaches(c.npc.entry, lesson.spellId))
                    {
                        best = &c;
                        break;
                    }
                if (best)
                    break;
            }
            if (!best)
                continue;
            used.insert(best->npc.dbGuid);
            m_planned.insert(itr.first);

            TripStop stop;
            stop.farSpawn = best->farSpawn;
            stop.target = BotShared::NpcDirectory::GuidOf(best->npc);
            stop.x = best->npc.x;
            stop.y = best->npc.y;
            stop.z = best->npc.z;
            stop.placeKey = "place.trainer.profession";
            stop.action = std::make_shared<ProfessionAction>(m_lessons);
            stop.last = true;
            plan.Add(stop);
        }
    }

    void ProfessionPlanner::Notes(Player* me, Player const* reader, std::vector<Requests::Ask::Note>& out)
    {
        LocaleConstant const locale = reader && reader->GetSession() ? reader->GetSession()->GetSessionDbcLocale() : LOCALE_enUS;
        std::map<uint32, uint32> recipes;
        for (P::Lesson const& lesson : P::Available(me))
        {
            if (!m_planned.count(lesson.skill))
                continue;
            if (lesson.rank)
                out.push_back({ 0, Lessons::NameOf(lesson.spellId, locale, BotShared::I18n::Of(reader)) });
            else
                ++recipes[lesson.skill];
        }
        for (auto const& itr : recipes)
        {
            BotShared::Phrases::Vars vars;
            vars["skill"] = SkillName(itr.first, locale);
            vars["count"] = std::to_string(itr.second);
            out.push_back({ 0, BotShared::Phrases::Say(BotShared::I18n::OfParty(me), "training.recipes", {}, vars) });
        }
    }
}
}
