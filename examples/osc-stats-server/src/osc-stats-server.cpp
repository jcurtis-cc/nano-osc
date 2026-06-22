#include "nanoosc/runtime_stats.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>

int main()
{
    constexpr uint16_t port = 9000;

    auto  transport          = std::make_unique<NanoOsc::RuntimeStatsUDPTransport>(port);
    auto* transport_os_drops = transport.get();

    NanoOsc::RuntimeStatsHostUdpDropProvider host_udp_drops;
    NanoOsc::RuntimeStatsOsDropProvider*     os_drop_provider = nullptr;
    if (transport_os_drops->os_drops_supported())
    {
        os_drop_provider = transport_os_drops;
    }
    else if (host_udp_drops.is_supported())
    {
        os_drop_provider = &host_udp_drops;
    }

    NanoOsc::OSCStatsServer server(std::move(transport));
    server.set_os_drop_provider(os_drop_provider);

    server.set_message_view_handler([](const NanoOsc::MessageView& msg) {
        (void)msg;
        // Normal message handling can stay allocation-free while stats are collected.
    });
    server.set_bundle_view_handler([](const NanoOsc::BundleView& bundle) {
        (void)bundle;
        // Bundle handling is counted as top-level bundle ingress by RuntimeStats.
    });

    std::cout << "listening for OSC messages with RuntimeStats on port " << port << "\n";
    if (os_drop_provider == nullptr)
    {
        std::cout << "OS drop counters unavailable on this platform/configuration\n";
    }

    auto next_report = std::chrono::steady_clock::now() + std::chrono::seconds(1);

    for (;;)
    {
        server.process_all(256);

        const auto now = std::chrono::steady_clock::now();
        if (now >= next_report)
        {
            std::cout << "\n" << NanoOsc::format_runtime_stats_snapshot(server.stats().snapshot()) << std::flush;
            next_report = now + std::chrono::seconds(1);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
