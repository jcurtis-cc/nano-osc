#include "doctest.h"
#include "nanoosc/runtime_stats.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace NanoOsc;

namespace {

class ReplayTransport : public Transport, public OsDropProvider
{
public:
    explicit ReplayTransport(std::vector<std::vector<uint8_t>> packets, OsDropDelta os_drops = OsDropDelta {})
        : m_packets(std::move(packets)), m_os_drops(os_drops)
    {}

    bool send(const uint8_t*, size_t) override { return true; }

    size_t receive(uint8_t* buf, size_t buf_size) override
    {
        if (m_next >= m_packets.size()) return 0;

        const auto& packet = m_packets[m_next++];
        size_t      n      = packet.size();
        if (n > buf_size) n = buf_size;
        std::memcpy(buf, packet.data(), n);
        return n;
    }

    bool is_ready() const override { return true; }
    void close() override {}

    OsDropDelta sample() noexcept override
    {
        ++m_sample_calls;
        OsDropDelta delta = m_pending;
        m_pending         = OsDropDelta {};
        return delta;
    }

    void queue_drops(OsDropDelta drops) noexcept { m_pending = drops; }

    size_t sample_calls() const noexcept { return m_sample_calls; }

private:
    std::vector<std::vector<uint8_t>> m_packets;
    size_t                            m_next {0};
    OsDropDelta                       m_os_drops;
    OsDropDelta                       m_pending {m_os_drops};
    size_t                            m_sample_calls {0};
};

Message test_message(const char* address, int value)
{
    Message msg(address);
    msg.add_int32(value);
    return msg;
}

} // namespace

TEST_CASE("OSCStatsServer counts packets and top-level bundles")
{
    Bundle root;
    root.add_message(test_message("/a", 1));
    root.add_message(test_message("/b", 2));

    Bundle nested;
    nested.add_message(test_message("/c", 3));
    root.add_bundle(nested);

    auto transport = std::make_unique<ReplayTransport>(
        std::vector<std::vector<uint8_t>> {root.encode()}, OsDropDelta {7, OsDropScope::PerSocket}
    );
    auto*          provider = transport.get();
    OSCStatsServer server(std::move(transport));
    server.set_os_drop_provider(provider);

    CHECK(server.process_all(1) == 1);

    const RuntimeStatsSnapshot snapshot = server.stats().snapshot();
    CHECK(snapshot.rx_packets_total == 1);
    CHECK(snapshot.top_level_messages_total == 0);
    CHECK(snapshot.top_level_bundles_total == 1);
    CHECK(snapshot.os_drops_total == 7);
    CHECK(snapshot.os_drop_scope == OsDropScope::PerSocket);
    CHECK(snapshot.runtime_drops.total() == 0);
}

TEST_CASE("OSCStatsServer counts malformed packets as runtime drops")
{
    std::vector<uint8_t> malformed = {
        '/',
        'x',
        0x00,
        0x00,
        ',',
        'i',
        0x00,
        0x00,
    };

    auto           transport = std::make_unique<ReplayTransport>(std::vector<std::vector<uint8_t>> {malformed});
    OSCStatsServer server(std::move(transport));

    int errors = 0;
    server.set_error_handler([&](const uint8_t*, size_t) { ++errors; });

    CHECK_FALSE(server.process_one());

    const RuntimeStatsSnapshot snapshot = server.stats().snapshot();
    CHECK(snapshot.rx_packets_total == 1);
    CHECK(snapshot.top_level_messages_total == 0);
    CHECK(snapshot.runtime_drops.total() == 1);
    CHECK(snapshot.runtime_drops.validation_failed == 1);
    CHECK(snapshot.runtime_drops.decode_failed == 0);
    CHECK(snapshot.runtime_drops.assignment_failed == 0);
    CHECK(errors == 1);
}

TEST_CASE("OSCStatsServer records process_all packet and message counts")
{
    Message msg    = test_message("/x", 1);
    auto    packet = msg.encode();

    auto transport = std::make_unique<ReplayTransport>(std::vector<std::vector<uint8_t>> {packet, packet, packet});
    OSCStatsServer server(std::move(transport));

    CHECK(server.process_all(5) == 3);

    RuntimeStatsSnapshot snapshot = server.stats().snapshot();
    CHECK(snapshot.rx_packets_total == 3);
    CHECK(snapshot.top_level_messages_total == 3);
    CHECK(snapshot.top_level_bundles_total == 0);
}

TEST_CASE("OSCStatsServer prefers view handlers like OSCServer")
{
    Message msg    = test_message("/x", 1);
    auto    packet = msg.encode();

    auto           transport = std::make_unique<ReplayTransport>(std::vector<std::vector<uint8_t>> {packet});
    OSCStatsServer server(std::move(transport));

    int view_handled   = 0;
    int owning_handled = 0;
    server.set_message_view_handler([&](const MessageView&) { ++view_handled; });
    server.set_message_handler([&](const Message&) { ++owning_handled; });

    CHECK(server.process_one());
    CHECK(view_handled == 1);
    CHECK(owning_handled == 0);
    CHECK(server.stats().snapshot().top_level_messages_total == 1);
}

TEST_CASE("OSCStatsServer samples OS drops once per process_all batch")
{
    Message msg    = test_message("/x", 1);
    auto    packet = msg.encode();

    auto transport = std::make_unique<ReplayTransport>(
        std::vector<std::vector<uint8_t>> {packet, packet, packet}, OsDropDelta {1, OsDropScope::PerSocket}
    );
    auto*          provider = transport.get();
    OSCStatsServer server(std::move(transport));
    server.set_os_drop_provider(provider);

    CHECK(server.process_all(10) == 3);
    CHECK(provider->sample_calls() == 1);
}

