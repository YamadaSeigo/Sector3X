#include "Audio/AudioService.h"

#include <SoLoud/include/soloud.h>
#include <SoLoud/include/soloud_wav.h>
#include <SoLoud/include/soloud_wavstream.h>

namespace SFW::Audio
{
	struct AudioService::SoundEntry
	{
		enum class Kind : uint8_t { Wav, Stream };

		uint32_t refCount = 0;
		Kind kind = Kind::Wav;
		std::unique_ptr<SoLoud::AudioSource> src;
		std::string path;
	};

	bool AudioService::Initialize()
	{
		std::lock_guard<std::mutex> lk(m_initMtx);

		// Create SoLoud engine
		m_soloud = std::make_unique<SoLoud::Soloud>();

		if (m_initialized) return true;
		const int r = m_soloud->init();
		m_initialized = (r == 0);
		return m_initialized;
	}

	AudioService::AudioService() = default;

	AudioService::~AudioService()
	{
		Shutdown();
	}

	void AudioService::Shutdown()
	{
		std::lock_guard<std::mutex> lk(m_initMtx);
		if (!m_initialized) return;

		// stop everything on SoLoud thread by pushing a command
		Push(Cmd::MakeShutdown());
		PumpCommands(); // deinit in Pump

		m_initialized = false;
	}

	[[nodiscard]] SoundHandle AudioService::EnqueueLoadWav(const std::string& path)
	{
		const std::string key = std::string("wav|") + path;

		{
			std::lock_guard<std::mutex> lk(m_cacheMtx);
			auto it = m_pathCache.find(key);
			if (it != m_pathCache.end())
			{
				it->second.refCount++;
				return it->second.handle;
			}

			const uint32_t id = m_nextSoundId.fetch_add(1, std::memory_order_relaxed);
			SoundHandle h{ id };
			m_pathCache.emplace(key, CachedSound{ h, 1u });
			m_idToCacheKey.emplace(id, key);

			Cmd c = Cmd::MakeLoadWav(h, path);
			Push(std::move(c));
			return h;
		}
	}

	[[nodiscard]] SoundHandle AudioService::EnqueueLoadStream(const std::string& path)
	{
		const std::string key = std::string("stream|") + path;

		{
			std::lock_guard<std::mutex> lk(m_cacheMtx);
			auto it = m_pathCache.find(key);
			if (it != m_pathCache.end())
			{
				it->second.refCount++;
				return it->second.handle;
			}

			const uint32_t id = m_nextSoundId.fetch_add(1, std::memory_order_relaxed);
			SoundHandle h{ id };
			m_pathCache.emplace(key, CachedSound{ h, 1u });
			m_idToCacheKey.emplace(id, key);

			Cmd c = Cmd::MakeLoadStream(h, path);
			Push(std::move(c));
			return h;
		}
	}

