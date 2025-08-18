#include "nano-osc.hpp"
#include <cerrno>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <system_error>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

namespace NanoOsc {

std::vector<uint8_t> Message::encode() const
{
    using namespace detail;
    std::vector<uint8_t> buffer;
    buffer.reserve(512);
    add_osc_string(buffer, address);
    add_osc_string(buffer, tags);

    for (const auto& arg : arguments)
    {
        std::visit(
            [&](const auto& value)
            {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, OSCInt>)
                {
                    add_osc_int32(buffer, value);
                }
                else if constexpr (std::is_same_v<T, OSCFloat>)
                {
                    add_osc_float(buffer, value);
                }
                else if constexpr (std::is_same_v<T, OSCString>)
                {
                    add_osc_string(buffer, value);
                }
                else if constexpr (std::is_same_v<T, OSCBlob>)
                {
                    add_osc_blob(buffer, value.data(), value.size());
                }
            },
            arg
        );
    }
    return buffer;
}

Message Message::decode(const uint8_t* data, size_t size)
{
    using namespace detail;
    std::string addr;
    std::string tagstr;
    size_t offset = 0;
    if (!read_osc_string(addr, data, size, offset)) {
        throw std::runtime_error("Could not read OSC message address");
    }
    if (!read_osc_string(tagstr, data, size, offset)) {
        throw std::runtime_error("Could not read OSC message format string");
    }

    Message msg(addr);
    msg.tags = tagstr;

    for (char& tag : tagstr)
    {
        switch(tag) {
            case 'i':
                msg.arguments.emplace_back(read_osc_int32(data, offset));
                break;
            case 'f':
                msg.arguments.emplace_back(read_osc_float32(data, offset));
                break;
            case 's': {
                std::string s;
                read_osc_string(s, data, size, offset);
                msg.arguments.emplace_back(s);
                break;
            }
            case 'b': {
                std::vector<uint8_t> b;
                read_osc_blob(b, data, size, offset);
                msg.arguments.emplace_back(b);
                break;
            }
            default:
                break;
        }
    }
    return msg;
}

bool UDPTransport::setup_client()
{
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        return false;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0)
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

    socket_fd = fd;
    connected = true;
    return true;
}

bool UDPTransport::setup_server()
{
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0)
    {
        return false;
    }

    int opt = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(port);

    if (bind(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        ::close(socket_fd);
        socket_fd = -1;
        return false;
    }

    int flags = fcntl(socket_fd, F_GETFL, 0);
    fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);

    connected = true;
    return true;
}

bool UDPTransport::send(const uint8_t* data, size_t size)
{
    if (!connected || socket_fd < 0)
    {
        return false;
    }

    ssize_t sent = ::send(socket_fd, data, size, 0);
    if (sent < 0)
    {
        return false;
    }
    return sent == static_cast<ssize_t>(size);
}

size_t UDPTransport::receive(uint8_t* buffer, size_t buffer_size)
{
    if (!connected || socket_fd < 0) return false;

    ssize_t received = ::recv(socket_fd, buffer, buffer_size, 0);
    if (received < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 0;
        }
        return 0;
    }

    return static_cast<size_t>(received);
}

void UDPTransport::close()
{
    if (socket_fd >= 0)
    {
        ::close(socket_fd);
        socket_fd = -1;
        connected = false;
    }
}

bool OSCClient::send_message(const Message& msg)
{
    auto data = msg.encode();
    return send_packet(data.data(), data.size());
}

bool OSCClient::send_packet(const uint8_t* data, size_t size)
{
    return transport->send(data, size);
}

bool OSCServer::process_one()
{
    size_t received = transport->receive(buffer.data(), buffer.size());
    if (received == 0) return false;

    try
    {
        auto msg = Message::decode(buffer.data(), received);
        if (msg_handler) msg_handler(msg);
        return true;
    }
    catch (const std::exception& e)
    {
        return false;
    }
}

int OSCServer::process_all()
{
    int count = 0;
    while (process_one())
    {
        count++;
    }
    return count;
}

}  // namespace NanoOsc
