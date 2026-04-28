#include <iostream>
#include <memory>
#include <ctime>
#include <iomanip>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <QCoreApplication>
#include <QObject>
#include <QTimer>
#include <QFile>

#include "timeshiftx/catchup_engine.hpp"
#include "timeshiftx/inetwork_plugin.hpp"
#include "timeshiftx/m3u_parser.hpp"
#include "timeshiftx/network_plugin_manager.hpp"
#include "timeshiftx/playback_facade.hpp"
#include "timeshiftx/types.hpp"
#include "timeshiftx/xtream_codes_parser.hpp"
#include "timeshiftx/epg_manager.hpp"

class M3UParseDemo : public QObject {
    Q_OBJECT
public:
    explicit M3UParseDemo(QCoreApplication& app, QObject* parent = nullptr)
        : QObject(parent), app_(app) {}

    void start() {
        std::cout << "[Qt Demo] 启动 M3U 解析示例...\n";
        std::cout << "[Qt Demo] 使用事件循环进行异步处理\n";
        std::cout << "[Qt Demo] 使用网络链接: https://gh-proxy.com/raw.githubusercontent.com/suxuang/myIPTV/main/ipv4.m3u\n";

        // 获取网络插件
        auto& plugin_mgr = timeshiftx::NetworkPluginManager::instance();
        std::cout << "[Qt Demo] 当前插件: " << plugin_mgr.preferredPluginName() << "\n";

        // 使用 Qt 事件循环异步执行解析任务
        QTimer::singleShot(100, this, &M3UParseDemo::runParseTask);
    }

private slots:
    void runParseTask() {
        std::cout << "\n========== M3U 解析开始 ==========\n";

        // 执行 M3U 解析
        timeshiftx::M3UParser m3u;
        timeshiftx::Error m3u_rc = m3u.parseFromUrl(
            "https://gh-proxy.com/raw.githubusercontent.com/suxuang/myIPTV/main/ipv4.m3u",
            10,  // timeout=10s
            nullptr  // 使用默认的网络插件
        );

        std::cout << "[M3U 解析结果]\n";
        std::cout << "  错误代码: " << static_cast<int>(m3u_rc.code) << "\n";
        std::cout << "  错误信息: " << m3u_rc.message << "\n";

        const auto channels = m3u.getChannels();
        std::string epg_url = ""; // m3u.getEpgUrl();
    
        timeshiftx::EPGManager epg;
        if (!epg_url.empty()) {
            auto epg_rc = epg.loadFromUrl(epg_url, 15);
            if (!epg_rc.ok()) {
                std::cerr << "EPG load failed: " << epg_rc.message << std::endl;
            } else {
                std::cout << "EPG loaded successfully" << std::endl;
            }
        } else {
            QFile file("sample_epg.xml");
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QString xml_content = file.readAll();
                auto epg_rc = epg.loadXMLTV(xml_content.toStdString());
                if (!epg_rc.ok()) {
                    std::cerr << "EPG load from XML failed: " << epg_rc.message << std::endl;
                } else {
                    std::cout << "EPG loaded successfully from XML" << std::endl;
                }
            } else {
                std::cerr << "Failed to open local EPG XML file" << std::endl;
            }
        }

        std::unordered_map<std::string, std::string> channel_epg_map; // channel.internal_id -> epg_id
        for (const auto& ch : channels) {
            std::string epg_id = epg.resolveStrictEpgId(ch);
            if (epg_id.empty()) 
                epg_id = epg.fuzzyMatchChannelName(ch.epg_match_id);

            if (!epg_id.empty()) {
                channel_epg_map[ch.internal_id] = epg_id;
                std::cout << "Channel " << ch.name << " mapped to EPG ID " << epg_id << std::endl;
            }
        }

        std::cout << "  解析频道数: " << channels.size() << "\n\n";

