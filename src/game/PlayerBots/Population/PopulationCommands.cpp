/*
 * .popbot [subcommand]   GM commands for world population bots.
 *
 *   .popbot                              status
 *   .popbot on | off                     enable / disable (off logs every population bot out)
 *   .popbot stats [reset]                cost of population bot AI ticks
 *   .popbot list                         population bots: level, race, class, distance, time left
 *   .popbot roster                       roster cohorts: owner, anchor level, level spread
 *   .popbot hubs                         settlements (gathering places) within 1500 yards
 *   .popbot town                         towns with a player near or within 1500 yards: anchors, residents, targets
 *   .popbot density                      field density at your position, the field bots wanted there
 *   .popbot headless on|off              dev tests: the headless character counts as a player (default off)
 *   .popbot spawn [count] [level] [ally|horde|enemy] [showcase|preraid]
 *                                        spawn idle test bots around you (one every SpawnGapMs)
 *   .popbot emote one|state|stand|text|sheath <id> | end
 *                                        dev review: the selected (else nearest) town/city/test bot
 *                                        shows one action and holds still (addon /pbemote)
 *   .popbot despawn                      log out the selected population bot
 *   .popbot clear                        log out every population bot (cities refill while you are there)
 *   .popbot city                         per city: anchors, bots by role, targets for this hour
 *   .popbot city anchors                 anchors of your city within 60 yards and how full they are
 *   .popbot fill                         run the city director now instead of within 5 seconds
 *   .popbot point add <city> <type> [route] [capacity]
 *                                        record your position and facing as a scene point
 *   .popbot point list                   scene points within 60 yards
 *   .popbot point del <id>               delete a scene point
 *   .popbot reload                       re-read PopBot.* (run .reload config first if
 *                                        mangosd.conf was edited), names and scene points
 *
 * Runs on the world thread while map threads are idle.
 */

