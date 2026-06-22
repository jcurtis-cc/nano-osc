#include "nanoosc/runtime_stats.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>

namespace NanoOsc {
namespace {

std::string format_double(double value, int precision)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string format_rate(double value)
{
    if (value >= 100.0)
    {
        return format_double(value, 0);
    }
    if (value >= 10.0)
    {
        return format_double(value, 1);
    }
    return format_double(value, 2);
}

void append_metric(std::ostringstream& out, const char* label, const std::string& value)
{
    out << std::left << std::setw(20) << label << value << "\n";
}

std::string format_runtime_drops(const RuntimeDropCounts& counts)
{
    std::string text = std::to_string(counts.total());
    if (counts.total() == 0)
    {
        return text;
    }

    text        += " (";
    bool first   = true;
    auto append  = [&](const char* label, uint64_t n) {
        if (n == 0) return;
        if (!first) text += ", ";
        first  = false;
        text  += label;
        text  += ' ';
        text  += std::to_string(n);
    };
    append("decode", counts.decode_failed);
    append("validation", counts.validation_failed);
    append("assignment", counts.assignment_failed);
    text += ')';
    return text;
}

uint64_t monotonic_ns() noexcept
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count()
    );
}

} // namespace

RuntimeStats::RuntimeStats() : m_last_rate_sample_ns(monotonic_ns()) {}

const char* os_drop_scope_name(OsDropScope scope) noexcept
{
    switch (scope)
    {
        case OsDropScope::Unavailable:
            return "unavailable";
        case OsDropScope::PerSocket:
            return "per_socket";
        case OsDropScope::HostUdp:
            return "host_udp";
    }
    return "unavailable";
}

void RuntimeStats::record_packet(size_t bytes) noexcept
{
    ++m_rx_packets_total;
    m_rx_bytes_total += bytes;
}

void RuntimeStats::record_message(const MessageView& msg) noexcept
{
    (void)msg;
    ++m_top_level_messages_total;
}

void RuntimeStats::record_bundle(const BundleView& bundle) noexcept
{
    (void)bundle;
    ++m_top_level_bundles_total;
}

void RuntimeStats::record_runtime_drop(RuntimeDropReason reason) noexcept
{
    switch (reason)
    {
        case RuntimeDropReason::DecodeFailed:
            ++m_runtime_drops.decode_failed;
            return;
        case RuntimeDropReason::ValidationFailed:
            ++m_runtime_drops.validation_failed;
            return;
        case RuntimeDropReason::AssignmentFailed:
            ++m_runtime_drops.assignment_failed;
            return;
    }
}

void RuntimeStats::add_os_drops(OsDropDelta delta) noexcept
{
    if (delta.scope == OsDropScope::Unavailable)
    {
        return;
    }

    if (m_os_drop_scope != delta.scope)
    {
        m_os_drop_scope  = delta.scope;
        m_os_drops_total = 0;
    }
    m_os_drops_total += delta.drops;
}

RuntimeStatsSnapshot RuntimeStats::snapshot() const noexcept
{
    RuntimeStatsSnapshot out;
    out.rx_packets_total         = m_rx_packets_total;
    out.rx_bytes_total           = m_rx_bytes_total;
    out.top_level_messages_total = m_top_level_messages_total;
    out.top_level_bundles_total  = m_top_level_bundles_total;
    out.runtime_drops            = m_runtime_drops;
    out.os_drops_total           = m_os_drops_total;
    out.os_drop_scope            = m_os_drop_scope;
    return out;
}

RuntimeStatsRateSample RuntimeStats::sample_rates() noexcept
{
    const uint64_t now_ns  = monotonic_ns();
    const double   seconds = static_cast<double>(now_ns - m_last_rate_sample_ns) / 1e9;

    RuntimeStatsRateSample out;
    out.elapsed_seconds = seconds;
    if (seconds > 0.0)
    {
        out.rx_packets_per_second = static_cast<double>(m_rx_packets_total - m_last_rate_rx_packets_total) / seconds;
        out.top_level_messages_per_second =
            static_cast<double>(m_top_level_messages_total - m_last_rate_top_level_messages_total) / seconds;
        out.top_level_bundles_per_second =
            static_cast<double>(m_top_level_bundles_total - m_last_rate_top_level_bundles_total) / seconds;
    }

    m_last_rate_sample_ns                = now_ns;
    m_last_rate_rx_packets_total         = m_rx_packets_total;
    m_last_rate_top_level_messages_total = m_top_level_messages_total;
    m_last_rate_top_level_bundles_total  = m_top_level_bundles_total;

    return out;
}

