#include "nanoosc/udp_transport.hpp"

#include <cerrno>
#include <cstring>
#include <system_error>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace NanoOsc {

UDPTransport::UDPTransport(const std::string& host, uint16_t port)
    : m_socket_fd(-1), m_host(host), m_port(port), m_is_server(false), m_connected(false)
{
    if (!setup_client())
    {
        throw std::system_error(errno, std::generic_category(), "UDP client setup failed");
    }
}

UDPTransport::UDPTransport(uint16_t port) : m_socket_fd(-1), m_port(port), m_is_server(true), m_connected(false)
{
    if (!setup_server())
    {
        throw std::system_error(errno, std::generic_category(), "UDP server setup failed");
    }
}

UDPTransport::~UDPTransport()
{
    close();
}

bool UDPTransport::setup_client()
{
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

    if (::connect(fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
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
    return true;
}

bool UDPTransport::setup_server()
{
    m_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socket_fd < 0)
    {
        return false;
    }

    int opt = 1;
    setsockopt(m_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(m_port);

    if (bind(m_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        ::close(m_socket_fd);
        m_socket_fd = -1;
        return false;
    }

    int flags = fcntl(m_socket_fd, F_GETFL, 0);
    fcntl(m_socket_fd, F_SETFL, flags | O_NONBLOCK);

    m_connected = true;
    return true;
}

bool UDPTransport::send(const uint8_t* data, size_t size)
{
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
}

size_t UDPTransport::receive(uint8_t* buffer, size_t buffer_size)
{
    if (!m_connected || m_socket_fd < 0) return 0;

    ssize_t received = ::recv(m_socket_fd, buffer, buffer_size, 0);
    if (received < 0)
    {
        return 0;
    }

    return static_cast<size_t>(received);
}

void UDPTransport::close()
{
    if (m_socket_fd >= 0)
    {
        ::close(m_socket_fd);
        m_socket_fd = -1;
        m_connected = false;
    }
}

} // namespace NanoOsc
