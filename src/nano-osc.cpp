#include "nano-osc.hpp"
#include <cerrno>
#include <cstddef>
#include <iomanip>
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
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, OSCInt>)
                {
                    add_osc_int32(buffer, value);
                }
                else if constexpr (std::is_same_v<T, OSCInt64>)
                {
                    add_osc_int64(buffer, value);
                }
                else if constexpr (std::is_same_v<T, OSCFloat>)
                {
                    add_osc_float32(buffer, value);
                }
                else if constexpr (std::is_same_v<T, OSCFloat64>)
                {
                    add_osc_float64(buffer, value);
                }
                else if constexpr (std::is_same_v<T, OSCString>)
                {
                    add_osc_string(buffer, value);
                }
                else if constexpr (std::is_same_v<T, OSCBlob>)
                {
                    add_osc_blob(buffer, value.data(), value.size());
                }
                else if constexpr (std::is_same_v<T, OSCTimeTag>)
                {
                    add_osc_u64(buffer, value);
                }
            },
            arg
        );
    }
    return buffer;
}

namespace {

bool try_decode_message(Message& out, const uint8_t* data, size_t size) noexcept
{
    using namespace detail;
    std::string addr;
    std::string tagstr;
    size_t      offset = 0;
    if (!read_osc_string(addr, data, size, offset)) return false;
    if (!read_osc_string(tagstr, data, size, offset)) return false;

    out.address = std::move(addr);
    out.tags    = std::move(tagstr);
    out.arguments.clear();

    for (size_t i = 1; i < out.tags.size(); ++i)
    {
        switch (out.tags[i])
        {
            case 'i': {
                int32_t v;
                if (!read_osc_int32(v, data, size, offset)) return false;
                out.arguments.emplace_back(v);
                break;
            }
            case 'f': {
                float v;
                if (!read_osc_float32(v, data, size, offset)) return false;
                out.arguments.emplace_back(v);
                break;
            }
            case 'S':
            case 's': {
                std::string s;
                if (!read_osc_string(s, data, size, offset)) return false;
                out.arguments.emplace_back(std::move(s));
                break;
            }
            case 'b': {
                std::vector<uint8_t> b;
                if (!read_osc_blob(b, data, size, offset)) return false;
                out.arguments.emplace_back(std::move(b));
                break;
            }
            case 'h': {
                int64_t v;
                if (!read_osc_int64(v, data, size, offset)) return false;
                out.arguments.emplace_back(v);
                break;
            }
            case 't': {
                uint64_t v;
                if (!read_osc_timetag(v, data, size, offset)) return false;
                out.arguments.emplace_back(OSCTimeTag {v});
                break;
            }
            case 'd': {
                double v;
                if (!read_osc_float64(v, data, size, offset)) return false;
                out.arguments.emplace_back(v);
                break;
            }
            case 'c': {
                // an ascii character, sent as 32 bit
                if (!ensure(size, offset, 4)) return false;
                offset += 4;
                break;
            }
            case 'r': {
                // 32 bit RGBA color
                if (!ensure(size, offset, 4)) return false;
                offset += 4;
                break;
            }
            case 'm': {
                // 4 byte MIDI message. Bytes from MSB to LSB are: port id, status byte, data1, data2
                if (!ensure(size, offset, 4)) return false;
                offset += 4;
                break;
            }
            default:
                break;
        }
    }
    return true;
}

} // namespace

Message Message::decode(const uint8_t* data, size_t size)
{
    Message msg("");
    if (!try_decode_message(msg, data, size))
    {
        throw std::runtime_error("OSC message decode failed");
    }
    return msg;
}

std::vector<uint8_t> Bundle::encode() const
{
    using namespace detail;
    std::vector<uint8_t> buffer;
    buffer.reserve(512);
    add_osc_string(buffer, std::string_view {BUNDLE_ID.data(), 7});
    add_osc_u64(buffer, timetag);

    for (const auto& msg : messages)
    {
        auto m   = msg.encode();
        auto len = m.size();
        add_osc_u32(buffer, len);
        buffer.insert(buffer.end(), m.data(), m.data() + len);
    }

    for (const auto& bundle : bundles)
    {
        auto b   = bundle.encode();
        auto len = b.size();
        add_osc_u32(buffer, len);
        buffer.insert(buffer.end(), b.data(), b.data() + len);
    }

    return buffer;
}

namespace {

bool try_decode_bundle(Bundle& out, const uint8_t* data, size_t size) noexcept
{
    using namespace detail;
    if (!is_bundle(data, size)) return false;
    size_t   offset = 8;
    uint64_t tt;
    if (!read_osc_timetag(tt, data, size, offset)) return false;

    out.timetag = OSCTimeTag {tt};
    out.messages.clear();
    out.bundles.clear();

    while (offset < size)
    {
        if (!ensure(size, offset, 4)) return false;
        uint32_t len  = read_u32_be(data + offset);
        offset       += 4;
        if (!ensure(size, offset, len)) return false;

        if (is_bundle(data + offset, len))
        {
            Bundle child;
            if (!try_decode_bundle(child, data + offset, len)) return false;
            out.bundles.emplace_back(std::move(child));
        }
        else
        {
            Message child("");
            if (!try_decode_message(child, data + offset, len)) return false;
            out.messages.emplace_back(std::move(child));
        }
        offset += len;
    }

    return true;
}

} // namespace

Bundle Bundle::decode(const uint8_t* data, size_t size)
{
    Bundle b;
    if (!try_decode_bundle(b, data, size))
    {
        throw std::runtime_error("OSC bundle decode failed");
    }
    return b;
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
    memset(&server_addr, 0, sizeof(server_addr));
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
    if (!m_connected || m_socket_fd < 0) return false;

    ssize_t received = ::recv(m_socket_fd, buffer, buffer_size, 0);
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
    if (m_socket_fd >= 0)
    {
        ::close(m_socket_fd);
        m_socket_fd = -1;
        m_connected = false;
    }
}

bool OSCClient::send_message(const Message& msg)
{
    auto data = msg.encode();
    return send_packet(data.data(), data.size());
}

bool OSCClient::send_bundle(const Bundle& bundle)
{
    auto data = bundle.encode();
    return send_packet(data.data(), data.size());
}

bool OSCClient::send_packet(const uint8_t* data, size_t size)
{
    return m_transport->send(data, size);
}

bool OSCServer::process_one()
{
    size_t received = m_transport->receive(m_buffer.data(), m_buffer.size());
    if (received == 0) return false;

    try
    {
        using namespace detail;
        if (is_bundle(m_buffer.data(), received))
        {
            auto bundle = Bundle::decode(m_buffer.data(), received);
            if (m_bundle_handler) m_bundle_handler(bundle);
            return true;
        }
        auto msg = Message::decode(m_buffer.data(), received);
        if (m_msg_handler) m_msg_handler(msg);
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error processing OSC packet: " << e.what() << "\n";
        return false;
    }
}

int OSCServer::process_all(int max)
{
    int count = 0;
    while ((max < 0 || count < max) && process_one())
    {
        count++;
    }
    return count;
}

} // namespace NanoOsc
