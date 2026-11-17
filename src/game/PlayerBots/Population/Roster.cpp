#include "Population/Roster.h"
#include "Population/Growth.h"
#include "Population/PopulationIdentity.h"
#include "Database/DatabaseEnv.h"
#include "Config/Config.h"
#include "Group.h"
#include "Item.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Utilities/Random.h"
#include <algorithm>
#include <cstdio>
#include <ctime>

namespace Population
{
    namespace
    {
        uint32 const UPDATE_EVERY_MS = 5 * IN_MILLISECONDS;
        uint32 const HOME_CITY_WEIGHT = 6;

        uint32 ClampPct(int32 value)
        {
            return uint32(std::min(std::max(value, 0), 100));
        }

        uint32 HomeCityOfRace(uint8 race)
        {
            switch (race)
            {
                case RACE_HUMAN:    return 1519;    // Stormwind
                case RACE_DWARF:
                case RACE_GNOME:    return 1537;    // Ironforge
                case RACE_NIGHTELF: return 1657;    // Darnassus
                case RACE_ORC:
                case RACE_TROLL:    return 1637;    // Orgrimmar
                case RACE_UNDEAD:   return 1497;    // Undercity
                case RACE_TAUREN:   return 1638;    // Thunder Bluff
            }
            return 0;
        }

        uint32 RandomCityOfTeam(uint32 team, std::mt19937& rng)
        {
            static uint32 const alliance[] = { 1519, 1537, 1657 };
            static uint32 const horde[] = { 1637, 1497, 1638 };
            uint32 const* cities = team == HORDE ? horde : alliance;
            return cities[Identity::Roll(&rng, 0, 2)];
        }

        uint32 OtherTeam(uint32 team)
        {
            return team == HORDE ? ALLIANCE : HORDE;
        }

        struct Candidate
        {
            RosterMember* member;
            uint8 level;
        };
    }

    void Roster::LoadConfig()
    {
        m_enabled = sConfig.GetBoolDefault("PopBot.Roster.Enable", true);
        m_cohortSize = uint32(std::min(std::max(sConfig.GetIntDefault("PopBot.Roster.CohortSize", 300), 10), 1000));
        m_leadCap = uint8(std::min(std::max(sConfig.GetIntDefault("PopBot.Roster.LeadCap", 3), 0), 10));
        m_stallPct = ClampPct(sConfig.GetIntDefault("PopBot.Roster.StallPct", 15));
        m_leaderPct = ClampPct(sConfig.GetIntDefault("PopBot.Roster.LeaderPct", 10));
        m_quitPct = ClampPct(sConfig.GetIntDefault("PopBot.Roster.QuitPct", 30));
        m_enemyPct = ClampPct(sConfig.GetIntDefault("PopBot.Roster.EnemyPct", 10));
        m_elderSharePct = ClampPct(sConfig.GetIntDefault("PopBot.Roster.ElderSharePct", 30));
        uint32 const cooldownMin = uint32(std::min(std::max(sConfig.GetIntDefault("PopBot.Roster.RespawnCooldownMin", 30), 0), 24 * 60));
        m_cooldownMs = cooldownMin * MINUTE * IN_MILLISECONDS;
    }

