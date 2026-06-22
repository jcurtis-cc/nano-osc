#include "nanoosc/udp_transport.hpp"

#include <cerrno>
#include <cstring>
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

RuntimeStatsUDPTransport::RuntimeStatsUDPTransport(const std::string& host, uint16_t port) : m_host(host), m_port(port)
{
    if (!setup_client())
    {
        throw std::system_error(errno, std::generic_category(), "stats UDP client setup failed");
    }
}

RuntimeStatsUDPTransport::RuntimeStatsUDPTransport(uint16_t port) : m_port(port)
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
        const uint32_t delta  = kernel_value - m_last_rxq_ovfl_value;
        m_pending_os_drops   += delta;
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

    const uint32_t delta = current - m_last_value;
    m_last_value         = current;
    return OsDropDelta {delta, OsDropScope::HostUdp};
}

} // namespace NanoOsc
