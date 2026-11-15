#include "Commander/Trace/FightLoadJson.h"
#include "Commander/Trace/Trace.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <map>

namespace Commander
{
namespace FightLoadJson
{
    namespace
    {
        // float numbers: written as the shortest text of the float itself (header).
        using Json = nlohmann::basic_json<std::map, std::vector, std::string, bool, std::int64_t, std::uint64_t, float>;

        float Num(Json const& j, char const* key)
        {
            auto const itr = j.find(key);
            return itr != j.end() && itr->is_number() ? itr->get<float>() : 0.0f;
        }
    }

    std::string InputJson(FightLoad::Input const& in)
    {
        Json j;
        j["partyDps"] = in.partyDps;
        j["tankHealth"] = in.tankHealth;
        j["tankMax"] = in.tankMaxHealth;
        Json foes = Json::array();
        for (FightLoad::Foe const& f : in.foes)
            foes.push_back({ { "g", Trace::Guid(f.guid) }, { "hp", f.health }, { "dps", f.dps }, { "at", f.arriveSec },
                             { "same", f.sameFight }, { "after", Trace::Guid(f.parent) }, { "life", f.lifeSec },
                             { "burst", f.burst }, { "on", f.holder }, { "hdps", f.holderDps } });
        j["foes"] = std::move(foes);
        Json holders = Json::array();
        for (FightLoad::Holder const& h : in.holders)
            holders.push_back({ { "g", Trace::Guid(h.guid) }, { "hp", h.health }, { "max", h.maxHealth } });
        j["holders"] = std::move(holders);
        Json healers = Json::array();
        for (FightLoad::Healer const& h : in.healers)
        {
            Json rates = Json::array();
            for (HealRate const& r : h.rates)
                rates.push_back({ { "hps", r.hps }, { "perMana", r.perMana }, { "cast", r.castSec } });
            healers.push_back({ { "mana", h.mana }, { "max", h.maxMana }, { "regen", h.regenPerSec }, { "rest", h.restRegenPerSec }, { "rates", std::move(rates) } });
        }
        j["healers"] = std::move(healers);
        return j.dump();
    }

    std::string ResultJson(FightLoad::Result const& r)
    {
        Json j;
        j["sec"] = r.fightSec;
        j["damage"] = r.damage;
        j["healed"] = r.healed;
        j["peak"] = r.peakDps;
        j["low"] = r.lowestTankPct;
        j["manaStart"] = r.manaStart;
        j["manaLeft"] = r.manaLeft;
        j["dies"] = r.tankDies;
        j["unfinished"] = r.unfinished;
        j["left"] = r.healthLeftPct;
        j["holderLow"] = r.lowestHolderPct;
        j["holderGaveUp"] = r.holderGaveUp;
        return j.dump();
    }

    bool ParseInput(std::string const& text, FightLoad::Input& out, std::string& error)
    {
        Json const j = Json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object())
        {
            error = "not a JSON object";
            return false;
        }
        out = FightLoad::Input();
        out.partyDps = Num(j, "partyDps");
        out.tankHealth = Num(j, "tankHealth");
        out.tankMaxHealth = Num(j, "tankMax");
        auto const foes = j.find("foes");
        if (foes == j.end() || !foes->is_array())
        {
            error = "no foes array";
            return false;
        }
        for (Json const& f : *foes)
        {
            FightLoad::Foe foe;
            if (f.contains("g") && f["g"].is_string())
                foe.guid = ObjectGuid(uint64(std::strtoull(f["g"].get<std::string>().c_str(), nullptr, 16)));
            foe.health = Num(f, "hp");
            foe.dps = Num(f, "dps");
            foe.arriveSec = Num(f, "at");
            foe.sameFight = f.contains("same") && f["same"].is_boolean() && f["same"].get<bool>();
            foe.holder = f.contains("on") && f["on"].is_number() ? int(f["on"].get<float>()) : -1;
            foe.holderDps = Num(f, "hdps");
            out.foes.push_back(foe);
        }
        auto const holders = j.find("holders");
        if (holders != j.end() && holders->is_array())
            for (Json const& h : *holders)
            {
                FightLoad::Holder holder;
                holder.health = Num(h, "hp");
                holder.maxHealth = Num(h, "max");
                out.holders.push_back(holder);
            }
        auto const healers = j.find("healers");
        if (healers != j.end() && healers->is_array())
        {
            for (Json const& h : *healers)
            {
                FightLoad::Healer healer;
                healer.mana = Num(h, "mana");
                healer.maxMana = Num(h, "max");
                healer.regenPerSec = Num(h, "regen");
                healer.restRegenPerSec = Num(h, "rest");
                auto const rates = h.find("rates");
                if (rates != h.end() && rates->is_array())
                    for (Json const& r : *rates)
                    {
                        HealRate rate;
                        rate.hps = Num(r, "hps");
                        rate.perMana = Num(r, "perMana");
                        rate.castSec = Num(r, "cast");
                        healer.rates.push_back(rate);
                    }
                out.healers.push_back(std::move(healer));
            }
        }
        return true;
    }

    std::string StepsJson(std::vector<FightLoad::Step> const& steps)
    {
        Json out = Json::array();
        for (FightLoad::Step const& s : steps)
            out.push_back({ { "t", s.t }, { "in", s.incoming }, { "heal", s.healed }, { "tank", s.tank }, { "mana", s.mana },
                            { "hp", s.health } });
        return out.dump();
    }
}
}