    bool Roster::EnsureTables()
    {
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `custom_popbot_world` ("
            " `id` TINYINT UNSIGNED NOT NULL,"
            " `seed` BIGINT UNSIGNED NOT NULL,"
            " `created_at` INT UNSIGNED NOT NULL DEFAULT 0,"
            " PRIMARY KEY (`id`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='population roster: world seed'");

        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `custom_popbot_cohort` ("
            " `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            " `owner_guid` INT UNSIGNED NOT NULL,"
            " `owner_name` VARCHAR(12) NOT NULL,"
            " `team` SMALLINT UNSIGNED NOT NULL,"
            " `max_level` TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            " `gear_cap` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'best average item level worn in the owner group',"
            " `free` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'owner deleted: anchor frozen',"
            " `created_at` INT UNSIGNED NOT NULL DEFAULT 0,"
            " PRIMARY KEY (`id`),"
            " KEY `idx_owner` (`owner_guid`, `free`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='population roster: one per real character'");

        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `custom_popbot_member` ("
            " `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,"
            " `cohort_id` INT UNSIGNED NOT NULL,"
            " `name` VARCHAR(12) NOT NULL,"
            " `race` TINYINT UNSIGNED NOT NULL,"
            " `class` TINYINT UNSIGNED NOT NULL,"
            " `gender` TINYINT UNSIGNED NOT NULL,"
            " `skin` TINYINT UNSIGNED NOT NULL,"
            " `face` TINYINT UNSIGNED NOT NULL,"
            " `hair_style` TINYINT UNSIGNED NOT NULL,"
            " `hair_color` TINYINT UNSIGNED NOT NULL,"
            " `facial_hair` TINYINT UNSIGNED NOT NULL,"
            " `home_zone` SMALLINT UNSIGNED NOT NULL,"
            " `pace` FLOAT NOT NULL,"
            " `stall_level` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            " `quit_level` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            " `jitter` TINYINT NOT NULL DEFAULT 0,"
            " `wealth` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            " `temperament` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            " `char_guid` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'real character once recruited (phase R4)',"
            " `created_at` INT UNSIGNED NOT NULL DEFAULT 0,"
            " PRIMARY KEY (`id`),"
            " UNIQUE KEY `idx_name` (`name`),"
            " KEY `idx_cohort` (`cohort_id`)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='population roster: fixed identities'");

        std::unique_ptr<QueryResult> exists(CharacterDatabase.Query("SHOW TABLES LIKE 'custom_popbot_member'"));
        if (!exists)
            return false;

        // Tables from phase R1 (before gear_cap).
        std::unique_ptr<QueryResult> gearCap(CharacterDatabase.Query("SHOW COLUMNS FROM `custom_popbot_cohort` LIKE 'gear_cap'"));
        if (!gearCap)
            CharacterDatabase.DirectExecute("ALTER TABLE `custom_popbot_cohort` ADD COLUMN `gear_cap` SMALLINT UNSIGNED NOT NULL DEFAULT 0 AFTER `max_level`");
        return true;
    }