void RuntimeStats::reset() noexcept
{
    m_last_rate_sample_ns                = monotonic_ns();
    m_rx_packets_total                   = 0;
    m_rx_bytes_total                     = 0;
    m_top_level_messages_total           = 0;
    m_top_level_bundles_total            = 0;
    m_os_drops_total                     = 0;
    m_runtime_drops                      = RuntimeDropCounts {};
    m_os_drop_scope                      = OsDropScope::Unavailable;
    m_last_rate_rx_packets_total         = 0;
    m_last_rate_top_level_messages_total = 0;
    m_last_rate_top_level_bundles_total  = 0;
}

OSCStatsServer::OSCStatsServer(std::unique_ptr<Transport> transport)
    : m_transport(std::move(transport)), m_buffer(BUFFER_MAX_SIZE)
{}

bool OSCStatsServer::process_one()
{
    size_t received = m_transport->receive(m_buffer.data(), m_buffer.size());
    if (received == 0) return false;

    m_stats.record_packet(received);

    auto report_drop = [&](RuntimeDropReason reason) {
        m_stats.record_runtime_drop(reason);
        if (m_error_handler) m_error_handler(m_buffer.data(), received);
    };

    using namespace detail;
    if (is_bundle(m_buffer.data(), received))
    {
        BundleView bv;
        if (!decode_bundle_view(bv, m_buffer.data(), received))
        {
            report_drop(RuntimeDropReason::DecodeFailed);
            return false;
        }
        if (!validate_bundle_view(bv))
        {
            report_drop(RuntimeDropReason::ValidationFailed);
            return false;
        }
        m_stats.record_bundle(bv);
        if (m_bundle_view_handler)
        {
            m_bundle_view_handler(bv);
        }
        else if (m_bundle_handler)
        {
            if (!m_bundle.assign(bv))
            {
                report_drop(RuntimeDropReason::AssignmentFailed);
                return false;
            }
            m_bundle_handler(m_bundle);
        }
        return true;
    }

    MessageView mv;
    if (!decode_message_view(mv, m_buffer.data(), received))
    {
        report_drop(RuntimeDropReason::DecodeFailed);
        return false;
    }
    if (!validate_message_view(mv))
    {
        report_drop(RuntimeDropReason::ValidationFailed);
        return false;
    }
    m_stats.record_message(mv);
    if (m_msg_view_handler)
    {
        m_msg_view_handler(mv);
    }
    else if (m_msg_handler)
    {
        if (!m_msg.assign(mv))
        {
            report_drop(RuntimeDropReason::AssignmentFailed);
            return false;
        }
        m_msg_handler(m_msg);
    }
    return true;
}

int OSCStatsServer::process_all(int max)
{
    int count = 0;
    while ((max < 0 || count < max) && process_one())
    {
        count++;
    }
    sample_os_drops();
    return count;
}

void OSCStatsServer::sample_os_drops() noexcept
{
    if (m_os_drop_provider)
    {
        m_stats.add_os_drops(m_os_drop_provider->sample());
    }
}

std::string format_runtime_stats_snapshot(const RuntimeStatsSnapshot& snapshot)
{
    return format_runtime_stats_snapshot(snapshot, RuntimeStatsRateSample {});
}

std::string format_runtime_stats_snapshot(const RuntimeStatsSnapshot& snapshot, const RuntimeStatsRateSample& rates)
{
    std::ostringstream out;
    append_metric(out, "rx packets/sec", format_rate(rates.rx_packets_per_second));
    append_metric(out, "top messages/sec", format_rate(rates.top_level_messages_per_second));
    append_metric(out, "bundles/sec", format_rate(rates.top_level_bundles_per_second));
    out << "\n";
    append_metric(
        out,
        "os drops",
        std::to_string(snapshot.os_drops_total) + " (" + os_drop_scope_name(snapshot.os_drop_scope) + ")"
    );
    append_metric(out, "runtime drops", format_runtime_drops(snapshot.runtime_drops));
    return out.str();
}

} // namespace NanoOsc
