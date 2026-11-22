#ifndef MANGOS_SPELLQUEUE_H
#define MANGOS_SPELLQUEUE_H

#include "Platform/Define.h"
#include "SpellCastTargetsInfo.h"

class Player;
class SpellEntry;

// The modern client's spell queue (CVar SpellQueueWindow): a cast pressed while the current cast,
// the GCD or the spell's own cooldown has only a moment left is sent early. The server holds it and
// starts it as soon as nothing blocks it, instead of failing it with "another action is in progress".
// One cast is held at a time; a new one replaces it.
class SpellQueue
{
public:
    // The client sends at most SpellQueueWindow (max 400 ms) early; the rest covers the update tick
    // and the time the client's cast bar runs ahead of the server's.
    static constexpr uint32 WindowMs = 500;
    // Safety net: a held cast still blocked after this long is started anyway and fails normally.
    static constexpr uint32 ExpireMs = 3000;

    // true = the cast is held; false = start it now (nothing blocks it, or waiting would not help)
    bool Offer(Player const* player, uint32 spellId, SpellCastTargets const& targets);
    // Called every player update: starts the held cast once nothing blocks it.
    void Update(Player* player);
    // true = the held cast was this spell and is dropped
    bool Cancel(uint32 spellId);
    void Clear() { m_spellId = 0; }

private:
    // ms until spellInfo could start; -1 = waiting will not make it start (permanent cooldown)
    static int32 WaitMs(Player const* player, SpellEntry const* spellInfo);

    uint32 m_spellId = 0;
    SpellCastTargets m_targets;
    uint32 m_queuedAtMs = 0;
};

#endif
