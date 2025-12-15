// AudioService_AllInOne.h (or .cpp) - C++17
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <algorithm>
#include <memory>

#include "AudioType.h"

#include "../Core/ECS/ServiceContext.hpp"

// 前方宣言
namespace SoLoud
{
    class Soloud;
    class Wav;
}


namespace SFW::Audio
{

    // ---------------------------
    // AudioService
    // ---------------------------
    class AudioService : public ECS::IUpdateService
    {
    public:
        //インライン展開されないようにcppで定義
        AudioService();
        ~AudioService();

        AudioService(const AudioService&) = delete;
        AudioService& operator=(const AudioService&) = delete;

        /**
         * @brief AudioServiceを初期化します。SoLoudエンジンをセットアップします。
         * @return bool 初期化に成功した場合はtrue、失敗した場合はfalseを返します。
         */
        bool Initialize();

        /**
         * @brief AudioServiceをシャットダウンします。すべてのサウンドを停止し、リソースを解放します。
         */
        void Shutdown();

        void Update(double deltaTime) override
        {
			// コマンドを処理
            PumpCommands();
        }

        /**
         * @brief WAVファイルのロードをキューに追加します。実際のファイルのロードはPumpCommands()スレッドで行われます。
         * @param path ロードするWAVファイルのパス
         * @return SoundHandle ロードされたサウンドのハンドル
         */
        SoundHandle EnqueueLoadWav(const std::string& path);



        /**
         * @brief ストリーミング(主に長尺BGM向け)で音声ファイルをロードするコマンドをキューに追加します。
         *        実際のロードはPumpCommands()スレッドで行われます。
         * @param path ロードする音声ファイルのパス（wav/oggなど）
         * @return SoundHandle ロードされたサウンドのハンドル
         */
        SoundHandle EnqueueLoadStream(const std::string& path);
        /**
               * @brief サウンドのアンロードをキューに追加します。実際のアンロードはPumpCommands()スレッドで行われます。
               * @param h アンロードするサウンドのハンドル
               */
        void EnqueueUnload(SoundHandle h);

        /**
         * @brief サウンドの再生をキューに追加します。実際の再生はPumpCommands()スレッドで行われます。
         * @param sound 再生するサウンドのハンドル
         * @param p 再生パラメータ
         * @return AudioTicketID 再生チケットのID
         */
        AudioTicketID EnqueuePlay(SoundHandle sound, const AudioPlayParams& p = {});

        void EnqueueStop(VoiceID v);

        void EnqueueSetVolume(VoiceID v, float volume);

        void EnqueueSetPan(VoiceID v, float pan);

        void EnqueueSetPitch(VoiceID v, float pitch);

        void EnqueueSet3D(VoiceID v, float x, float y, float z, float vx, float vy, float vz);

        /**
         * @brief リスナーの位置と向きを設定するコマンドをキューに追加します。実際の設定はPumpCommands()スレッドで行われます。
         * @param px 位置X
         * @param py 位置Y
         * @param pz 位置Z
         * @param ax 向きX
         * @param ay 向きY
         * @param az 向きZ
         * @param ux 上方向X
         * @param uy 上方向Y
         * @param uz 上方向Z
         * @param vx 前方向X
         * @param vy 前方向Y
         * @param vz 前方向Z
         */
        void EnqueueSetListener(float px, float py, float pz,
            float ax, float ay, float az,
            float ux, float uy, float uz,
            float vx, float vy, float vz);

        /**
         * @brief 指定されたオーディオチケットIDに対応するボイスIDを解決しようとします。
         * @param t 解決するオーディオチケットID
         * @return std::optional<VoiceID> チケットが有効でボイスが解決された場合はVoiceIDを含むオプションを返し、そうでない場合はstd::nulloptを返します。
         */
        std::optional<VoiceID> TryResolve(AudioTicketID t) const noexcept;

        /**
         * @brief 指定されたオーディオチケットを解放します。これにより、関連するリソースが解放されます。
         * @param t 解放するオーディオチケットID
         * @return void
         */
        void ReleaseTicket(AudioTicketID t) noexcept;

        /**
         * @brief 指定されたボイスIDが現在アクティブかどうかをオーディオスレッド上で確認します。
         * @param v チェックするボイスID
         * @return bool ボイスがアクティブな場合はtrue、そうでない場合はfalseを返します。
         */
        bool IsVoiceAlive_OnAudioThread(VoiceID v) noexcept;