        if (channels.empty()) {
            std::cout << "[警告] 没有解析到任何频道\n";
            QTimer::singleShot(0, this, &M3UParseDemo::finish);
            return;
        }

        // 打印前几个频道的详细信息
        std::cout << "========== 频道信息示例 ==========\n";
        size_t max_display = channels.size();
        for (size_t i = 0; i < max_display; ++i) {
            const auto& ch = channels[i];
            std::cout << "\n[频道 " << (i + 1) << "]\n";
            std::cout << "  频道名: " << ch.name << "\n";
            std::cout << "  内部 ID: " << ch.internal_id << "\n";
            std::cout << "  分组: " << ch.group_name << "\n";
            std::cout << "  台标 URL: " << ch.logo_url << "\n";
            std::cout << "  EPG 匹配 ID: " << ch.epg_match_id << "\n";
            std::cout << "  直播链接: " << ch.live_url << "\n";
            std::cout << "  源类型: " << (ch.source_type == timeshiftx::Channel::SourceType::M3U ? "M3U" : "XTREAM_CODES") << "\n";
            std::cout << "  支持回看（声明）: " << (ch.catchup_declared ? "是" : "否") << "\n";
            if (ch.catchup_declared) {
                std::cout << "  回看天数: " << ch.catchup_days << "\n";
            }

            if (!ch.catchup_declared) {
                std::cout << "  [提示] 该频道未声明支持回看，回看 URL 将无法生成\n";
                continue;
            }

            std::cout << "\n========== 回看 URL 生成演示 ==========\n";

            // 选择第一个频道进行回看演示
            std::cout << "[频道] " << ch.name << " 支持回看\n";

            // 获取对应的 EPG 时间线
            std::vector<timeshiftx::Programme> timeline;
            auto it = channel_epg_map.find(ch.internal_id);
            if (it != channel_epg_map.end()) {
                timeline = epg.getTimelineForChannel(it->second, std::time(nullptr));
                std::cout << "  获取到 EPG 时间线，节目数: " << timeline.size() << "\n";
            } else {
                std::cout << "  没有找到对应的 EPG ID，无法获取时间线\n";
            }

            for (const auto& prog : timeline) {
                std::cout << "    节目: " << prog.title << " (";
                std::cout << std::put_time(std::localtime(&prog.start_time), "%c") << " - ";
                std::cout << std::put_time(std::localtime(&prog.end_time), "%c") << ")\n";
            
                // 生成回看 URL
                auto decision = timeshiftx::PlaybackFacade::resolveProgrammePlayback(const_cast<timeshiftx::Channel&>(ch), prog, {}, false);
                std::cout << "  播放模式: " << (decision.mode == timeshiftx::PlaybackMode::CATCHUP ? "CATCHUP" : "LIVE") << "\n";
                std::cout << "  生成的回看 URL:\n    " << decision.url << "\n";

                // 探测回看 URL 的可用性
                std::cout << "\n  探测回看 URL 可用性...\n";
                timeshiftx::Error probe_rc = timeshiftx::CatchupEngine::probeAvailability(decision.url, 5, 1,  // timeout=5s 1,  // max_retries=1
                    nullptr
                );
            
                std::cout << "  探测结果: " << static_cast<int>(probe_rc.code) << " - " << probe_rc.message << "\n";
            }

        }


        std::cout << "\n========== 解析完成 ==========\n";

        // 使用事件循环继续执行，然后退出
        QTimer::singleShot(0, this, &M3UParseDemo::finish);
    }

    void finish() {
        std::cout << "[Qt Demo] 完成所有解析任务\n";
        app_.quit();
    }

private:
    QCoreApplication& app_;
};

#include "qt_demo.moc"

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

    QCoreApplication app(argc, argv);

    std::cout << "========================================\n";
    std::cout << "  TimeShiftX Qt 事件循环 M3U 解析示例\n";
    std::cout << "========================================\n\n";

    M3UParseDemo demo(app);
    demo.start();

    return app.exec();
}
