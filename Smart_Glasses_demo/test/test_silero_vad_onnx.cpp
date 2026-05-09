/* test_silero_vad_onnx.cpp - Silero VAD ONNX 实时采集测试 */

#include "app/media/audio/audio.hpp"
#include "app/tool/log/log.hpp"

#include <atomic>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "onnxruntime_cxx_api.h"

using namespace app::media::audio;
using namespace app::tool::log;

namespace
{
    constexpr const char* TAG = "SILERO_VAD";

    constexpr uint32_t    CAPTURE_RATE        = 48000;
    constexpr uint8_t     CAPTURE_CHANNELS    = 1;
    constexpr uint8_t     CAPTURE_FRAME_MS    = 20;
    constexpr uint32_t    MODEL_RATE          = 16000;
    constexpr size_t      MODEL_SAMPLES       = 512;
    constexpr int64_t     HIDDEN_DIMS[3]      = {2, 1, 64};
    constexpr int64_t     AUDIO_DIMS[2]       = {1, static_cast<int64_t>(MODEL_SAMPLES)};
    constexpr int64_t     SR_DIMS[1]          = {1};
    constexpr int64_t     SR_VALUE[1]         = {MODEL_RATE};
    constexpr float       DEFAULT_THRESHOLD   = 0.5f;
    constexpr float       MIN_VALID_THRESHOLD = 0.0f;
    constexpr float       MAX_VALID_THRESHOLD = 1.0f;
    constexpr const char* SEP_LINE = "------------------------------------------------------------";

    std::atomic<bool> g_running{true};

    void onSignal(int)
    {
        g_running.store(false, std::memory_order_relaxed);
    }

    void logSection(const char* title)
    {
        LOG_INFO(TAG, "%s", SEP_LINE);
        LOG_INFO(TAG, "%s", title);
        LOG_INFO(TAG, "%s", SEP_LINE);
    }

    struct VadStats
    {
        std::atomic<uint32_t> frame_infer_count{0};
        std::atomic<uint32_t> speech_count{0};
        std::atomic<double>   total_infer_ms{0.0};
        std::atomic<double>   max_infer_ms{0.0};
        std::atomic<bool>     current_speech{false};
        std::atomic<float>    last_prob{0.0f};
    };

    class SileroVad
    {
    public:
        SileroVad(const std::string& model_path, float threshold)
            : threshold_(threshold), env_(ORT_LOGGING_LEVEL_WARNING, "test-silero-vad"),
              session_options_(), allocator_()
        {
            session_options_.SetIntraOpNumThreads(1);
            session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
            session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), session_options_);