        /**
         * @brief キューに溜まったオーディオコマンドを処理します。これにはサウンドのロード、再生、停止、パラメータの設定などが含まれます。
         */
        void PumpCommands();

    private:
        // ---------------------------
        // Ticket table
        // ---------------------------
        struct TicketSlot
        {
            std::atomic<uint32_t> gen{ 0 };
            std::atomic<VoiceID>  voice{ 0 }; // 0=not ready/invalid
        };

        static constexpr uint32_t kMaxTickets = 1u << 16;
        //でかすぎるのでヒープで管理
        std::vector<TicketSlot> m_ticketSlots = std::vector<TicketSlot>(kMaxTickets);
        std::atomic<uint32_t> m_ticketAlloc{ 0 };

        AudioTicketID AllocTicket() noexcept;

        // ---------------------------
        // Sound storage (Wav only)
        // If you need OGG/WavStream, use variant/unique_ptr base class.
        // ---------------------------
        struct SoundEntry;

        std::unordered_map<uint32_t, SoundEntry> m_sounds;
        std::atomic<uint32_t> m_nextSoundId{ 1 };

        // ---------------------------
        // Load cache (caller thread)
        // - 同じパスを再ロードしない
        // - EnqueueUnload() の参照カウントで実際のアンロードを制御
        // NOTE: Wav と Stream は別キーで管理 ("wav|..." / "stream|...")
        // ---------------------------
        struct CachedSound
        {
            SoundHandle handle{};
            uint32_t    refCount = 0;
        };
        mutable std::mutex m_cacheMtx;
        std::unordered_map<std::string, CachedSound> m_pathCache;
        std::unordered_map<uint32_t, std::string> m_idToCacheKey;

        // ---------------------------
        // Command queue
        // ---------------------------
        enum class CmdType : uint8_t
        {
            LoadWav,
            LoadStream,
            Unload,
            Play,
            Stop,
            SetVolume,
            SetPan,
            SetPitch,
            Set3D,
            SetListener,
            Shutdown
        };

        struct Cmd
        {
            CmdType type{ CmdType::Play };

            // For Play publishing
            AudioTicketID ticket{ AudioTicketID::Invalid() };

            // For asset
            SoundHandle sound{};
            std::string path;

            // For play
            AudioPlayParams play{};

            // For voice control
            VoiceID voice = 0;

            // generic floats (set*)
            float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, h = 0, i = 0, j = 0, k = 0, l = 0;

            static Cmd MakeLoadWav(SoundHandle h, const std::string& p)
            {
                Cmd c;
                c.type = CmdType::LoadWav;
                c.sound = h;
                c.path = p;
                return c;
            }



            static Cmd MakeLoadStream(SoundHandle h, const std::string& p)
            {
                Cmd c;
                c.type = CmdType::LoadStream;
                c.sound = h;
                c.path = p;
                return c;
            }
            static Cmd MakeUnload(SoundHandle h)
            {
                Cmd c;
                c.type = CmdType::Unload;
                c.sound = h;
                return c;
            }

            static Cmd MakePlay(AudioTicketID t, SoundHandle h, const AudioPlayParams& p)
            {
                Cmd c;
                c.type = CmdType::Play;
                c.ticket = t;
                c.sound = h;
                c.play = p;
                return c;
            }

            static Cmd MakeStop(VoiceID v)
            {
                Cmd c;
                c.type = CmdType::Stop;
                c.voice = v;
                return c;
            }

            static Cmd MakeSet1(CmdType t, VoiceID v, float x)
            {
                Cmd c;
                c.type = t;
                c.voice = v;
                c.a = x;
                return c;
            }

            static Cmd MakeShutdown()
            {
                Cmd c;
                c.type = CmdType::Shutdown;
                return c;
            }
        };

        void Push(Cmd&& c)
        {
            std::lock_guard<std::mutex> lk(m_cmdMtx);
            m_queue.emplace_back(std::move(c));
        }

    private:
        //cppでヘッダーをインクルードするためにポインタを使用
        std::unique_ptr<SoLoud::Soloud> m_soloud{};

        // command queue
        std::mutex m_cmdMtx;
        std::vector<Cmd> m_queue;
        std::vector<Cmd> m_swap;

        // init guard
        mutable std::mutex m_initMtx;
        bool m_initialized = false;
    public:
        STATIC_SERVICE_TAG
        DEFINE_UPDATESERVICE_GROUP(GROUP_AUDIO)
    };

}
