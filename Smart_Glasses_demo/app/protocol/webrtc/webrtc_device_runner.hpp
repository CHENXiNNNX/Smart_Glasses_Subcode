#pragma once

#include <istream>
#include <string>
#include <vector>

namespace app::protocol::webrtc
{
    struct WebRtcDeviceOptions
    {
        std::string              device_id;
        std::string              server_url;
        int                      audio_playback_volume = 50;
        std::vector<std::string> ice_stun;
        std::vector<std::string> ice_turn;
        const char*              log_tag = nullptr;
        std::string              boot_log_line;
        std::string              end_log_line;
        std::string              command_hint;
    };

    void apply_default_webrtc_device_options(WebRtcDeviceOptions& opt);
    int  run_webrtc_device_interactive(WebRtcDeviceOptions opt, std::istream& in);
} // namespace app::protocol::webrtc
