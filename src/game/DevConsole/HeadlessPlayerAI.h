/*
 * AI of a character logged in without a client by the dev console (login(name) in Lua).
 *
 * It does nothing by itself: Lua scripts move it, pick targets and cast for it, so a test
 * can play the human leader of a party bot group. It acknowledges teleports (near: base
 * class UpdateAI, far: SMSG_NEW_WORLD), which a client would normally do.
 *
 * Casts go through the client's handler (CastSpell) with the session's own checks, so the
 * server answers with SMSG_CAST_RESULT like for a client. The AI keeps the last answer.
 *
 * The session uses the character's real account. When a client logs into that account,
 * World::AddSession_ deletes the headless session and the client takes over.
 */

#ifndef MANGOS_DEVCONSOLE_HEADLESSPLAYERAI_H
#define MANGOS_DEVCONSOLE_HEADLESSPLAYERAI_H

#include "Player.h"
#include "PlayerBotAI.h"

namespace DevConsole
{
    struct HeadlessCastResult
    {
        uint32 spellId = 0;
        bool ok = false;
        uint8 reason = 0;       // SpellCastResult when !ok
        uint32 timeMs = 0;      // server ms clock; 0 = no answer yet
    };

    class HeadlessPlayerAI : public PlayerBotAI
    {
        public:
            HeadlessPlayerAI() : PlayerBotAI(nullptr) {}

            void OnPacketReceived(WorldPacket const* packet) override;

            HeadlessCastResult const& GetLastCast() const { return m_lastCast; }
            void ClearLastCast() { m_lastCast = HeadlessCastResult(); }

        private:
            HeadlessCastResult m_lastCast;
    };

    inline HeadlessPlayerAI* GetHeadlessAI(Player* p)
    {
        return (p && p->AI()) ? dynamic_cast<HeadlessPlayerAI*>(p->AI()) : nullptr;
    }
}

#endif
