#include "DevConsole/HeadlessPlayerAI.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Opcodes.h"
#include "SpellDefines.h"
#include "Timer.h"

namespace DevConsole
{
    void HeadlessPlayerAI::OnPacketReceived(WorldPacket const* packet)
    {
        // Far teleport: the player is out of the world, so UpdateAI does not run. Answer like a
        // client that finished the loading screen (same as CombatBotBaseAI).
        if (packet->GetOpcode() == SMSG_NEW_WORLD)
        {
            if (me)
                me->GetSession()->QueuePacket(std::make_unique<NullClientPacket>(MSG_MOVE_WORLDPORT_ACK));
            return;
        }

        if (packet->GetOpcode() != SMSG_CAST_RESULT || packet->size() < 5)
            return;

        // spellId, status, [failureReason, ...] (WorldPackets::Spell::CastResult)
        uint8 const* data = packet->contents();
        HeadlessCastResult r;
        r.spellId = uint32(data[0]) | (uint32(data[1]) << 8) | (uint32(data[2]) << 16) | (uint32(data[3]) << 24);
        r.ok = data[4] != uint8(SPELL_RESULT_STATUS_FAIL);
        r.reason = (!r.ok && packet->size() >= 6) ? data[5] : uint8(0);
        r.timeMs = WorldTimer::getMSTime();
        m_lastCast = r;
    }
}
