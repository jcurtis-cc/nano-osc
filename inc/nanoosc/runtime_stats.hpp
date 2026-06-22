#ifndef NANOOSC_RUNTIME_STATS_HPP
#define NANOOSC_RUNTIME_STATS_HPP

#include "nanoosc/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace NanoOsc {

enum class OsDropScope : uint8_t
{
    Unavailable,
    PerSocket,
    HostUdp,
};

struct OsDropDelta
{
    uint64_t    drops {0};
    OsDropScope scope {OsDropScope::Unavailable};
};

const char* os_drop_scope_name(OsDropScope scope) noexcept;

class OsDropProvider
{
public:
    OsDropProvider()                                 = default;
    OsDropProvider(const OsDropProvider&)            = delete;
    OsDropProvider(OsDropProvider&&)                 = delete;
    OsDropProvider& operator=(const OsDropProvider&) = delete;
    OsDropProvider& operator=(OsDropProvider&&)      = delete;
    virtual ~OsDropProvider()                        = default;

    virtual OsDropDelta sample() noexcept = 0;
};

enum class RuntimeDropReason : uint8_t
{
    DecodeFailed,
    ValidationFailed,
    AssignmentFailed,
};

struct RuntimeDropCounts
{
    uint64_t decode_failed {0};
    uint64_t validation_failed {0};
    uint64_t assignment_failed {0};

    uint64_t total() const noexcept { return decode_failed + validation_failed + assignment_failed; }
};

struct RuntimeStatsSnapshot
{
    uint64_t rx_packets_total {0};
    uint64_t rx_bytes_total {0};
    uint64_t top_level_messages_total {0};
    uint64_t top_level_bundles_total {0};

    RuntimeDropCounts runtime_drops;
    // Historical cumulative count from the configured OS-drop provider. This
    // is not proof that provider sampling is currently available.
    uint64_t          os_drops_total {0};
    OsDropScope       os_drop_scope {OsDropScope::Unavailable};
};

struct RuntimeStatsRateSample
{
    double elapsed_seconds {0.0};
    double rx_packets_per_second {0.0};
    double top_level_messages_per_second {0.0};
    double top_level_bundles_per_second {0.0};
};

class RuntimeStats
{
public:
    RuntimeStats();

    void record_packet(size_t bytes) noexcept;
    // msg/bundle args reserved for future per-address/per-typetag stats.
    void record_message(const MessageView& msg) noexcept;
    void record_bundle(const BundleView& bundle) noexcept;
    void record_runtime_drop(RuntimeDropReason reason) noexcept;
    void add_os_drops(OsDropDelta delta) noexcept;

    RuntimeStatsSnapshot   snapshot() const noexcept;
    RuntimeStatsRateSample sample_rates() noexcept;
    void                   reset() noexcept;

private:
    uint64_t m_last_rate_sample_ns {0};

    uint64_t m_rx_packets_total {0};
    uint64_t m_rx_bytes_total {0};
    uint64_t m_top_level_messages_total {0};
    uint64_t m_top_level_bundles_total {0};
    uint64_t m_os_drops_total {0};

    RuntimeDropCounts m_runtime_drops;
    OsDropScope       m_os_drop_scope {OsDropScope::Unavailable};

    uint64_t m_last_rate_rx_packets_total {0};
    uint64_t m_last_rate_top_level_messages_total {0};
    uint64_t m_last_rate_top_level_bundles_total {0};
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
    // OS drops are sampled once per process_all() batch, not per process_one() packet.
    void set_os_drop_provider(OsDropProvider* provider) noexcept { m_os_drop_provider = provider; }

    RuntimeStats&       stats() noexcept { return m_stats; }
    const RuntimeStats& stats() const noexcept { return m_stats; }
    void                reset_stats() noexcept { m_stats.reset(); }

    bool process_one();
    int  process_all(int max = -1);

private:
    void sample_os_drops() noexcept;

    std::unique_ptr<Transport> m_transport;
    std::vector<uint8_t>       m_buffer;
    Message                    m_msg;
    Bundle                     m_bundle;
    MessageHandler             m_msg_handler;
    BundleHandler              m_bundle_handler;
    MessageViewHandler         m_msg_view_handler;
    BundleViewHandler          m_bundle_view_handler;
    ErrorHandler               m_error_handler;
    RuntimeStats               m_stats;
    OsDropProvider*            m_os_drop_provider {nullptr};
};

std::string format_runtime_stats_snapshot(const RuntimeStatsSnapshot& snapshot);
std::string format_runtime_stats_snapshot(const RuntimeStatsSnapshot& snapshot, const RuntimeStatsRateSample& rates);

} // namespace NanoOsc

#endif // NANOOSC_RUNTIME_STATS_HPP