            logSection("模型初始化");
            LOG_INFO(TAG, "模型路径        : %s", model_path.c_str());
            LOG_INFO(TAG, "阈值 threshold  : %.3f", threshold_);
        }

        bool infer(const int16_t* pcm_48k, size_t samples_48k, float& prob, double& infer_ms)
        {
            if (!pcm_48k || samples_48k == 0)
            {
                return false;
            }

            appendDownsampled16k(pcm_48k, samples_48k);
            if (ring_16k_.size() < MODEL_SAMPLES)
            {
                return false;
            }

            std::array<float, MODEL_SAMPLES> input_audio{};
            for (size_t i = 0; i < MODEL_SAMPLES; ++i)
            {
                input_audio[i] = ring_16k_[i];
            }
            ring_16k_.erase(ring_16k_.begin(), ring_16k_.begin() + MODEL_SAMPLES);

            Ort::MemoryInfo memory_info =
                Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

            Ort::Value x_tensor = Ort::Value::CreateTensor<float>(
                memory_info, input_audio.data(), input_audio.size(), AUDIO_DIMS, 2);
            Ort::Value h_tensor  = Ort::Value::CreateTensor<float>(memory_info, h_state_.data(),
                                                                  h_state_.size(), HIDDEN_DIMS, 3);
            Ort::Value c_tensor  = Ort::Value::CreateTensor<float>(memory_info, c_state_.data(),
                                                                  c_state_.size(), HIDDEN_DIMS, 3);
            Ort::Value sr_tensor = Ort::Value::CreateTensor<int64_t>(
                memory_info, const_cast<int64_t*>(SR_VALUE), 1, SR_DIMS, 1);

            std::array<const char*, 3> input_names   = {"x", "h", "c"};
            std::array<const char*, 3> output_names  = {"prob", "new_h", "new_c"};
            std::array<Ort::Value, 3>  input_tensors = {std::move(x_tensor), std::move(h_tensor),
                                                        std::move(c_tensor)};

            auto t0 = std::chrono::steady_clock::now();
            auto outputs =
                session_->Run(Ort::RunOptions{nullptr}, input_names.data(), input_tensors.data(),
                              input_tensors.size(), output_names.data(), output_names.size());
            auto t1 = std::chrono::steady_clock::now();
            infer_ms =
                std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t1 - t0)
                    .count();

            float* prob_data = outputs[0].GetTensorMutableData<float>();
            prob             = prob_data ? prob_data[0] : 0.0f;

            float* new_h = outputs[1].GetTensorMutableData<float>();
            float* new_c = outputs[2].GetTensorMutableData<float>();
            if (new_h)
            {
                std::memcpy(h_state_.data(), new_h, h_state_.size() * sizeof(float));
            }
            if (new_c)
            {
                std::memcpy(c_state_.data(), new_c, c_state_.size() * sizeof(float));
            }

            return true;
        }

        bool isSpeech(float prob) const
        {
            return prob >= threshold_;
        }

    private:
        void appendDownsampled16k(const int16_t* pcm_48k, size_t samples_48k)
        {
            // 48k -> 16k 简单抽取，每3个样本取1个。测试用途足够。
            for (size_t i = 0; i < samples_48k; i += 3)
            {
                float normalized = static_cast<float>(pcm_48k[i]) / 32768.0f;
                ring_16k_.push_back(normalized);
            }
        }

    private:
        float threshold_;

        Ort::Env                         env_;
        Ort::SessionOptions              session_options_;
        Ort::AllocatorWithDefaultOptions allocator_;
        std::unique_ptr<Ort::Session>    session_;
        std::array<float, 2 * 1 * 64>    h_state_{};
        std::array<float, 2 * 1 * 64>    c_state_{};
        std::vector<float>               ring_16k_;
    };

    void printUsage(const char* prog)
    {
        std::cout << "用法: " << prog << " [-m 模型路径] [-t 阈值]\n";
        std::cout << "示例: " << prog << " -m /root/bin/assets/models/silero_vad.onnx -t 0.60\n";
    }

} // namespace

