#include "app/app.hpp"
#include "app/tool/log/log.hpp"

#include <csignal>

namespace
{
    constexpr const char* LOG_TAG = "MAIN";
    app::App*             g_app   = nullptr;

    void signal_handler(int sig)
    {
        (void)sig;
        if (g_app)
        {
            g_app->stop();
        }
    }
} // namespace

int main()
{
    app::App app;
    g_app = &app;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (!app.init())
    {
        LOG_ERROR(LOG_TAG, "初始化失败");
        return 1;
    }

    app.run();
    g_app = nullptr;

    LOG_INFO(LOG_TAG, "退出");
    return 0;
}
