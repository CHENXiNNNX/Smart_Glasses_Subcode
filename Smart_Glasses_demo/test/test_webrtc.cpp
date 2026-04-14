#include "app/protocol/webrtc/webrtc_device_runner.hpp"
#include "app/tool/log/log.hpp"

#include <iostream>

using namespace app::tool::log;

namespace
{
    constexpr const char* LOG_TAG = "MAIN";
} // namespace

int main()
{
    Logger::inst().init(LogConfig());
    app::protocol::webrtc::WebRtcDeviceOptions opt;
    opt.log_tag               = LOG_TAG;
    opt.audio_playback_volume = 70;
    return app::protocol::webrtc::run_webrtc_device_interactive(std::move(opt), std::cin);
}
