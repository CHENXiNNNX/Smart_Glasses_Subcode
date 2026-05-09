/* audio_proc.hpp - 应用层 3A（Speex；采集无内置 VQE 时启用） */

#pragma once

#include "../core/types.hpp"
#include <memory>

namespace app::media::audio
{

    class AudioProc
    {
    public:
        AudioProc();
        ~AudioProc();

        AudioProc(const AudioProc&)            = delete;
        AudioProc& operator=(const AudioProc&) = delete;

        Error init(const ProcCfg& cfg, uint32_t rate, uint8_t frame_ms);
        void  deinit();
        bool  is_init() const;

        void process(int16_t* pcm, size_t samples);

        void set_denoise(bool en);
        void set_agc(bool en);
        void set_vad(bool en);
        void set_dereverb(bool en);
        void set_agc_level(float level);
        void set_noise_suppress(int db);
        void set_echo_suppress(int db);

        bool denoise() const;
        bool agc() const;
        bool vad() const;
        bool dereverb() const;

        const ProcCfg& cfg() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace app::media::audio
