#include "nanoosc/runtime_stats.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>

#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <netinet/udp_var.h>
#include <sys/sysctl.h>
#endif

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

    text += " (";
    bool first = true;
    auto append = [&](const char* label, uint64_t n) {
        if (n == 0) return;
        if (!first) text += ", ";
        first = false;
        text += label;
        text += ' ';
        text += std::to_string(n);
    };
    append("decode", counts.decode_failed);
    append("validation", counts.validation_failed);
    append("assignment", counts.assignment_failed);
    text += ')';
    return text;
}

OsDropDelta unavailable_os_drops() noexcept
{
    return OsDropDelta {};
}

std::system_error unsupported_runtime_stats_udp()
{
    return std::system_error(
        std::make_error_code(std::errc::function_not_supported), "RuntimeStatsUDPTransport requires POSIX sockets"
    );
}

bool read_host_udp_fullsock(uint32_t& out) noexcept
{
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    struct udpstat stats;
    std::memset(&stats, 0, sizeof(stats));
    size_t size = sizeof(stats);
    if (::sysctlbyname("net.inet.udp.stats", &stats, &size, nullptr, 0) != 0)
    {
        return false;
    }
    if (size < sizeof(stats))
    {
        return false;
    }
    out = stats.udps_fullsock;
    return true;
#else
    (void)out;
    return false;
#endif
}

} // namespace

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

RuntimeStats::RuntimeStats() : m_last_snapshot(Clock::now()) {}

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

RuntimeStatsSnapshot RuntimeStats::snapshot()
{
    const TimePoint now     = Clock::now();
    const double    seconds = std::chrono::duration<double>(now - m_last_snapshot).count();

    RuntimeStatsSnapshot out;
    out.rx_packets_total         = m_rx_packets_total;
    out.rx_bytes_total           = m_rx_bytes_total;
    out.top_level_messages_total = m_top_level_messages_total;
    out.top_level_bundles_total  = m_top_level_bundles_total;
    out.runtime_drops            = m_runtime_drops;
    out.os_drops_total           = m_os_drops_total;
    out.os_drop_scope            = m_os_drop_scope;

    if (seconds > 0.0)
    {
        out.rx_packets_per_second = static_cast<double>(m_rx_packets_total - m_last_rx_packets_total) / seconds;
        out.top_level_messages_per_second =
            static_cast<double>(m_top_level_messages_total - m_last_top_level_messages_total) / seconds;
        out.top_level_bundles_per_second =
            static_cast<double>(m_top_level_bundles_total - m_last_top_level_bundles_total) / seconds;
    }

    m_last_snapshot                 = now;
    m_last_rx_packets_total         = m_rx_packets_total;
    m_last_top_level_messages_total = m_top_level_messages_total;
    m_last_top_level_bundles_total  = m_top_level_bundles_total;

    return out;
}

void RuntimeStats::reset()
{
    m_last_snapshot                 = Clock::now();
    m_rx_packets_total              = 0;
    m_rx_bytes_total                = 0;
    m_top_level_messages_total      = 0;
    m_top_level_bundles_total       = 0;
    m_os_drops_total                = 0;
    m_runtime_drops                 = RuntimeDropCounts {};
    m_os_drop_scope                 = OsDropScope::Unavailable;
    m_last_rx_packets_total         = 0;
    m_last_top_level_messages_total = 0;
    m_last_top_level_bundles_total  = 0;
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

RuntimeStatsUDPTransport::RuntimeStatsUDPTransport(const std::string& host, uint16_t port)
    : m_host(host), m_port(port), m_is_server(false)
{
    if (!setup_client())
    {
        throw std::system_error(errno, std::generic_category(), "stats UDP client setup failed");
    }
}

RuntimeStatsUDPTransport::RuntimeStatsUDPTransport(uint16_t port) : m_port(port), m_is_server(true)
{
    if (!setup_server())
    {
        throw std::system_error(errno, std::generic_category(), "stats UDP server setup failed");
    }
}

RuntimeStatsUDPTransport::~RuntimeStatsUDPTransport()
{
    close();
}

bool RuntimeStatsUDPTransport::setup_client()
{
#if defined(__unix__) || defined(__APPLE__)
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        return false;
    }

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(m_port);

    if (inet_pton(AF_INET, m_host.c_str(), &server_addr.sin_addr) <= 0)
    {
        ::close(fd);
        return false;
    }

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0)
    {
        ::close(fd);
        return false;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        ::close(fd);
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        ::close(fd);
        return false;
    }

    m_socket_fd = fd;
    m_connected = true;
    enable_per_socket_os_drops();
    return true;
#else
    throw unsupported_runtime_stats_udp();
#endif
}

bool RuntimeStatsUDPTransport::setup_server()
{
#if defined(__unix__) || defined(__APPLE__)
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        return false;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(m_port);

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0)
    {
        ::close(fd);
        return false;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        ::close(fd);
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        ::close(fd);
        return false;
    }

    m_socket_fd = fd;
    m_connected = true;
    enable_per_socket_os_drops();
    return true;