TEST_CASE("OSCStatsServer process_one does not sample OS drops")
{
    Message msg    = test_message("/x", 1);
    auto    packet = msg.encode();

    auto transport = std::make_unique<ReplayTransport>(
        std::vector<std::vector<uint8_t>> {packet, packet, packet}, OsDropDelta {1, OsDropScope::PerSocket}
    );
    auto*          provider = transport.get();
    OSCStatsServer server(std::move(transport));
    server.set_os_drop_provider(provider);

    CHECK(server.process_one());
    CHECK(server.process_one());
    CHECK(server.process_one());
    CHECK(provider->sample_calls() == 0);
}

TEST_CASE("RuntimeStats accumulates OS drop deltas within a scope")
{
    RuntimeStats stats;
    stats.add_os_drops(OsDropDelta {3, OsDropScope::PerSocket});
    stats.add_os_drops(OsDropDelta {4, OsDropScope::PerSocket});

    RuntimeStatsSnapshot snapshot = stats.snapshot();
    CHECK(snapshot.os_drops_total == 7);
    CHECK(snapshot.os_drop_scope == OsDropScope::PerSocket);
}

TEST_CASE("RuntimeStats resets OS drop total when scope changes")
{
    RuntimeStats stats;
    stats.add_os_drops(OsDropDelta {10, OsDropScope::PerSocket});
    stats.add_os_drops(OsDropDelta {2, OsDropScope::HostUdp});

    RuntimeStatsSnapshot snapshot = stats.snapshot();
    CHECK(snapshot.os_drops_total == 2);
    CHECK(snapshot.os_drop_scope == OsDropScope::HostUdp);
}

TEST_CASE("RuntimeStats ignores unavailable OS drop deltas")
{
    RuntimeStats stats;
    stats.add_os_drops(OsDropDelta {5, OsDropScope::PerSocket});
    stats.add_os_drops(OsDropDelta {99, OsDropScope::Unavailable});

    RuntimeStatsSnapshot snapshot = stats.snapshot();
    CHECK(snapshot.os_drops_total == 5);
    CHECK(snapshot.os_drop_scope == OsDropScope::PerSocket);
}

TEST_CASE("RuntimeStats records runtime drops by reason")
{
    RuntimeStats stats;
    stats.record_runtime_drop(RuntimeDropReason::DecodeFailed);
    stats.record_runtime_drop(RuntimeDropReason::DecodeFailed);
    stats.record_runtime_drop(RuntimeDropReason::ValidationFailed);
    stats.record_runtime_drop(RuntimeDropReason::AssignmentFailed);

    RuntimeStatsSnapshot snapshot = stats.snapshot();
    CHECK(snapshot.runtime_drops.decode_failed == 2);
    CHECK(snapshot.runtime_drops.validation_failed == 1);
    CHECK(snapshot.runtime_drops.assignment_failed == 1);
    CHECK(snapshot.runtime_drops.total() == 4);
}

TEST_CASE("RuntimeStats formats compact snapshots")
{
    RuntimeStats stats;
    stats.add_os_drops(OsDropDelta {120, OsDropScope::HostUdp});

    RuntimeStatsSnapshot snapshot          = stats.snapshot();
    snapshot.rx_packets_per_second         = 4500.0;
    snapshot.top_level_messages_per_second = 18000.0;
    snapshot.top_level_bundles_per_second  = 12.0;

    const std::string text = format_runtime_stats_snapshot(snapshot);
    CHECK(text.find("rx packets/sec      4500") != std::string::npos);
    CHECK(text.find("top messages/sec    18000") != std::string::npos);
    CHECK(text.find("bundles/sec         12.0") != std::string::npos);
    CHECK(text.find("os drops            120 (host_udp)") != std::string::npos);
    CHECK(text.find("runtime drops       0\n") != std::string::npos);
}

TEST_CASE("Formatter renders runtime drop breakdown when total is non-zero")
{
    RuntimeStatsSnapshot snapshot;
    snapshot.runtime_drops.decode_failed     = 4;
    snapshot.runtime_drops.validation_failed = 2;
    snapshot.runtime_drops.assignment_failed = 1;

    const std::string text = format_runtime_stats_snapshot(snapshot);
    CHECK(text.find("runtime drops       7 (decode 4, validation 2, assignment 1)") != std::string::npos);
}

TEST_CASE("Formatter omits zero-valued runtime drop reasons")
{
    RuntimeStatsSnapshot snapshot;
    snapshot.runtime_drops.decode_failed = 3;

    const std::string text = format_runtime_stats_snapshot(snapshot);
    CHECK(text.find("runtime drops       3 (decode 3)") != std::string::npos);
    CHECK(text.find("validation") == std::string::npos);
    CHECK(text.find("assignment") == std::string::npos);
}

TEST_CASE("RuntimeStats names OS drop scopes")
{
    CHECK(std::string(os_drop_scope_name(OsDropScope::Unavailable)) == "unavailable");
    CHECK(std::string(os_drop_scope_name(OsDropScope::PerSocket)) == "per_socket");
    CHECK(std::string(os_drop_scope_name(OsDropScope::HostUdp)) == "host_udp");
}

TEST_CASE("BsdHostUdpProvider is safe to sample")
{
    BsdHostUdpProvider host_provider;
    OsDropDelta        host_drops = host_provider.sample();
    if (host_provider.is_supported())
    {
        CHECK(host_drops.scope == OsDropScope::HostUdp);
    }
    else
    {
        CHECK(host_drops.scope == OsDropScope::Unavailable);
    }
}
