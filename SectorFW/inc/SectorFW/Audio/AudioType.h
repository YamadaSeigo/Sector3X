#pragma once

namespace SFW::Audio
{

    // ---------------------------
    // Basic handles
    // ---------------------------
    struct SoundHandle
    {
        uint32_t id = 0;
        explicit operator bool() const noexcept { return id != 0; }
    };

    using VoiceID = uint64_t; // store SoLoud::handle; 0 = invalid

    // ---------------------------
    // Ticket: (index + generation) like EntityID
    // ---------------------------
    struct AudioTicketID
    {
        uint32_t index = UINT32_MAX;
        uint32_t generation = 0;

        bool IsValid() const noexcept { return index != UINT32_MAX; }
        static constexpr AudioTicketID Invalid() noexcept { return AudioTicketID{ UINT32_MAX, 0 }; }

        bool operator==(const AudioTicketID& o) const noexcept {
            return index == o.index && generation == o.generation;
        }
    };

    // ---------------------------
    // Play parameters
    // ---------------------------
    struct AudioPlayParams
    {
        float volume = 1.0f; // 0..?
        float pan = 0.0f; // -1..1
        float pitch = 1.0f; // 1=normal
        bool  loop = false;
        bool  paused = false;

        // 3D optional
        bool  is3D = false;
        Math::Vec3f pos;
        Math::Vec3f vec;
    };

}