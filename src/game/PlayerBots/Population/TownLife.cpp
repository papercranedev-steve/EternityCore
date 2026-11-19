#include "Population/TownLife.h"
#include "Population/CityPopulation.h"
#include "Life/Errands/ErrandCommon.h"
#include "GameObject.h"
#include "Map.h"
#include "Player.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Population
{
    namespace
    {
        // Beyond this a place is not on today's way (the far end of a capital).
        float const MAX_VISIT_YD = 250.0f;
        uint32 const PICK_TRIES = 4;
        uint32 const SPOT_TRIES = 3;
        float const CHAIR_REACH_YD = 1.2f;

        bool SpotTaken(Player* me, float x, float y)
        {
            for (auto const& ref : me->GetMap()->GetPlayers())
            {
                Player* p = ref.getSource();
                if (p != me && p->IsInWorld() && p->GetDistance2d(x, y, SizeFactor::None) < TownLife::SPOT_TAKEN_YD)
                    return true;
            }
            return false;
        }
    }

    void TownLife::Begin(Player* me, SpawnRequest const& request, uint32 now)
    {
        m_started = true;
        m_places = request.places;
        m_recent.clear();
        Life::Pastimes::Notice::Init(m_notice, me->GetObjectGuid().GetRawValue());

        if (m_places && request.anchorIndex >= 0 && size_t(request.anchorIndex) < m_places->size())
        {
            // A stander or a sitter: the day starts at its anchor, where it stands now.
            m_current = request.anchorIndex;
            m_dest = Life::Trip::Destination();
            m_dest.hasFace = request.hasFacePoint;
            m_dest.faceX = request.faceX;
            m_dest.faceY = request.faceY;
            StartStay(me, now);
            return;
        }
        m_current = -1;
        m_step = Step::Choosing;
        m_nextPickMs = now + urand(0, 3) * IN_MILLISECONDS;
    }

    void TownLife::Stop(Player* me)
    {
        if (m_step == Step::Walking)
            Life::Errands::StopWalk(me);
        else if (m_step == Step::Staying)
            Life::Pastimes::PlaceActs::End(me, m_act);
        Life::Pastimes::Notice::Stop(m_notice);
        m_wasLooking = false;
        m_step = Step::Choosing;
        m_nextPickMs = 0;
    }

    void TownLife::Update(Player* me, uint32 now)
    {
        if (!m_started || !m_places)
            return;

        switch (m_step)
        {
            case Step::Staying:
            {
                bool const free = Life::Pastimes::PlaceActs::CanLookAround(m_act);
                if (Life::Pastimes::Notice::Update(me, m_notice, free, now))
                {
                    m_wasLooking = true;
                    return;
                }
                if (m_wasLooking)
                {
                    m_wasLooking = false;
                    Life::Pastimes::PlaceActs::FaceAgain(me, m_act);
                }
                if (Life::Pastimes::PlaceActs::Update(me, m_act, now))
                    return;
                Life::Pastimes::PlaceActs::End(me, m_act);
                m_step = Step::Choosing;
                m_nextPickMs = now + urand(1, 4) * IN_MILLISECONDS;
                return;
            }
            case Step::Walking:
            {
                switch (Life::Trip::Approach::Update(me, m_dest, m_leg, now))
                {
                    case Life::Trip::ApproachResult::Walking:
                        return;
                    case Life::Trip::ApproachResult::Failed:
                        Life::Errands::StopWalk(me);
                        m_step = Step::Choosing;
                        m_nextPickMs = now + 2 * IN_MILLISECONDS;
                        return;
                    case Life::Trip::ApproachResult::Arrived:
                        StartStay(me, now);
                        return;
                }
                return;
            }
            case Step::Choosing:
            {
                if (now < m_nextPickMs || me->IsMoving())
                    return;
                if (!PickNext(me, now))
                    m_nextPickMs = now + RETRY_MS;
                return;
            }
        }
    }

    bool TownLife::PickNext(Player* me, uint32 now)
    {
        std::vector<PlaceAnchor> const& places = *m_places;
        std::vector<std::pair<int32, float>> candidates;
        float total = 0.0f;
        auto gather = [&](bool skipRecent)
        {
            for (size_t i = 0; i < places.size(); ++i)
            {
                PlaceAnchor const& p = places[i];
                int32 const index = int32(i);
                if (!p.visitWeight || index == m_current ||
                    (skipRecent && std::find(m_recent.begin(), m_recent.end(), index) != m_recent.end()))
                    continue;
                float const d = me->GetDistance2d(p.x, p.y, SizeFactor::None);
                if (d > MAX_VISIT_YD)
                    continue;
                float const score = float(p.visitWeight) * NEAR_YD / (NEAR_YD + d);
                candidates.emplace_back(index, score);
                total += score;
            }
        };
        gather(true);
        // (a camp with a few places: back to one of them)
        if (candidates.empty())
            gather(false);

        for (uint32 attempt = 0; attempt < PICK_TRIES && total > 0.0f; ++attempt)
        {
            float roll = frand(0.0f, total);
            size_t pick = candidates.size() - 1;
            for (size_t k = 0; k < candidates.size(); ++k)
            {
                if (roll < candidates[k].second)
                {
                    pick = k;
                    break;
                }
                roll -= candidates[k].second;
            }

            int32 const index = candidates[pick].first;
            Life::Trip::Destination dest;
            if (!PickSpot(me, places[index], dest))
            {
                // (taken or no ground: another one)
                total -= candidates[pick].second;
                candidates.erase(candidates.begin() + pick);
                continue;
            }

            float const d = me->GetDistance2d(dest.x, dest.y, SizeFactor::None);
            dest.run = d <= WALK_YD ? urand(0, 99) >= WALK_PCT : urand(0, 99) < RUN_FAR_PCT;
            m_dest = dest;
            m_current = index;
            if (me->IsSittingDown())
                me->SetStandState(UNIT_STAND_STATE_STAND);
            Life::Trip::Approach::Begin(m_leg, now);
            m_step = Step::Walking;
            Life::Trip::Approach::Update(me, m_dest, m_leg, now);
            return true;
        }

        // Nowhere else to go (one place, all taken): another while where it is.
        if (m_current >= 0 && size_t(m_current) < places.size() &&
            me->GetDistance2d(places[m_current].x, places[m_current].y, SizeFactor::None) < 10.0f)
        {
            StartStay(me, now);
            return true;
        }
        return false;
    }

    bool TownLife::PickSpot(Player* me, PlaceAnchor const& place, Life::Trip::Destination& out) const
    {
        out = Life::Trip::Destination();
        if (place.chairEntry)
        {
            // Onto the chair itself (PlaceActs takes its seat, if free).
            out.x = place.x;
            out.y = place.y;
            out.z = place.z;
            out.reachYd = CHAIR_REACH_YD;
            return true;
        }

        CityAnchor anchor;
        anchor.type = place.type;
        anchor.x = place.x;
        anchor.y = place.y;
        anchor.z = place.z;
        anchor.o = place.o;
        anchor.exactSpot = place.exactSpot;
        // In front of an NPC, all around an object (as the directors' stand spots).
        float const minDist = place.object ? 1.5f : 3.0f;
        float const maxDist = place.object ? 4.0f : 6.0f;
        float const spread = place.object ? float(M_PI) : 0.8f;

        for (uint32 attempt = 0; attempt < SPOT_TRIES; ++attempt)
        {
            Waypoint w;
            if (!CityDirector::PickStandSpot(me->GetMap(), anchor, minDist, maxDist, spread, w))
                return false;
            if (SpotTaken(me, w.x, w.y))
            {
                if (place.exactSpot)
                    return false;
                continue;
            }
            out.x = w.x;
            out.y = w.y;
            out.z = w.z;
            out.hasFace = true;
            out.faceX = w.faceX;
            out.faceY = w.faceY;
            return true;
        }
        return false;
    }

    void TownLife::StartStay(Player* me, uint32 now)
    {
        PlaceAnchor const& place = (*m_places)[m_current];
        m_recent.push_back(m_current);
        while (m_recent.size() > RECENT_KEEP)
            m_recent.pop_front();

        GameObject* chair = place.chairEntry ? me->FindNearestGameObject(place.chairEntry, 5.0f) : nullptr;
        Life::Pastimes::PlaceActs::Begin(me, m_act, Life::Pastimes::PlaceOfAnchor(place.type, place.object),
            m_dest.faceX, m_dest.faceY, m_dest.hasFace, chair, now);
        m_step = Step::Staying;
    }
}