	void AudioService::EnqueueUnload(SoundHandle h)
	{
		if (!h) return;

		bool doUnload = false;
		{
			std::lock_guard<std::mutex> lk(m_cacheMtx);
			auto itKey = m_idToCacheKey.find(h.id);
			if (itKey == m_idToCacheKey.end())
			{
				// 未キャッシュ: そのままアンロード要求（安全側）
				doUnload = true;
			}
			else
			{
				auto it = m_pathCache.find(itKey->second);
				if (it == m_pathCache.end())
				{
					doUnload = true;
				}
				else
				{
					if (it->second.refCount > 0) it->second.refCount--;
					if (it->second.refCount == 0)
					{
						m_pathCache.erase(it);
						m_idToCacheKey.erase(itKey);
						doUnload = true;
					}
				}
			}
		}

		if (doUnload)
			Push(Cmd::MakeUnload(h));
	}
	[[nodiscard]] AudioTicketID AudioService::EnqueuePlay(SoundHandle sound, const AudioPlayParams& p)
	{
		AudioTicketID t = AllocTicket();
		Cmd c = Cmd::MakePlay(t, sound, p);
		Push(std::move(c));
		return t;
	}
	void AudioService::EnqueueStop(VoiceID v)
	{
		if (v == 0) return;
		Push(Cmd::MakeStop(v));
	}
	void AudioService::EnqueueSetVolume(VoiceID v, float volume)
	{
		if (v == 0) return;
		Push(Cmd::MakeSet1(CmdType::SetVolume, v, volume));
	}
	void AudioService::EnqueueSetPan(VoiceID v, float pan)
	{
		if (v == 0) return;
		Push(Cmd::MakeSet1(CmdType::SetPan, v, pan));
	}
	void AudioService::EnqueueSetPitch(VoiceID v, float pitch)
	{
		if (v == 0) return;
		Push(Cmd::MakeSet1(CmdType::SetPitch, v, pitch));
	}
	void AudioService::EnqueueSet3D(VoiceID v, float x, float y, float z, float vx, float vy, float vz)
	{
		if (v == 0) return;
		Cmd c;
		c.type = CmdType::Set3D;
		c.voice = v;
		c.a = x; c.b = y; c.c = z;
		c.d = vx; c.e = vy; c.f = vz;
		Push(std::move(c));
	}
	void AudioService::EnqueueSetListener(float px, float py, float pz, float ax, float ay, float az, float ux, float uy, float uz, float vx, float vy, float vz)
	{
		Cmd c;
		c.type = CmdType::SetListener;
		c.a = px; c.b = py; c.c = pz;
		c.d = ax; c.e = ay; c.f = az;
		c.g = ux; c.h = uy; c.i = uz;
		c.j = vx; c.k = vy; c.l = vz;
		Push(std::move(c));
	}
	std::optional<VoiceID> AudioService::TryResolve(AudioTicketID t) const noexcept
	{
		if (!t.IsValid() || t.index >= kMaxTickets) return std::nullopt;
		if (m_ticketSlots[t.index].gen.load(std::memory_order_acquire) != t.generation) return std::nullopt;

		const VoiceID v = m_ticketSlots[t.index].voice.load(std::memory_order_acquire);
		if (v == 0) return std::nullopt; // not ready yet
		return v;
	}
	void AudioService::ReleaseTicket(AudioTicketID t) noexcept
	{
		if (!t.IsValid() || t.index >= kMaxTickets) return;
		if (m_ticketSlots[t.index].gen.load(std::memory_order_acquire) != t.generation) return;

		const VoiceID v = m_ticketSlots[t.index].voice.load(std::memory_order_acquire);
		if (v != 0)
			EnqueueStop(v);

		m_ticketSlots[t.index].voice.store(0, std::memory_order_release);
		// generation is NOT changed here; it will be bumped on next AllocTicket() for this slot
	}
	bool AudioService::IsVoiceAlive_OnAudioThread(VoiceID v) noexcept
	{
		if (v == 0) return false;
		return m_soloud->isValidVoiceHandle((SoLoud::handle)v);
	}
	void AudioService::PumpCommands()
	{
		// swap queue to reduce lock duration
		{
			std::lock_guard<std::mutex> lk(m_cmdMtx);
			m_swap.swap(m_queue);
		}

		for (Cmd& cmd : m_swap)
		{
			switch (cmd.type)
			{
			case CmdType::LoadWav:
			{
				// Create or refcount
				auto& entry = m_sounds[cmd.sound.id];
				entry.refCount = (entry.refCount == 0) ? 1 : (entry.refCount + 1);

				// (re)create source if needed
				if (!entry.src || entry.kind != SoundEntry::Kind::Wav || entry.path != cmd.path)
				{
					entry.kind = SoundEntry::Kind::Wav;
					entry.path = cmd.path;

					auto wav = std::make_unique<SoLoud::Wav>();
					wav->load(cmd.path.c_str());
					entry.src = std::move(wav);
				}
				break;
			}
			case CmdType::LoadStream:
			{
				// Create or refcount
				auto& entry = m_sounds[cmd.sound.id];
				entry.refCount = (entry.refCount == 0) ? 1 : (entry.refCount + 1);

				// (re)create source if needed
				if (!entry.src || entry.kind != SoundEntry::Kind::Stream || entry.path != cmd.path)
				{
					entry.kind = SoundEntry::Kind::Stream;
					entry.path = cmd.path;

					auto st = std::make_unique<SoLoud::WavStream>();
					st->load(cmd.path.c_str()); // streaming load
					entry.src = std::move(st);
				}
				break;
			}

			case CmdType::Unload:
			{
				auto it = m_sounds.find(cmd.sound.id);
				if (it != m_sounds.end())
				{
					if (it->second.refCount > 0) it->second.refCount--;
					if (it->second.refCount == 0)
						m_sounds.erase(it);
				}
				break;
			}
			case CmdType::Play:
			{
				auto it = m_sounds.find(cmd.sound.id);
				if (it == m_sounds.end()) break;
				if (!it->second.src) break;

				SoLoud::AudioSource& src = *it->second.src;
				src.setLooping(cmd.play.loop);

				SoLoud::handle h = 0;
				if (!cmd.play.is3D)
				{
					h = m_soloud->play(src, cmd.play.volume, cmd.play.pan, cmd.play.paused);
				}
				else
				{
					h = m_soloud->play3d(
						src,
						cmd.play.pos.x, cmd.play.pos.y, cmd.play.pos.z,
						cmd.play.vec.x, cmd.play.vec.y, cmd.play.vec.z,
						cmd.play.volume,
						cmd.play.paused
					);
				}

				if (h != 0)
				{
					m_soloud->setRelativePlaySpeed(h, cmd.play.pitch);
				}

				// publish voice to ticket slot ONLY if same generation
				const uint32_t idx = cmd.ticket.index;
				const uint32_t gen = cmd.ticket.generation;
				if (idx < kMaxTickets &&
					m_ticketSlots[idx].gen.load(std::memory_order_acquire) == gen)
				{
					m_ticketSlots[idx].voice.store((VoiceID)h, std::memory_order_release);
				}
				break;
			}

			case CmdType::Stop:
				m_soloud->stop((SoLoud::handle)cmd.voice);
				break;

			case CmdType::SetVolume:
				m_soloud->setVolume((SoLoud::handle)cmd.voice, cmd.a);
				break;

			case CmdType::SetPan:
				m_soloud->setPan((SoLoud::handle)cmd.voice, cmd.a);
				break;

			case CmdType::SetPitch:
				m_soloud->setRelativePlaySpeed((SoLoud::handle)cmd.voice, cmd.a);
				break;

			case CmdType::Set3D:
				m_soloud->set3dSourcePosition((SoLoud::handle)cmd.voice, cmd.a, cmd.b, cmd.c);
				m_soloud->set3dSourceVelocity((SoLoud::handle)cmd.voice, cmd.d, cmd.e, cmd.f);
				break;

			case CmdType::SetListener:
				m_soloud->set3dListenerParameters(
					cmd.a, cmd.b, cmd.c,  // pos
					cmd.d, cmd.e, cmd.f,  // at
					cmd.g, cmd.h, cmd.i,  // up
					cmd.j, cmd.k, cmd.l   // vel
				);
				break;

			case CmdType::Shutdown:
				// stop and deinit
				m_soloud->deinit();
				// best-effort cleanup
				m_sounds.clear();
				{
					std::lock_guard<std::mutex> lk(m_cacheMtx);
					m_pathCache.clear();
					m_idToCacheKey.clear();
				}
				// invalidate published voices
				for (auto& s : m_ticketSlots)
					s.voice.store(0, std::memory_order_release);
				break;
			}
		}

		m_swap.clear();

		// 3D update at end (cheap)
		m_soloud->update3dAudio();
	}
	AudioTicketID AudioService::AllocTicket() noexcept
	{
		// simple ring allocator (no free list)
		const uint32_t idx = m_ticketAlloc.fetch_add(1, std::memory_order_relaxed) % kMaxTickets;
		const uint32_t gen = m_ticketSlots[idx].gen.fetch_add(1, std::memory_order_relaxed) + 1;

		m_ticketSlots[idx].voice.store(0, std::memory_order_release);
		return AudioTicketID{ idx, gen };
	}
}