#include "Chat.h"
#include "Player.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "Population/PopulationMgr.h"
#include "Population/PopulationBotAI.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace
{
    char const* RaceName(uint8 race)
    {
        switch (race)
        {
            case RACE_HUMAN:    return "Human";
            case RACE_ORC:      return "Orc";
            case RACE_DWARF:    return "Dwarf";
            case RACE_NIGHTELF: return "NightElf";
            case RACE_UNDEAD:   return "Undead";
            case RACE_TAUREN:   return "Tauren";
            case RACE_GNOME:    return "Gnome";
            case RACE_TROLL:    return "Troll";
        }
        return "?";
    }

    char const* ClassName(uint8 classId)
    {
        switch (classId)
        {
            case CLASS_WARRIOR: return "Warrior";
            case CLASS_PALADIN: return "Paladin";
            case CLASS_HUNTER:  return "Hunter";
            case CLASS_ROGUE:   return "Rogue";
            case CLASS_PRIEST:  return "Priest";
            case CLASS_SHAMAN:  return "Shaman";
            case CLASS_MAGE:    return "Mage";
            case CLASS_WARLOCK: return "Warlock";
            case CLASS_DRUID:   return "Druid";
        }
        return "?";
    }

    // Next whitespace separated word of *args (ChatHandler::ExtractLiteralArg is protected).
    char* NextToken(char** args)
    {
        if (!args || !*args)
            return nullptr;
        char* p = *args;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (!*p)
        {
            *args = p;
            return nullptr;
        }
        char* start = p;
        while (*p && *p != ' ' && *p != '\t')
            ++p;
        if (*p)
            *p++ = '\0';
        *args = p;
        return start;
    }

    bool ParseUInt(char const* text, uint32& value)
    {
        if (!text || !*text)
            return false;
        for (char const* c = text; *c; ++c)
            if (!std::isdigit(static_cast<unsigned char>(*c)))
                return false;
        value = uint32(std::strtoul(text, nullptr, 10));
        return true;
    }

    void ShowStatus(ChatHandler* handler)
    {
        Population::PopulationMgr& mgr = sPopulationMgr;
        handler->PSendSysMessage("Population bots: %s, %u bots (max %u), %u queued, AI update %u ms",
            mgr.IsEnabled() ? "|cff00ff00on|r" : "|cffff0000off|r",
            mgr.GetBotCount(), mgr.GetMaxBots(), mgr.GetQueuedCount(), mgr.GetUpdateInterval());

        Population::ScenePoints& points = mgr.GetScenePoints();
        if (points.IsAvailable())
            handler->PSendSysMessage("Scene points: %u. Names in custom_popbot_name: %u.",
                uint32(points.All().size()), mgr.GetIdentity().GetTableNameCount());
        else
            handler->SendSysMessage("Tables custom_popbot_point / custom_popbot_name missing: import sql/custom/popbot.sql into the world database, then .popbot reload.");
    }

    void ShowStats(ChatHandler* handler)
    {
        Population::TickStats const s = sPopulationMgr.GetStats();
        double const avg = s.ticks ? double(s.totalMicros) / double(s.ticks) : 0.0;
        double const corePct = s.elapsedMs ? double(s.totalMicros) / (double(s.elapsedMs) * 1000.0) * 100.0 : 0.0;
        handler->PSendSysMessage("Population AI ticks in %.1f s: %llu, avg %.1f us, max %llu us, %.3f%% of one core",
            double(s.elapsedMs) / 1000.0, (unsigned long long)s.ticks, avg, (unsigned long long)s.maxMicros, corePct);
    }

    void ShowList(ChatHandler* handler, Player* viewer)
    {
        std::vector<Population::BotInfo> bots = sPopulationMgr.ListBots();
        if (bots.empty())
        {
            handler->SendSysMessage("No population bots.");
            return;
        }

        uint32 shown = 0;
        for (Population::BotInfo const& b : bots)
        {
            if (++shown > 30)
            {
                handler->PSendSysMessage("... %u more", uint32(bots.size() - 30));
                break;
            }

            char where[48];
            if (viewer && b.mapId == viewer->GetMapId())
                snprintf(where, sizeof(where), "%.0f yd", viewer->GetDistance(b.x, b.y, b.z));
            else
                snprintf(where, sizeof(where), "map %u", b.mapId);

            char kind[48];
            if (b.role != Population::CityRole::None)
                snprintf(kind, sizeof(kind), "%s %s", Population::BotKindName(b.kind), Population::CityRoleName(b.role));
            else
                snprintf(kind, sizeof(kind), "%s", Population::BotKindName(b.kind));

            handler->PSendSysMessage("%s %u %s %s [%s/%s]%s %s, %s, %u min left%s",
                b.name.c_str(), b.level, Population::Identity::IsValidCombo(b.race, b.classId) ? RaceName(b.race) : "?",
                ClassName(b.classId), kind, Population::GearTierName(b.tier),
                !b.rosterId ? "" : Population::Roster::IsStranger(b.rosterId) ? " stranger" : " roster",
                b.online ? "online" : "loading", where, b.leftMs / (MINUTE * IN_MILLISECONDS),
                b.removing ? " (leaving)" : "");
        }
    }

    void ShowRoster(ChatHandler* handler)
    {
        Population::Roster const& roster = sPopulationMgr.GetRoster();
        if (!roster.IsAvailable())
        {
            handler->SendSysMessage("Roster tables missing in the characters database (see the server log).");
            return;
        }

        std::vector<Population::RosterCohortStats> const stats = roster.GetStats();
        handler->PSendSysMessage("Roster %s, seed %llu, %u cohorts, %u strangers online.", roster.IsActive() ? "on" : "off (PopBot.Roster.Enable)",
            (unsigned long long)roster.GetSeed(), uint32(stats.size()), roster.GetStrangerCount());
        for (Population::RosterCohortStats const& s : stats)
        {
            handler->PSendSysMessage("#%u %s (%s)%s%s anchor %u: %u members, %u enemy faction, %u retired, %u online",
                s.cohort.id, s.cohort.ownerName.c_str(), s.cohort.team == HORDE ? "Horde" : "Alliance",
                s.cohort.free ? " free" : "", s.cohort.ownerOnline ? " owner online" : "", s.cohort.maxLevel,
                s.members, s.enemy, s.retired, s.inUse);
            handler->PSendSysMessage("   levels 1-9: %u, 10s: %u, 20s: %u, 30s: %u, 40s: %u, 50s: %u, 60: %u",
                s.byTen[0], s.byTen[1], s.byTen[2], s.byTen[3], s.byTen[4], s.byTen[5], s.byTen[6]);
        }
    }

    // A city, town or test bot's AI (field bots are run by the Commander: no review).
    Population::PopulationBotAI* ReviewableAI(Player* bot)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || !bot->AI())
            return nullptr;
        Population::PopulationBotAI* ai = dynamic_cast<Population::PopulationBotAI*>(bot->AI());
        if (!ai || !ai->IsInitialized() || ai->GetRequest().kind == Population::BotKind::Field ||
            ai->GetRequest().kind == Population::BotKind::Traveller)
            return nullptr;
        return ai;
    }

    // The selected population bot, else the nearest one within 30 yards.
    Population::PopulationBotAI* ReviewBot(Player* selected, Player* player)
    {
        if (Population::PopulationBotAI* ai = ReviewableAI(selected))
            return ai;
        Population::PopulationBotAI* best = nullptr;
        float bestDist = 30.0f;
        for (auto const& ref : player->GetMap()->GetPlayers())
        {
            Player* p = ref.getSource();
            Population::PopulationBotAI* ai = p != player ? ReviewableAI(p) : nullptr;
            if (!ai)
                continue;
            float const d = player->GetDistance(p);
            if (d < bestDist)
            {
                best = ai;
                bestDist = d;
            }
        }
        return best;
    }

    // .popbot emote one|state|stand|text|sheath <id> | end
    // Replies for the addon (Emotes.lua): PBEMOTE;ok;<bot>  PBEMOTE;end;<bot>  PBEMOTE;err;nobot|usage
    bool ReviewEmote(ChatHandler* handler, Player* player, Player* selected, char* args)
    {
        using Kind = Population::PopulationBotAI::ReviewKind;
        char* kindText = NextToken(&args);
        Population::PopulationBotAI* ai = kindText ? ReviewBot(selected, player) : nullptr;
        if (kindText && !ai)
        {
            handler->SendSysMessage("PBEMOTE;err;nobot");
            return false;
        }

        if (kindText && !strcmp(kindText, "end"))
        {
            ai->EndReview();
            handler->PSendSysMessage("PBEMOTE;end;%s", ai->GetBotName().c_str());
            return true;
        }

        uint32 id = 0;
        bool const hasId = ParseUInt(NextToken(&args), id);
        Kind kind = Kind::OneShot;
        bool known = true;
        if (!kindText)
            known = false;
        else if (!strcmp(kindText, "one"))
            kind = Kind::OneShot;
        else if (!strcmp(kindText, "state"))
            kind = Kind::State;
        else if (!strcmp(kindText, "stand"))
            kind = Kind::Stand;
        else if (!strcmp(kindText, "text"))
            kind = Kind::Text;
        else if (!strcmp(kindText, "sheath"))
            kind = Kind::Sheath;
        else
            known = false;
        if (!known || !hasId)
        {
            handler->SendSysMessage("PBEMOTE;err;usage");
            handler->SendSysMessage("Usage: .popbot emote one|state|stand|text|sheath <id> | end (selected population bot, else the nearest within 30 yd)");
            return false;
        }

        ai->PlayForReview(kind, id, player);
        handler->PSendSysMessage("PBEMOTE;ok;%s", ai->GetBotName().c_str());
        return true;
    }

    bool Spawn(ChatHandler* handler, Player* player, char* args)
    {
        uint32 count = 1;
        uint32 level = 0;
        uint32 numbers = 0;
        uint32 team = player->GetTeam();
        Population::GearTier tier = Population::GearTier::Normal;

        while (char* token = NextToken(&args))
        {
            uint32 value = 0;
            if (ParseUInt(token, value))
            {
                if (numbers == 0)
                    count = value;
                else
                    level = value;
                ++numbers;
            }
            else if (!strcmp(token, "ally") || !strcmp(token, "alliance"))
                team = ALLIANCE;
            else if (!strcmp(token, "horde"))
                team = HORDE;
            else if (!strcmp(token, "enemy"))
                team = player->GetTeam() == ALLIANCE ? HORDE : ALLIANCE;
            else if (!strcmp(token, "showcase"))
                tier = Population::GearTier::Showcase;
            else if (!strcmp(token, "preraid"))
                tier = Population::GearTier::PreRaid;
            else
            {
                handler->PSendSysMessage("Unknown option '%s'. Usage: .popbot spawn [count] [level] [ally|horde|enemy] [showcase|preraid]", token);
                return false;
            }
        }

        if (count < 1 || count > 20 || level > 60)
        {
            handler->SendSysMessage("count must be 1-20, level 1-60 (0 = random).");
            return false;
        }

        if (!sPopulationMgr.IsEnabled())
        {
            handler->SendSysMessage("Population bots are off. Use .popbot on first.");
            return false;
        }

        uint32 queued = 0;
        for (uint32 i = 0; i < count; ++i)
        {
            Population::SpawnRequest request;
            request.kind = Population::BotKind::Test;
            request.team = team;
            request.level = uint8(level);
            request.tier = tier;
            request.mapId = player->GetMapId();
            request.faceGuid = player->GetObjectGuid();

            float x = player->GetPositionX();
            float y = player->GetPositionY();
            float z = player->GetPositionZ();
            if (!player->GetRandomPoint(x, y, z, 15.0f, request.x, request.y, request.z))
            {
                request.x = x;
                request.y = y;
                request.z = z;
            }
            request.o = frand(0.0f, 2.0f * float(M_PI));

            if (!sPopulationMgr.QueueSpawn(request))
                break;
            ++queued;
        }

        handler->PSendSysMessage("Queued %u of %u population bots (one every few seconds).", queued, count);
        if (queued < count)
            handler->PSendSysMessage("Limit reached: PopBot.MaxBots = %u.", sPopulationMgr.GetMaxBots());
        return queued > 0;
    }

    bool PointAdd(ChatHandler* handler, Player* player, char* args)
    {
        char* cityArg = NextToken(&args);
        char* typeArg = NextToken(&args);
        if (!cityArg || !typeArg)
        {
            handler->PSendSysMessage("Usage: .popbot point add <city> <type> [route] [capacity]. Cities: %s. Types: %s.",
                Population::ScenePoints::CityList(), Population::ScenePoints::TypeList());
            return false;
        }

        Population::ScenePoint p;
        p.city = cityArg;
        p.type = typeArg;
        if (!Population::ScenePoints::IsValidCity(p.city) || !Population::ScenePoints::IsValidType(p.type))
        {
            handler->PSendSysMessage("Unknown city or type. Cities: %s. Types: %s.",
                Population::ScenePoints::CityList(), Population::ScenePoints::TypeList());
            return false;
        }
        bool const worldType = p.type == "hub" || p.type == "road";
        if ((p.city == "world") != worldType)
        {
            handler->SendSysMessage("Outside the cities: .popbot point add world hub (a gathering place) or world road (a point a road passes). City points cannot be either.");
            return false;
        }

        if (p.type == "route")
        {
            char* routeArg = NextToken(&args);
            if (!routeArg || !Population::ScenePoints::IsValidRouteName(routeArg))
            {
                handler->SendSysMessage("Type route needs a route name: lowercase letters, digits and _ (max 32), e.g. bank_ah.");
                return false;
            }
            p.route = routeArg;
        }

        if (char* capArg = NextToken(&args))
        {
            uint32 cap = 0;
            if (!ParseUInt(capArg, cap) || cap < 1 || cap > 50)
            {
                handler->SendSysMessage("capacity must be 1-50.");
                return false;
            }
            p.capacity = cap;
        }

        Population::ScenePoints& points = sPopulationMgr.GetScenePoints();
        if (!points.IsAvailable())
        {
            handler->SendSysMessage("Table custom_popbot_point missing: import sql/custom/popbot.sql into the world database, then .popbot reload.");
            return false;
        }

        if (!p.route.empty())
            p.seq = points.NextSeq(p.city, p.route);
        p.mapId = player->GetMapId();
        p.x = player->GetPositionX();
        p.y = player->GetPositionY();
        p.z = player->GetPositionZ();
        p.o = player->GetOrientation();

        uint32 const id = points.Add(p);
        if (!id)
        {
            handler->SendSysMessage("Insert failed, see the server log.");
            return false;
        }
        sPopulationMgr.ReloadAnchors();

        if (p.route.empty())
            handler->PSendSysMessage("Scene point %u: %s %s, capacity %u.", id, p.city.c_str(), p.type.c_str(), p.capacity);
        else
            handler->PSendSysMessage("Scene point %u: %s route %s #%u.", id, p.city.c_str(), p.route.c_str(), p.seq);
        return true;
    }

    void ShowCities(ChatHandler* handler)
    {
        Population::CityDirector& city = sPopulationMgr.GetCityDirector();
        Population::CityTargets const t = city.TargetsNow();
        handler->PSendSysMessage("Cities: %s. Hour factor %.2f -> target %u bots (%u movers, %u crafters, %u veterans) per city with a player.",
            city.IsEnabled() ? "on" : "off", city.HourFactor(), t.total, t.mover, t.crafter, t.showcase);

        for (Population::CityState const& s : city.States())
        {
            uint32 ah = 0, bank = 0, mail = 0, other = 0;
            for (Population::CityAnchor const& a : s.anchors)
            {
                if (a.type == "ah") ++ah;
                else if (a.type == "bank") ++bank;
                else if (a.type == "mail") ++mail;
                else ++other;
            }
            Population::CityCounts const c = sPopulationMgr.CountPlace(Population::PlaceKey::City(s.def->zone), s.anchors.size());
            handler->PSendSysMessage("%s (zone %u): %s%s, anchors ah %u bank %u mail %u other %u, bots %u (stand %u, movers %u, crafters %u, veterans %u)",
                s.def->title, s.def->zone, s.enabled ? "enabled" : "disabled", s.arriving ? ", player arriving" : s.playerPresent ? ", player here" : "",
                ah, bank, mail, other, c.total, c.stand, c.mover, c.crafter, c.showcase);
        }
    }

    void ShowAnchors(ChatHandler* handler, Player* player)
    {
        Population::CityDirector& city = sPopulationMgr.GetCityDirector();
        Population::CityState const* state = city.FindState(player->GetCachedZoneId());
        if (!state)
        {
            handler->SendSysMessage("You are not in a city zone.");
            return;
        }

        Population::CityCounts const c = sPopulationMgr.CountPlace(Population::PlaceKey::City(state->def->zone), state->anchors.size());
        uint32 shown = 0;
        for (size_t i = 0; i < state->anchors.size(); ++i)
        {
            Population::CityAnchor const& a = state->anchors[i];
            float const dist = player->GetDistance(a.x, a.y, a.z);
            if (dist > 60.0f)
                continue;
            if (++shown > 20)
                break;
            handler->PSendSysMessage("[%u] %s %s, %.1f yd, %u/%u used%s", uint32(i), a.type.c_str(), a.label.c_str(),
                dist, i < c.anchorUse.size() ? c.anchorUse[i] : 0, a.capacity, a.exactSpot ? ", exact spot" : "");
        }
        handler->PSendSysMessage("%s: %u anchors, %u within 60 yards.", state->def->title, uint32(state->anchors.size()), shown);
    }

    // Settlements (Settlements.h) near the player, or a count per kind from the console.
    void ShowHubs(ChatHandler* handler, Player* player)
    {
        Population::Settlements const& hubs = sPopulationMgr.GetSettlements();
        uint32 byKind[5] = {};
        for (Population::Settlement const& s : hubs.All())
            ++byKind[uint8(s.kind)];
        handler->PSendSysMessage("Settlements: %u (capitals %u, start areas %u, towns %u, camps %u, manual %u).",
            uint32(hubs.All().size()), byKind[0], byKind[1], byKind[2], byKind[3], byKind[4]);
        if (!player)
            return;

        float const RANGE = 1500.0f;
        auto const nearby = hubs.Near(player->GetMapId(), player->GetPositionX(), player->GetPositionY(), RANGE);
        uint32 shown = 0;
        for (auto const& n : nearby)
        {
            if (++shown > 15)
            {
                handler->PSendSysMessage("... %u more", uint32(nearby.size() - 15));
                break;
            }
            Population::Settlement const& s = *n.second;
            std::string const where = s.kind == Population::SettlementKind::Capital ? s.label : Population::Settlements::AreaName(s);
            handler->PSendSysMessage("#%u %s %s (%s), %s, %.0f yd from the edge, core %.0f, falloff %.0f, weight %.1f, %u sources",
                s.id, Population::SettlementKindName(s.kind), where.c_str(), s.label.c_str(),
                s.alliance && s.horde ? "both sides" : s.alliance ? "Alliance" : "Horde",
                n.first, s.coreYd, s.falloffYd, s.weight, s.sources);
        }
        handler->PSendSysMessage("%u within %.0f yards of your position.", uint32(nearby.size()), RANGE);
    }

    // Field density at the player's position (FieldPopulation.h) and what makes it.
    void ShowDensity(ChatHandler* handler, Player* player)
    {
        Population::Settlements const& hubs = sPopulationMgr.GetSettlements();
        Population::FieldDirector const& field = sPopulationMgr.GetFieldDirector();
        uint32 const team = player->GetTeam();
        uint32 const mapId = player->GetMapId();
        float const x = player->GetPositionX(), y = player->GetPositionY();

        Population::Observer o;
        o.mapId = mapId;
        o.x = x;
        o.y = y;
        uint32 const here = sPopulationMgr.CountField(o, field.GetRadius(), Population::FieldDirector::BEHIND_SPENT_YD);
        handler->PSendSysMessage("Field density here %.2f: %u of %u field bots wanted within %.0f yd, %u there now.",
            field.DensityAt(hubs, mapId, x, y, team), field.BudgetAt(hubs, mapId, x, y, team), field.GetMaxUnits(),
            field.GetRadius(), here);

        uint32 shown = 0;
        for (auto const& n : hubs.Near(mapId, x, y, 3000.0f))
        {
            Population::Settlement const& s = *n.second;
            if (!s.ServesTeam(team))
                continue;
            if (++shown > 3)
                break;
            std::string const where = s.kind == Population::SettlementKind::Capital ? s.label : Population::Settlements::AreaName(s);
            handler->PSendSysMessage("   %s %s: %.0f yd beyond its core, draws %.2f", Population::SettlementKindName(s.kind),
                where.c_str(), n.first, s.weight * std::exp(-n.first / s.falloffYd));
        }
    }

    // Roads (TravellerPopulation.h): the network's build, the roads near your position and how
    // often a traveller comes by.
    void ShowRoads(ChatHandler* handler, Player* player)
    {
        Population::TravellerDirector const& travel = sPopulationMgr.GetTravellerDirector();
        Commander::Roads::RoadNetwork const& roads = travel.Roads();
        handler->PSendSysMessage("Roads: %s, %u built of %u planned (%u done, %u failed%s), %u hint points, %u ms building. At most %u travellers near a player.",
            travel.IsEnabled() ? "on" : "off", uint32(roads.Edges().size()), roads.Planned(), roads.Done(), roads.Failed(),
            roads.IsBuilding() ? ", building" : "", roads.HintCount(), roads.BuildMs(), travel.GetMaxNear());
        if (!player)
            return;

        Population::Settlements const& hubs = sPopulationMgr.GetSettlements();
        auto nameOf = [&hubs](uint32 id) -> std::string
        {
            Population::Settlement const* s = hubs.Find(id);
            if (!s)
                return "?";
            return s->kind == Population::SettlementKind::Capital ? s->label : Population::Settlements::AreaName(*s);
        };

        float const RANGE = 600.0f;
        uint32 shown = 0;
        for (Commander::Roads::RoadEdge const* e : roads.Near(player->GetMapId(), player->GetPositionX(), player->GetPositionY(), RANGE))
        {
            if (++shown > 10)
                break;
            float dist = 0.0f;
            float const along = Commander::Roads::RoadNetwork::Project(*e, player->GetPositionX(), player->GetPositionY(), dist);
            handler->PSendSysMessage("#%u %s - %s: %.0f yd, %u points (%u via patrols/road points), %.1f an hour; you are %.0f yd off it, %.0f yd along",
                e->id, nameOf(e->nodeA).c_str(), nameOf(e->nodeB).c_str(), e->length, uint32(e->points.size()), e->hints,
                travel.PerHour(*e), dist, along);
        }

        Population::Observer o;
        o.mapId = player->GetMapId();
        o.x = player->GetPositionX();
        o.y = player->GetPositionY();
        o.team = player->GetTeam();
        float const rate = travel.RateFor(o, hubs, sPopulationMgr.GetCityDirector().HourFactor());
        uint32 const nearCount = sPopulationMgr.CountTravellers(o, Population::TravellerDirector::NEAR_COUNT_YD);
        if (rate > 0.0f)
            handler->PSendSysMessage("Here: a traveller every %.0f s on average (roads within %.0f yd, hour factor applied), %u travellers within %.0f yd.",
                1.0f / rate, Population::TravellerDirector::NEAR_ROAD_YD, nearCount, Population::TravellerDirector::NEAR_COUNT_YD);
        else
            handler->PSendSysMessage("Here: no road of your side within %.0f yd, %u travellers within %.0f yd.",
                Population::TravellerDirector::NEAR_ROAD_YD, nearCount, Population::TravellerDirector::NEAR_COUNT_YD);
    }

    // Quest areas (Commander/Perception/Spatial/QuestAreas.h) near your position, acted out by
    // field bots (FieldDirector::BuildQuestRequest).
    void ShowQuests(ChatHandler* handler, Player* player)
    {
        Population::FieldDirector const& field = sPopulationMgr.GetFieldDirector();
        Commander::Quests::QuestAreaIndex const& quests = field.Quests();
        handler->PSendSysMessage("Quest areas: %u, ways %u%s (%u ms). %u%% of the field bots near one act out its quest.",
            uint32(quests.All().size()), quests.WaysBuilt(), quests.IsBuilding() ? ", building" : "", quests.BuildMs(), field.GetQuestPct());
        if (!player)
            return;

        uint32 shown = 0;
        for (Commander::Quests::QuestArea const* a : quests.Near(player->GetMapId(), player->GetPositionX(), player->GetPositionY(),
                 Population::FieldDirector::QUEST_NEAR_YD))
        {
            if (++shown > 10)
                break;
            Quest const* quest = sObjectMgr.GetQuestTemplate(a->questId);
            CreatureInfo const* prey = sObjectMgr.GetCreatureTemplate(a->preyEntry);
            CreatureInfo const* giver = sObjectMgr.GetCreatureTemplate(a->giverEntry);
            handler->PSendSysMessage("#%u quest %u \"%s\" (level %u): %u x %s (%u-%u), %u spawns within %.0f yd, %.0f yd from you; giver %s, way %.0f yd%s",
                a->id, a->questId, quest ? quest->GetTitle().c_str() : "?", uint32(a->questLevel), a->need,
                prey ? prey->name.c_str() : "?", uint32(a->preyLevelMin), uint32(a->preyLevelMax), uint32(a->spawns.size()), a->radiusYd,
                player->GetDistance2d(a->center.x, a->center.y), giver ? giver->name.c_str() : "?", a->wayLength,
                a->ServesTeam(player->GetTeam()) ? "" : " (not your side)");
        }
        if (!shown)
            handler->PSendSysMessage("No quest area within %.0f yd.", Population::FieldDirector::QUEST_NEAR_YD);
    }

    // Towns (TownPopulation.h): those with a player near, and the ones near your position.
    void ShowTowns(ChatHandler* handler, Player* player)
    {
        Population::TownDirector& towns = sPopulationMgr.GetTownDirector();
        float const hour = sPopulationMgr.GetCityDirector().HourFactor();
        handler->PSendSysMessage("Towns: %s, %u places, hour factor %.2f.", towns.IsEnabled() ? "on" : "off",
            uint32(towns.States().size()), hour);

        uint32 shown = 0;
        for (Population::TownState const& t : towns.States())
        {
            Population::Settlement const& s = *t.settlement;
            float dist = -1.0f;
            if (player && player->GetMapId() == s.mapId)
                dist = std::max(0.0f, player->GetDistance2d(s.x, s.y) - s.coreYd);
            if (!t.playerPresent && (dist < 0.0f || dist > 1500.0f))
                continue;
            if (++shown > 10)
                break;

            std::map<std::string, uint32> byType;
            for (Population::TownAnchor const& a : t.anchors)
                ++byType[a.spot.type];
            std::string types;
            for (auto const& kv : byType)
                types += " " + kv.first + " " + std::to_string(kv.second);

            Population::CityCounts const c = sPopulationMgr.CountPlace(Population::PlaceKey::Town(s.id), t.anchors.size());
            Population::TownTargets const target = towns.TargetsFor(t, hour);
            handler->PSendSysMessage("#%u %s %s%s, levels %u-%u, anchors:%s", s.id, Population::SettlementKindName(s.kind),
                Population::Settlements::AreaName(s).c_str(), t.arriving ? " (player arriving)" : t.playerPresent ? " (player near)" : "",
                t.levelMin, t.levelMax, types.empty() ? " none" : types.c_str());
            handler->PSendSysMessage("   bots %u/%u (sitters %u/%u, movers %u/%u, stand %u)%s", c.total, target.total,
                c.sitter, target.sitter, c.mover, target.mover, c.stand,
                dist >= 0.0f ? (", " + std::to_string(int32(dist)) + " yd from you").c_str() : "");
        }
    }

    bool PointList(ChatHandler* handler, Player* player)
    {
        Population::ScenePoints& points = sPopulationMgr.GetScenePoints();
        if (!points.IsAvailable())
        {
            handler->SendSysMessage("Table custom_popbot_point missing: import sql/custom/popbot.sql into the world database, then .popbot reload.");
            return false;
        }

        std::vector<std::pair<float, Population::ScenePoint const*>> nearby;
        for (Population::ScenePoint const& p : points.All())
        {
            if (p.mapId != player->GetMapId())
                continue;
            float const dist = player->GetDistance(p.x, p.y, p.z);
            if (dist <= 60.0f)
                nearby.emplace_back(dist, &p);
        }
        std::sort(nearby.begin(), nearby.end(), [](auto const& a, auto const& b) { return a.first < b.first; });

        handler->PSendSysMessage("%u scene points in total, %u within 60 yards:", uint32(points.All().size()), uint32(nearby.size()));
        uint32 shown = 0;
        for (auto const& n : nearby)
        {
            if (++shown > 20)
            {
                handler->PSendSysMessage("... %u more", uint32(nearby.size() - 20));
                break;
            }
            Population::ScenePoint const& p = *n.second;
            if (p.route.empty())
                handler->PSendSysMessage("#%u %s %s cap %u, %.1f yd", p.id, p.city.c_str(), p.type.c_str(), p.capacity, n.first);
            else
                handler->PSendSysMessage("#%u %s route %s step %u, %.1f yd", p.id, p.city.c_str(), p.route.c_str(), p.seq, n.first);
        }
        return true;
    }
}