int main(int argc, char* argv[])
{
    std::string model_path = "/root/bin/assets/models/silero_vad.onnx";
    float       threshold  = DEFAULT_THRESHOLD;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if ((arg == "-m" || arg == "--model") && (i + 1) < argc)
        {
            model_path = argv[++i];
        }
        else if ((arg == "-t" || arg == "--threshold") && (i + 1) < argc)
        {
            threshold = static_cast<float>(std::atof(argv[++i]));
        }
        else if (arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            return 0;
        }
        else
        {
            std::cerr << "未知参数: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    if (threshold < MIN_VALID_THRESHOLD || threshold > MAX_VALID_THRESHOLD)
    {
        std::cerr << "阈值范围必须在 [0.0, 1.0]，当前: " << threshold << std::endl;
        return 1;
    }

    LogConfig log_cfg;
    log_cfg.min_level    = LogLevel::INFO;
    log_cfg.enable_color = true;
    Logger::inst().init(log_cfg);

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    AudioCfg cfg;
    cfg.capture.rate     = CAPTURE_RATE;
    cfg.capture.channels = CAPTURE_CHANNELS;
    cfg.capture.frame_ms = CAPTURE_FRAME_MS;
    cfg.enable_capture   = true;
    cfg.enable_playback  = false;
    cfg.enable_opus      = false;
    cfg.enable_proc      = false;

    AudioDrv sys;
    if (sys.init(cfg) != Error::OK)
    {
        LOG_ERROR(TAG, "音频初始化失败");
        return 1;
    }

    SileroVad vad(model_path, threshold);
    VadStats  stats;
    bool      last_speech_state = false;

    SubHandle sub = sys.subscribe(
        StreamId::MicProcessed,
        [&](const FramePtr& pcm)
        {
            if (!pcm || !pcm->data || pcm->samples == 0)
            {
                return;
            }

            float  prob     = 0.0f;
            double infer_ms = 0.0;
            bool   inferred =
                vad.infer(pcm->get<int16_t>(), static_cast<size_t>(pcm->samples), prob, infer_ms);
            if (!inferred)
            {
                return;
            }

            bool speech = vad.isSpeech(prob);
            stats.frame_infer_count.fetch_add(1, std::memory_order_relaxed);
            if (speech)
            {
                stats.speech_count.fetch_add(1, std::memory_order_relaxed);
            }
            stats.current_speech.store(speech, std::memory_order_relaxed);
            stats.last_prob.store(prob, std::memory_order_relaxed);
            double old_total = stats.total_infer_ms.load(std::memory_order_relaxed);
            while (!stats.total_infer_ms.compare_exchange_weak(old_total, old_total + infer_ms,
                                                               std::memory_order_relaxed))
            {
            }

            double current_max = stats.max_infer_ms.load(std::memory_order_relaxed);
            while (infer_ms > current_max && !stats.max_infer_ms.compare_exchange_weak(
                                                 current_max, infer_ms, std::memory_order_relaxed))
            {
            }

            if (speech != last_speech_state)
            {
                logSection("状态切换");
                LOG_INFO(TAG, "当前状态        : %s", speech ? "检测到人声" : "静音");
                LOG_INFO(TAG, "当前概率 prob   : %.6f", prob);
                LOG_INFO(TAG, "单次耗时 infer  : %.3f ms", infer_ms);
                last_speech_state = speech;
            }
        });

    if (sys.start() != Error::OK)
    {
        LOG_ERROR(TAG, "音频启动失败");
        sys.deinit();
        return 1;
    }

    logSection("实时检测启动");
    LOG_INFO(TAG, "运行提示        : 按 Ctrl+C 结束");
    LOG_INFO(TAG, "模型路径        : %s", model_path.c_str());
    LOG_INFO(TAG, "阈值 threshold  : %.3f", threshold);

    int      elapsed_sec     = 0;
    uint32_t last_infer_cnt  = 0;
    uint32_t last_speech_cnt = 0;
    while (g_running.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        elapsed_sec++;

        uint32_t infer_cnt  = stats.frame_infer_count.load(std::memory_order_relaxed);
        uint32_t speech_cnt = stats.speech_count.load(std::memory_order_relaxed);
        double   total_ms   = stats.total_infer_ms.load(std::memory_order_relaxed);
        double   max_ms     = stats.max_infer_ms.load(std::memory_order_relaxed);
        double   avg_ms     = infer_cnt > 0 ? (total_ms / infer_cnt) : 0.0;
        float    last_prob  = stats.last_prob.load(std::memory_order_relaxed);
        bool     speech_now = stats.current_speech.load(std::memory_order_relaxed);

        uint32_t sec_infer  = infer_cnt >= last_infer_cnt ? (infer_cnt - last_infer_cnt) : 0;
        uint32_t sec_speech = speech_cnt >= last_speech_cnt ? (speech_cnt - last_speech_cnt) : 0;
        double   sec_ratio  = sec_infer > 0
                                  ? (static_cast<double>(sec_speech) / static_cast<double>(sec_infer))
                                  : 0.0;
        last_infer_cnt      = infer_cnt;
        last_speech_cnt     = speech_cnt;

        LOG_INFO(TAG, "%s", SEP_LINE);
        LOG_INFO(TAG, "时间            : %d s", elapsed_sec);
        LOG_INFO(TAG, "累计推理帧数    : %u", static_cast<unsigned>(infer_cnt));
        LOG_INFO(TAG, "累计人声帧数    : %u", static_cast<unsigned>(speech_cnt));
        LOG_INFO(TAG, "本秒人声占比    : %.6f", sec_ratio);
        LOG_INFO(TAG, "平均耗时        : %.3f ms", avg_ms);
        LOG_INFO(TAG, "最大耗时        : %.3f ms", max_ms);
        LOG_INFO(TAG, "最新概率 prob   : %.6f", static_cast<double>(last_prob));
        LOG_INFO(TAG, "当前判定        : %s", speech_now ? "人声" : "非人声");
    }

    sys.unsubscribe(sub);
    sys.stop();
    sys.deinit();

    uint32_t infer_cnt  = stats.frame_infer_count.load(std::memory_order_relaxed);
    uint32_t speech_cnt = stats.speech_count.load(std::memory_order_relaxed);
    double   total_ms   = stats.total_infer_ms.load(std::memory_order_relaxed);
    double   max_ms     = stats.max_infer_ms.load(std::memory_order_relaxed);
    double   avg_ms     = infer_cnt > 0 ? (total_ms / infer_cnt) : 0.0;

    logSection("结束统计");
    LOG_INFO(TAG, "推理帧数        : %u", static_cast<unsigned>(infer_cnt));
    LOG_INFO(TAG, "人声帧数        : %u", static_cast<unsigned>(speech_cnt));
    LOG_INFO(TAG, "平均耗时        : %.3f ms", avg_ms);
    LOG_INFO(TAG, "最大耗时        : %.3f ms", max_ms);

    return 0;
}