    void Roster::LoadSeed()
    {
        std::unique_ptr<QueryResult> result(CharacterDatabase.Query("SELECT `seed` FROM `custom_popbot_world` WHERE `id` = 1"));
        if (result)
        {
            m_seed = result->Fetch()[0].GetUInt64();
            return;
        }

        m_seed = (uint64(urand(0, 0xFFFFFFFF)) << 32) ^ uint64(urand(0, 0xFFFFFFFF)) ^ uint64(time(nullptr));
        CharacterDatabase.DirectPExecute("INSERT INTO `custom_popbot_world` (`id`, `seed`, `created_at`) VALUES (1, %llu, %u)",
            (unsigned long long)m_seed, uint32(time(nullptr)));
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] Roster: new world seed %llu.", (unsigned long long)m_seed);
    }

    void Roster::LoadCohorts()
    {
        std::vector<Cohort> old;
        old.swap(m_cohorts);

        std::unique_ptr<QueryResult> result(CharacterDatabase.Query(
            "SELECT `id`, `owner_guid`, `owner_name`, `team`, `max_level`, `free`, `gear_cap` FROM `custom_popbot_cohort`"));
        if (!result)
            return;
        do
        {
            Field* f = result->Fetch();
            Cohort c;
            c.id = f[0].GetUInt32();
            c.ownerGuid = f[1].GetUInt32();
            c.ownerName = f[2].GetCppString();
            c.team = f[3].GetUInt32();
            c.maxLevel = std::max<uint8>(f[4].GetUInt8(), 1);
            c.free = f[5].GetBool();
            c.gearCap = f[6].GetUInt32();
            auto prev = std::find_if(old.begin(), old.end(), [&c](Cohort const& o) { return o.id == c.id; });
            if (prev != old.end())
            {
                c.ownerOnline = prev->ownerOnline && !c.free;
                c.maxLevel = std::max(c.maxLevel, prev->maxLevel);  // async updates not written yet
                c.gearCap = std::max(c.gearCap, prev->gearCap);
            }
            m_cohorts.push_back(std::move(c));
        } while (result->NextRow());
    }

    void Roster::LoadMembers()
    {
        // Runtime state survives a reload (.popbot reload while bots are online).
        std::vector<RosterMember> old;
        old.swap(m_members);

        std::unique_ptr<QueryResult> result(CharacterDatabase.Query(
            "SELECT `id`, `cohort_id`, `name`, `race`, `class`, `gender`, `skin`, `face`, `hair_style`, `hair_color`,"
            " `facial_hair`, `home_zone`, `pace`, `stall_level`, `quit_level`, `jitter`, `wealth`, `temperament`"
            " FROM `custom_popbot_member` ORDER BY `id`"));
        if (!result)
            return;
        do
        {
            Field* f = result->Fetch();
            RosterMember m;
            m.id = f[0].GetUInt32();
            m.cohortId = f[1].GetUInt32();
            m.name = f[2].GetCppString();
            m.race = f[3].GetUInt8();
            m.classId = f[4].GetUInt8();
            m.look.gender = f[5].GetUInt8();
            m.look.skin = f[6].GetUInt8();
            m.look.face = f[7].GetUInt8();
            m.look.hairStyle = f[8].GetUInt8();
            m.look.hairColor = f[9].GetUInt8();
            m.look.facialHair = f[10].GetUInt8();
            m.homeZone = f[11].GetUInt32();
            m.pace = f[12].GetFloat();
            m.stallLevel = f[13].GetUInt8();
            m.quitLevel = f[14].GetUInt8();
            m.jitter = int8(f[15].GetInt32());
            m.wealth = f[16].GetUInt8();
            m.temperament = f[17].GetUInt8();

            if (!Identity::IsValidCombo(m.race, m.classId))
                continue;

            auto prev = std::find_if(old.begin(), old.end(), [&m](RosterMember const& o) { return o.id == m.id; });
            if (prev != old.end())
            {
                m.inUse = prev->inUse;
                m.availableAtMs = prev->availableAtMs;
                m.sessionGuid = prev->sessionGuid;
            }
            m_members.push_back(std::move(m));
        } while (result->NextRow());
    }

    void Roster::Load()
    {
        m_available = false;
        if (!EnsureTables())
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[PopBot] Roster: could not create the custom_popbot_* tables in the characters database.");
            m_cohorts.clear();
            m_members.clear();
            return;
        }

        LoadSeed();

        // The owner was deleted, or its guid now belongs to another character.
        CharacterDatabase.DirectExecute(
            "UPDATE `custom_popbot_cohort` c LEFT JOIN `characters` ch ON ch.`guid` = c.`owner_guid`"
            " SET c.`free` = 1 WHERE c.`free` = 0 AND (ch.`guid` IS NULL OR ch.`name` <> c.`owner_name`)");

        LoadCohorts();
        LoadMembers();
        m_available = true;
        m_nextUpdateMs = 0;

        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] Roster: %u cohorts, %u members.", uint32(m_cohorts.size()), uint32(m_members.size()));
    }

    Cohort* Roster::FindCohort(uint32 id)
    {
        for (Cohort& c : m_cohorts)
            if (c.id == id)
                return &c;
        return nullptr;
    }

    Cohort const* Roster::FindCohort(uint32 id) const
    {
        for (Cohort const& c : m_cohorts)
            if (c.id == id)
                return &c;
        return nullptr;
    }

    Cohort* Roster::FindOwnedCohort(uint32 ownerGuid)
    {
        for (Cohort& c : m_cohorts)
            if (!c.free && c.ownerGuid == ownerGuid)
                return &c;
        return nullptr;
    }

    RosterMember* Roster::FindMember(uint32 id)
    {
        for (RosterMember& m : m_members)
            if (m.id == id)
                return &m;
        return nullptr;
    }

    void Roster::FreeCohort(Cohort& cohort)
    {
        cohort.free = true;
        cohort.ownerOnline = false;
        // Direct: CreateCohort reloads the cohorts right after this.
        CharacterDatabase.DirectPExecute("UPDATE `custom_popbot_cohort` SET `free` = 1 WHERE `id` = %u", cohort.id);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] Roster: cohort %u of '%s' is free now.", cohort.id, cohort.ownerName.c_str());
    }

    bool Roster::GenerateMember(std::mt19937& rng, uint32 cohortTeam, Identity const& identity,
        std::set<std::string>& reserved, RosterMember& out) const
    {
        uint32 const team = Identity::Roll(&rng, 0, 99) < m_enemyPct ? OtherTeam(cohortTeam) : cohortTeam;
        out.race = Identity::RandomRace(team, &rng);
        out.classId = Identity::RandomClass(out.race, &rng);
        if (!out.classId)
            return false;

        out.name = identity.MakeName(out.race, reserved, &rng);
        if (out.name.empty())
            return false;
        reserved.insert(out.name);

        out.look.gender = uint8(Identity::Roll(&rng, 0, 1));
        Player::SelectRandomAppearance(out.race, out.look.gender, out.look.hairStyle, out.look.hairColor,
            out.look.face, out.look.facialHair, out.look.skin);

        // Most live in the capital of their race, some moved to another city of their faction.
        out.homeZone = Identity::Roll(&rng, 0, 99) < 85 ? HomeCityOfRace(out.race) : RandomCityOfTeam(team, rng);

        uint32 const kind = Identity::Roll(&rng, 0, 99);
        if (kind < m_stallPct)
        {
            // Stopped early: stays low for good.
            out.stallLevel = uint8(Identity::Roll(&rng, 1, 10));
            out.pace = 0.0f;
        }
        else if (kind < m_stallPct + m_leaderPct)
            out.pace = 1.0f + float(Identity::Roll(&rng, 0, 100)) / 1000.0f;     // 1.0 - 1.1
        else
        {
            std::normal_distribution<float> pace(0.8f, 0.12f);
            out.pace = std::min(std::max(pace(rng), 0.5f), 1.0f);
        }

        out.quitLevel = Identity::Roll(&rng, 0, 99) < m_quitPct ? uint8(Identity::Roll(&rng, 15, 58)) : 0;
        out.jitter = int8(Identity::Roll(&rng, 0, 2)) - 1;
        // Most are poor: square of a uniform roll.
        uint32 const w = Identity::Roll(&rng, 0, 100);
        out.wealth = uint8(w * w / 100);
        out.temperament = uint8(Identity::Roll(&rng, 0, 7));
        return true;
    }

    void Roster::CreateCohort(Player* owner, Identity const& identity)
    {
        std::string ownerName = owner->GetName();
        CharacterDatabase.escape_string(ownerName);
        uint32 const ownerGuid = owner->GetGUIDLow();
        uint32 const team = owner->GetTeam();
        uint32 const now = uint32(time(nullptr));

        CharacterDatabase.DirectPExecute(
            "INSERT INTO `custom_popbot_cohort` (`owner_guid`, `owner_name`, `team`, `max_level`, `free`, `created_at`)"
            " VALUES (%u, '%s', %u, %u, 0, %u)", ownerGuid, ownerName.c_str(), team, std::max<uint32>(owner->GetLevel(), 1), now);

        std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
            "SELECT MAX(`id`) FROM `custom_popbot_cohort` WHERE `owner_guid` = %u AND `free` = 0", ownerGuid));
        uint32 const cohortId = result ? result->Fetch()[0].GetUInt32() : 0;
        if (!cohortId)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[PopBot] Roster: could not create a cohort for '%s'.", owner->GetName());
            return;
        }

        LoadCohorts();
        if (Cohort const* cohort = FindCohort(cohortId))
            FillCohort(*cohort, identity);
    }

    void Roster::FillCohort(Cohort const& cohort, Identity const& identity)
    {
        uint32 have = 0;
        for (RosterMember const& m : m_members)
            if (m.cohortId == cohort.id)
                ++have;
        if (have >= m_cohortSize)
            return;

        uint32 const cohortId = cohort.id;
        uint32 const team = cohort.team;
        uint32 const now = uint32(time(nullptr));

        // Same world seed, owner and size = same members.
        std::seed_seq seq{ uint32(m_seed), uint32(m_seed >> 32), cohort.ownerGuid, cohortId, have };
        std::mt19937 rng(seq);

        std::set<std::string> reserved;
        AddNamesTo(reserved);

        std::string sql = "INSERT INTO `custom_popbot_member` (`cohort_id`, `name`, `race`, `class`, `gender`, `skin`, `face`,"
            " `hair_style`, `hair_color`, `facial_hair`, `home_zone`, `pace`, `stall_level`, `quit_level`, `jitter`, `wealth`,"
            " `temperament`, `created_at`) VALUES ";
        uint32 made = 0;
        for (uint32 i = have; i < m_cohortSize; ++i)
        {
            RosterMember m;
            if (!GenerateMember(rng, team, identity, reserved, m))
                continue;

            char row[256];
            snprintf(row, sizeof(row), "%s(%u, '%s', %u, %u, %u, %u, %u, %u, %u, %u, %u, %.3f, %u, %u, %d, %u, %u, %u)",
                made ? ", " : "", cohortId, m.name.c_str(), m.race, m.classId, m.look.gender, m.look.skin, m.look.face,
                m.look.hairStyle, m.look.hairColor, m.look.facialHair, m.homeZone, m.pace, m.stallLevel, m.quitLevel,
                int32(m.jitter), m.wealth, m.temperament, now);
            sql += row;
            ++made;
        }
        if (made)
            CharacterDatabase.DirectExecute(sql);

        std::string const ownerName = cohort.ownerName;   // LoadCohorts below replaces `cohort`
        LoadCohorts();
        LoadMembers();
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[PopBot] Roster: cohort %u of '%s': %u new members, %u in total.",
            cohortId, ownerName.c_str(), made, have + made);
    }

    void Roster::UpdateAnchor(Cohort& cohort, Player* owner)
    {
        uint8 const level = uint8(std::min<uint32>(owner->GetLevel(), 60));
        if (level > cohort.maxLevel)
        {
            cohort.maxLevel = level;
            CharacterDatabase.PExecute("UPDATE `custom_popbot_cohort` SET `max_level` = %u WHERE `id` = %u", level, cohort.id);
        }

        // Team gear: the best average item level in the owner's group. Temporary bots don't count
        // (their gear was handed out, not earned).
        auto averageItemLevel = [](Player* p)
        {
            uint32 sum = 0, count = 0;
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            {
                if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
                    continue;
                if (Item* item = p->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                {
                    sum += item->GetProto()->ItemLevel;
                    ++count;
                }
            }
            return count ? sum / count : 0;
        };

        uint32 best = averageItemLevel(owner);
        if (Group* group = owner->GetGroup())
        {
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* member = itr->getSource();
                if (!member || member == owner || (member->IsBot() && member->IsSavingDisabled()))
                    continue;
                best = std::max(best, averageItemLevel(member));
            }
        }

        if (best > cohort.gearCap)
        {
            cohort.gearCap = best;
            CharacterDatabase.PExecute("UPDATE `custom_popbot_cohort` SET `gear_cap` = %u WHERE `id` = %u", best, cohort.id);
        }
    }

    void Roster::Update(uint32 nowMs, Identity const& identity)
    {
        if (!IsActive() || nowMs < m_nextUpdateMs)
            return;
        m_nextUpdateMs = nowMs + UPDATE_EVERY_MS;

        for (Cohort& c : m_cohorts)
            c.ownerOnline = false;

        for (auto const& itr : sObjectAccessor.GetPlayers())
        {
            Player* p = itr.second;
            if (!p || !p->IsInWorld() || p->IsBot())
                continue;

            Cohort* cohort = FindOwnedCohort(p->GetGUIDLow());
            if (cohort && cohort->ownerName != p->GetName())
            {
                FreeCohort(*cohort);
                cohort = nullptr;
            }
            if (!cohort)
            {
                CreateCohort(p, identity);
                cohort = FindOwnedCohort(p->GetGUIDLow());
                if (!cohort)
                    continue;
            }

            cohort->ownerOnline = true;
            UpdateAnchor(*cohort, p);

            // PopBot.Roster.CohortSize was raised: top the cohort up (no-op when full).
            uint32 const cohortId = cohort->id;
            FillCohort(*cohort, identity);
            if (Cohort* reloaded = FindCohort(cohortId))
                reloaded->ownerOnline = true;
        }
    }

    uint8 Roster::LevelOf(RosterMember const& member) const
    {
        Cohort const* c = FindCohort(member.cohortId);
        return Growth::Level(member, c ? c->maxLevel : 1, m_leadCap);
    }

    bool Roster::Pick(RosterQuery const& query, uint32 nowMs, RosterPick& out)
    {
        if (!IsActive())
            return false;

        std::vector<Candidate> own, elders;
        for (RosterMember& m : m_members)
        {
            if (m.inUse || nowMs < m.availableAtMs)
                continue;
            if (uint32(Player::TeamForRace(m.race)) != query.team)
                continue;
            Cohort const* c = FindCohort(m.cohortId);
            if (!c || Growth::IsRetired(m, c->maxLevel))
                continue;
            uint8 const level = Growth::Level(m, c->maxLevel, m_leadCap);
            if (level < query.minLevel || level > query.maxLevel)
                continue;
            // A real character took the name (temporary bots keep their guid in the name cache).
            ObjectGuid const holder = sObjectMgr.GetPlayerGuidByName(m.name);
            if (!holder.IsEmpty() && holder.GetCounter() != m.sessionGuid)
                continue;

            (c->ownerOnline && !c->free ? own : elders).push_back({ &m, level });
        }

        bool const wantElder = urand(0, 99) < m_elderSharePct;
        std::vector<Candidate>& pool = (wantElder && !elders.empty()) || own.empty() ? elders : own;
        if (pool.empty())
            return false;

        Candidate chosen = pool.front();
        if (query.preferHigh)
        {
            std::sort(pool.begin(), pool.end(), [](Candidate const& a, Candidate const& b) { return a.level > b.level; });
            chosen = pool[urand(0, std::min<uint32>(uint32(pool.size()), 5) - 1)];
        }
        else
        {
            // Mostly locals: Stormwind is mostly humans living there.
            uint32 total = 0;
            for (Candidate const& c : pool)
                total += c.member->homeZone == query.zone ? HOME_CITY_WEIGHT : 1;
            uint32 roll = urand(1, total);
            for (Candidate const& c : pool)
            {
                uint32 const weight = c.member->homeZone == query.zone ? HOME_CITY_WEIGHT : 1;
                if (roll <= weight)
                {
                    chosen = c;
                    break;
                }
                roll -= weight;
            }
        }

        RosterMember& m = *chosen.member;
        m.inUse = true;
        out.memberId = m.id;
        out.name = m.name;
        out.race = m.race;
        out.classId = m.classId;
        out.level = chosen.level;
        out.look = m.look;
        out.sessionGuid = m.sessionGuid;
        Cohort const* cohort = FindCohort(m.cohortId);
        out.gear = Growth::Gear(m, chosen.level, cohort ? cohort->gearCap : 0);
        out.money = Growth::Money(m, chosen.level);
        return true;
    }

    void Roster::SetSessionGuid(uint32 memberId, uint32 guid)
    {
        if (RosterMember* m = FindMember(memberId))
            m->sessionGuid = guid;
    }

    void Roster::MarkInUse(uint32 memberId)
    {
        if (RosterMember* m = FindMember(memberId))
            m->inUse = true;
    }

    void Roster::Release(uint32 memberId, uint32 nowMs)
    {
        if (RosterMember* m = FindMember(memberId))
        {
            m->inUse = false;
            m->availableAtMs = nowMs + m_cooldownMs;
        }
    }

    void Roster::AddNamesTo(std::set<std::string>& names) const
    {
        for (RosterMember const& m : m_members)
            names.insert(m.name);
    }

    std::vector<RosterCohortStats> Roster::GetStats() const
    {
        std::vector<RosterCohortStats> stats;
        for (Cohort const& c : m_cohorts)
        {
            RosterCohortStats s;
            s.cohort = c;
            for (RosterMember const& m : m_members)
            {
                if (m.cohortId != c.id)
                    continue;
                ++s.members;
                if (uint32(Player::TeamForRace(m.race)) != c.team)
                    ++s.enemy;
                if (m.inUse)
                    ++s.inUse;
                if (Growth::IsRetired(m, c.maxLevel))
                {
                    ++s.retired;
                    continue;
                }
                ++s.byTen[std::min<uint32>(Growth::Level(m, c.maxLevel, m_leadCap) / 10, 6)];
            }
            stats.push_back(std::move(s));
        }
        return stats;
    }
}
