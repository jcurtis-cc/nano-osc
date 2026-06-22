#ifndef NANOOSC_RUNTIME_STATS_HPP
#define NANOOSC_RUNTIME_STATS_HPP

#include "nanoosc/runtime.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace NanoOsc {

enum class RuntimeStatsDropReason
{
    DecodeFailed,
    ValidationFailed,
    AssignmentFailed,
};

enum class RuntimeStatsOsDropScope
{
    Unavailable,
    PerSocket,
    HostUdp,
};

struct RuntimeStatsOsDrops
{
    uint64_t                total {0};
    RuntimeStatsOsDropScope scope {RuntimeStatsOsDropScope::Unavailable};
};

const char* runtime_stats_os_drop_scope_name(RuntimeStatsOsDropScope scope) noexcept;

class RuntimeStatsOsDropProvider
{
public:
    RuntimeStatsOsDropProvider()                                             = default;
    RuntimeStatsOsDropProvider(const RuntimeStatsOsDropProvider&)            = delete;
    RuntimeStatsOsDropProvider(RuntimeStatsOsDropProvider&&)                 = delete;
    RuntimeStatsOsDropProvider& operator=(const RuntimeStatsOsDropProvider&) = delete;
    RuntimeStatsOsDropProvider& operator=(RuntimeStatsOsDropProvider&&)      = delete;
    virtual ~RuntimeStatsOsDropProvider()                                    = default;

    virtual RuntimeStatsOsDrops sample() noexcept = 0;
};

struct RuntimeStatsSnapshot
{
    uint64_t rx_packets_total {0};
    uint64_t rx_bytes_total {0};
    uint64_t top_level_messages_total {0};
    uint64_t top_level_bundles_total {0};
    uint64_t runtime_drops_total {0};
    uint64_t os_drops_total {0};

    RuntimeStatsOsDropScope os_drop_scope {RuntimeStatsOsDropScope::Unavailable};

    double rx_packets_per_second {0.0};
    double top_level_messages_per_second {0.0};
    double top_level_bundles_per_second {0.0};
};

class RuntimeStats
{
public:
    RuntimeStats();

    void record_packet(size_t bytes) noexcept;
    void record_message(const MessageView& msg) noexcept;
    void record_bundle(const BundleView& bundle) noexcept;
    void record_runtime_drop(RuntimeStatsDropReason reason, const uint8_t* packet, size_t size) noexcept;
    void record_os_drops(RuntimeStatsOsDrops drops) noexcept;
    void add_os_drops(uint64_t drops, RuntimeStatsOsDropScope scope) noexcept;

    RuntimeStatsSnapshot snapshot();
    void                 reset();

private:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint m_last_snapshot;

    uint64_t m_rx_packets_total {0};
    uint64_t m_rx_bytes_total {0};
    uint64_t m_top_level_messages_total {0};
    uint64_t m_top_level_bundles_total {0};
    uint64_t m_runtime_drops_total {0};
    uint64_t m_os_drops_total {0};

    RuntimeStatsOsDropScope m_os_drop_scope {RuntimeStatsOsDropScope::Unavailable};

    uint64_t m_last_rx_packets_total {0};
    uint64_t m_last_top_level_messages_total {0};
    uint64_t m_last_top_level_bundles_total {0};
};

class OSCStatsServer
{
public:
    explicit OSCStatsServer(std::unique_ptr<Transport> transport);

    using MessageHandler     = std::function<void(const Message&)>;
    using BundleHandler      = std::function<void(const Bundle&)>;
    using MessageViewHandler = std::function<void(const MessageView&)>;
    using BundleViewHandler  = std::function<void(const BundleView&)>;
    using ErrorHandler       = std::function<void(const uint8_t* packet, size_t size)>;

    void set_message_handler(MessageHandler handler) { m_msg_handler = std::move(handler); }
    void set_bundle_handler(BundleHandler handler) { m_bundle_handler = std::move(handler); }

    void set_message_view_handler(MessageViewHandler handler) { m_msg_view_handler = std::move(handler); }
    void set_bundle_view_handler(BundleViewHandler handler) { m_bundle_view_handler = std::move(handler); }

    void set_error_handler(ErrorHandler handler) { m_error_handler = std::move(handler); }
    void set_os_drop_provider(RuntimeStatsOsDropProvider* provider) noexcept { m_os_drop_provider = provider; }

    RuntimeStats&       stats() noexcept { return m_stats; }
    const RuntimeStats& stats() const noexcept { return m_stats; }

    bool process_one();
    int  process_all(int max = -1);

private:
    void sample_os_drops() noexcept;

    std::unique_ptr<Transport>  m_transport;
    std::vector<uint8_t>        m_buffer;
    Message                     m_msg;
    Bundle                      m_bundle;
    MessageHandler              m_msg_handler;
    BundleHandler               m_bundle_handler;
    MessageViewHandler          m_msg_view_handler;
    BundleViewHandler           m_bundle_view_handler;
    ErrorHandler                m_error_handler;
    RuntimeStats                m_stats;
    RuntimeStatsOsDropProvider* m_os_drop_provider {nullptr};
};

class RuntimeStatsUDPTransport final : public Transport, public RuntimeStatsOsDropProvider
{
public:
    RuntimeStatsUDPTransport(const std::string& host, uint16_t port);
    explicit RuntimeStatsUDPTransport(uint16_t port);
    RuntimeStatsUDPTransport(const RuntimeStatsUDPTransport&)            = delete;
    RuntimeStatsUDPTransport& operator=(const RuntimeStatsUDPTransport&) = delete;
    RuntimeStatsUDPTransport(RuntimeStatsUDPTransport&&)                 = delete;
    RuntimeStatsUDPTransport& operator=(RuntimeStatsUDPTransport&&)      = delete;
    ~RuntimeStatsUDPTransport() override;

    bool   send(const uint8_t* data, size_t size) override;
    size_t receive(uint8_t* buffer, size_t buffer_size) override;
    bool   is_ready() const override { return m_connected; }
    void   close() override;

    RuntimeStatsOsDrops sample() noexcept override;
    bool os_drops_supported() const noexcept { return m_os_drop_scope != RuntimeStatsOsDropScope::Unavailable; }

private:
    bool setup_client();
    bool setup_server();
    void enable_per_socket_os_drops() noexcept;
    void record_linux_rxq_overflow(uint32_t low32) noexcept;

    int         m_socket_fd {-1};
    std::string m_host;
    int         m_port {0};

    bool m_is_server {false};
    bool m_connected {false};

    uint64_t                m_os_drops {0};
    uint32_t                m_os_drops_low32 {0};
    bool                    m_seen_os_drops {false};
    RuntimeStatsOsDropScope m_os_drop_scope {RuntimeStatsOsDropScope::Unavailable};
};

class RuntimeStatsHostUdpDropProvider final : public RuntimeStatsOsDropProvider
{
public:
    RuntimeStatsHostUdpDropProvider();

    RuntimeStatsOsDrops sample() noexcept override;
    bool                is_supported() const noexcept { return m_supported; }

private:
    uint32_t m_baseline {0};
    bool     m_supported {false};
};

std::string format_runtime_stats_snapshot(const RuntimeStatsSnapshot& snapshot);

} // namespace NanoOsc

#endif // NANOOSC_RUNTIME_STATS_HPP
