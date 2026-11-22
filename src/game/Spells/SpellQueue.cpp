#include "SpellQueue.h"

#include "Player.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "WorldSession.h"
#include "Timer.h"

#include <algorithm>

int32 SpellQueue::WaitMs(Player const* player, SpellEntry const* spellInfo)
{
    int32 const cooldownMs = player->GetSpellCooldownLeft(spellInfo);
    if (cooldownMs < 0)
        return -1;

    uint32 waitMs = std::max<uint32>(uint32(cooldownMs), player->GetGCDLeft(spellInfo));

    // Same check as Spell::prepare: channels and auto-repeat (wand, shoot) do not block a new cast.
    if (player->IsNonMeleeSpellCasted(false, true, true))
    {
        uint32 castMs = 1;  // casting with no time left: ends this update
        if (Spell const* spell = player->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            if (spell->getState() == SPELL_STATE_PREPARING)
                castMs = std::max<uint32>(castMs, spell->GetCastedTime());   // m_timer counts down
        waitMs = std::max(waitMs, castMs);
    }
    return int32(waitMs);
}

bool SpellQueue::Offer(Player const* player, uint32 spellId, SpellCastTargets const& targets)
{
    // A new request always replaces the held one, whether or not it is held itself.
    Clear();

    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(spellId);
    if (!spellInfo || spellInfo->IsPassiveSpell())
        return false;

    // These do not use the generic cast slot.
    if (spellInfo->IsNextMeleeSwingSpell() || spellInfo->IsAutoRepeatRangedSpell())
        return false;

    int32 const waitMs = WaitMs(player, spellInfo);
    if (waitMs <= 0 || uint32(waitMs) > WindowMs)
        return false;

    m_spellId = spellId;
    m_targets = targets;
    m_queuedAtMs = WorldTimer::getMSTime();
    return true;
}

void SpellQueue::Update(Player* player)
{
    if (!m_spellId)
        return;

    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(m_spellId);
    bool const expired = WorldTimer::getMSTimeDiffToNow(m_queuedAtMs) > ExpireMs;
    if (spellInfo && !expired && WaitMs(player, spellInfo) > 0)
        return;

    uint32 const spellId = m_spellId;
    SpellCastTargets const targets = m_targets;
    Clear();
    player->GetSession()->CastSpellFromClient(spellId, targets);
}

bool SpellQueue::Cancel(uint32 spellId)
{
    if (!m_spellId || m_spellId != spellId)
        return false;
    Clear();
    return true;
}