#else
    throw unsupported_runtime_stats_udp();
#endif
}

bool RuntimeStatsUDPTransport::send(const uint8_t* data, size_t size)
{
#if defined(__unix__) || defined(__APPLE__)
    if (!m_connected || m_socket_fd < 0)
    {
        return false;
    }

    ssize_t sent = ::send(m_socket_fd, data, size, 0);
    if (sent < 0)
    {
        return false;
    }
    return sent == static_cast<ssize_t>(size);
#else
    (void)data;
    (void)size;
    return false;
#endif
}

size_t RuntimeStatsUDPTransport::receive(uint8_t* buffer, size_t buffer_size)
{
#if defined(__unix__) || defined(__APPLE__)
    if (!m_connected || m_socket_fd < 0) return 0;

#ifdef SO_RXQ_OVFL
    struct iovec iov;
    std::memset(&iov, 0, sizeof(iov));
    iov.iov_base = buffer;
    iov.iov_len  = buffer_size;

    char control[CMSG_SPACE(sizeof(uint32_t))];
    std::memset(control, 0, sizeof(control));

    struct msghdr msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = control;
    msg.msg_controllen = sizeof(control);

    ssize_t received = ::recvmsg(m_socket_fd, &msg, 0);
    if (received < 0)
    {
        return 0;
    }

    for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg))
    {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_RXQ_OVFL)
        {
            uint32_t kernel_value = 0;
            std::memcpy(&kernel_value, CMSG_DATA(cmsg), sizeof(kernel_value));
            record_linux_rxq_overflow(kernel_value);
        }
    }
#else
    ssize_t received = ::recv(m_socket_fd, buffer, buffer_size, 0);
    if (received < 0)
    {
        return 0;
    }
#endif

    return static_cast<size_t>(received);
#else
    (void)buffer;
    (void)buffer_size;
    return 0;
#endif
}

void RuntimeStatsUDPTransport::close()
{
#if defined(__unix__) || defined(__APPLE__)
    if (m_socket_fd >= 0)
    {
        ::close(m_socket_fd);
        m_socket_fd = -1;
        m_connected = false;
    }
#endif
}

OsDropDelta RuntimeStatsUDPTransport::sample() noexcept
{
    if (m_os_drop_scope == OsDropScope::Unavailable)
    {
        return unavailable_os_drops();
    }
    OsDropDelta delta {m_pending_os_drops, m_os_drop_scope};
    m_pending_os_drops = 0;
    return delta;
}

void RuntimeStatsUDPTransport::enable_per_socket_os_drops() noexcept
{
#if defined(SO_RXQ_OVFL) && (defined(__unix__) || defined(__APPLE__))
    if (m_socket_fd < 0) return;
    int opt = 1;
    if (setsockopt(m_socket_fd, SOL_SOCKET, SO_RXQ_OVFL, &opt, sizeof(opt)) == 0)
    {
        m_os_drop_scope = OsDropScope::PerSocket;
    }
#endif
}

void RuntimeStatsUDPTransport::record_linux_rxq_overflow(uint32_t kernel_value) noexcept
{
    if (m_seen_os_drops)
    {
        m_pending_os_drops += static_cast<uint32_t>(kernel_value - m_last_rxq_ovfl_value);
    }
    else
    {
        m_pending_os_drops = kernel_value;
        m_seen_os_drops    = true;
    }
    m_last_rxq_ovfl_value = kernel_value;
    m_os_drop_scope       = OsDropScope::PerSocket;
}

BsdHostUdpProvider::BsdHostUdpProvider()
{
    uint32_t current = 0;
    m_supported      = read_host_udp_fullsock(current);
    m_last_value     = current;
}

OsDropDelta BsdHostUdpProvider::sample() noexcept
{
    if (!m_supported)
    {
        return unavailable_os_drops();
    }

    uint32_t current = 0;
    if (!read_host_udp_fullsock(current))
    {
        return unavailable_os_drops();
    }

    const uint64_t delta = static_cast<uint32_t>(current - m_last_value);
    m_last_value         = current;
    return OsDropDelta {delta, OsDropScope::HostUdp};
}

std::string format_runtime_stats_snapshot(const RuntimeStatsSnapshot& snapshot)
{
    std::ostringstream out;
    append_metric(out, "rx packets/sec", format_rate(snapshot.rx_packets_per_second));
    append_metric(out, "top messages/sec", format_rate(snapshot.top_level_messages_per_second));
    append_metric(out, "bundles/sec", format_rate(snapshot.top_level_bundles_per_second));
    out << "\n";
    append_metric(
        out, "os drops",
        std::to_string(snapshot.os_drops_total) + " (" + os_drop_scope_name(snapshot.os_drop_scope) + ")"
    );
    append_metric(out, "runtime drops", format_runtime_drops(snapshot.runtime_drops));
    return out.str();
}

} // namespace NanoOsc