bool ChatHandler::HandlePopBotCommand(char* args)
{
    // Status-type subcommands also work from the console (SOAP / wowdbg.ps1 -Cmd);
    // spawn, despawn, point and city anchors need a character.
    Player* player = m_session ? m_session->GetPlayer() : nullptr;

    char* sub = ExtractLiteralArg(&args);
    bool const needsPlayer = sub && (!strcmp(sub, "spawn") || !strcmp(sub, "despawn") || !strcmp(sub, "point") || !strcmp(sub, "density") ||
        !strcmp(sub, "emote") ||
        (!strcmp(sub, "city") && args && strstr(args, "anchors")));
    if (needsPlayer && !player)
    {
        SendSysMessage("This subcommand needs a character (use it in game).");
        return false;
    }
    if (!sub || !strcmp(sub, "status"))
    {
        ShowStatus(this);
        return true;
    }

    if (!strcmp(sub, "on") || !strcmp(sub, "off"))
    {
        bool const on = !strcmp(sub, "on");
        sPopulationMgr.SetEnabled(on);
        ShowStatus(this);
        return true;
    }

    if (!strcmp(sub, "stats"))
    {
        char* opt = ExtractLiteralArg(&args);
        if (opt && !strcmp(opt, "reset"))
        {
            sPopulationMgr.ResetStats();
            SendSysMessage("Population AI stats reset.");
            return true;
        }
        ShowStats(this);
        return true;
    }

    if (!strcmp(sub, "list"))
    {
        ShowList(this, player);
        return true;
    }

    if (!strcmp(sub, "roster"))
    {
        ShowRoster(this);
        return true;
    }

    if (!strcmp(sub, "hubs"))
    {
        ShowHubs(this, player);
        return true;
    }

    if (!strcmp(sub, "roads"))
    {
        ShowRoads(this, player);
        return true;
    }

    if (!strcmp(sub, "quests"))
    {
        ShowQuests(this, player);
        return true;
    }

    if (!strcmp(sub, "town"))
    {
        ShowTowns(this, player);
        return true;
    }

    if (!strcmp(sub, "density"))
    {
        ShowDensity(this, player);
        return true;
    }

    if (!strcmp(sub, "headless"))
    {
        char* opt = ExtractLiteralArg(&args);
        bool const on = opt && !strcmp(opt, "on");
        sPopulationMgr.SetHeadlessObserves(on);
        PSendSysMessage("The dev console's headless character %s.", on ? "counts as a player (until off or restart)" : "is not a player");
        return true;
    }

    if (!strcmp(sub, "spawn"))
        return Spawn(this, player, args);

    if (!strcmp(sub, "emote"))
        return ReviewEmote(this, player, GetSelectedPlayer(), args);

    if (!strcmp(sub, "despawn"))
    {
        Player* target = GetSelectedPlayer();
        if (!target || !sPopulationMgr.Despawn(target->GetObjectGuid()))
        {
            SendSysMessage("Select a population bot first.");
            return false;
        }
        PSendSysMessage("%s is logging out.", target->GetName());
        return true;
    }

    if (!strcmp(sub, "city"))
    {
        char* opt = ExtractLiteralArg(&args);
        if (opt && !strcmp(opt, "anchors"))
            ShowAnchors(this, player);
        else
            ShowCities(this);
        return true;
    }

    if (!strcmp(sub, "fill"))
    {
        if (!sPopulationMgr.IsEnabled())
        {
            SendSysMessage("Population bots are off. Use .popbot on first.");
            return false;
        }
        sPopulationMgr.GetCityDirector().ForceTick();
        SendSysMessage("City director runs on the next update.");
        return true;
    }

    if (!strcmp(sub, "clear"))
    {
        PSendSysMessage("%u population bots are logging out.", sPopulationMgr.DespawnAll());
        return true;
    }

    if (!strcmp(sub, "point"))
    {
        char* action = ExtractLiteralArg(&args);
        if (action && !strcmp(action, "add"))
            return PointAdd(this, player, args);
        if (action && !strcmp(action, "list"))
            return PointList(this, player);
        if (action && !strcmp(action, "del"))
        {
            uint32 id = 0;
            if (!ExtractUInt32(&args, id) || !id)
            {
                SendSysMessage("Usage: .popbot point del <id>");
                return false;
            }
            if (!sPopulationMgr.GetScenePoints().Remove(id))
            {
                PSendSysMessage("No scene point %u.", id);
                return false;
            }
            sPopulationMgr.ReloadAnchors();
            PSendSysMessage("Scene point %u deleted.", id);
            return true;
        }
        SendSysMessage("Usage: .popbot point add <city> <type> [route] [capacity] | list | del <id>");
        return false;
    }

    if (!strcmp(sub, "reload"))
    {
        sPopulationMgr.LoadConfig();
        sPopulationMgr.LoadData();
        ShowStatus(this);
        return true;
    }

    SendSysMessage("Usage: .popbot [status|on|off|stats [reset]|list|roster|hubs|roads|quests|town|density|headless on|off|spawn ...|emote ...|despawn|clear|city [anchors]|fill|point ...|reload]");
    return false;
}
