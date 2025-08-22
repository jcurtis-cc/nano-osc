#ifndef NANO_OSC_HPP
#define NANO_OSC_HPP

#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <system_error>
#include <vector>

namespace NanoOsc {

const int BUFFER_MAX_SIZE               = 65536;
constexpr std::array<char, 8> BUNDLE_ID = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0};

using OSCInt     = int32_t;
using OSCInt64   = int64_t;
using OSCTimeTag = uint64_t;
using OSCFloat   = float;
using OSCString  = std::string;
using OSCBlob    = std::vector<uint8_t>;
using OSCValue   = std::variant<OSCInt, OSCInt64, OSCFloat, OSCString, OSCBlob, OSCTimeTag>;

class Message
{
public:
    std::string address;
    std::string tags;
    std::vector<OSCValue> arguments;

    explicit Message(const std::string& addr) : address(addr)
    {
        tags.push_back(',');
    }

    void clear()
    {
        tags.assign(1, ',');
        arguments.clear();
    }
    void add_int32(int32_t value)
    {
        tags.push_back('i');
        arguments.emplace_back(value);
    }
    void add_float(float value)
    {
        tags.push_back('f');
        arguments.emplace_back(value);
    }
    void add_string(const std::string& value)
    {
        tags.push_back('s');
        arguments.emplace_back(value);
    }
    void add_blob(const uint8_t* data, size_t size)
    {
        tags.push_back('b');
        arguments.emplace_back(OSCBlob(data, data + size));
    }

    std::vector<uint8_t> encode() const;
    static Message decode(const uint8_t* data, size_t size);
};

class Bundle
{
public:
    std::vector<Message> messages;
    std::vector<Bundle> bundles;
    OSCTimeTag timetag {1};

    Bundle() = default;

    std::vector<uint8_t> encode() const;
    static Bundle decode(const uint8_t* data, size_t size);
};

class Transport
{
public:
    Transport()                            = default;
    Transport(const Transport&)            = delete;
    Transport(Transport&&)                 = delete;
    Transport& operator=(const Transport&) = delete;
    Transport& operator=(Transport&&)      = delete;
    virtual ~Transport()                   = default;

    virtual bool send(const uint8_t* data, size_t size)         = 0;
    virtual size_t receive(uint8_t* buffer, size_t buffer_size) = 0;
    virtual bool is_ready() const                               = 0;
    virtual void close()                                        = 0;
};

class UDPTransport final : public Transport
{
public:
    UDPTransport(const std::string& host, uint16_t port)
        : socket_fd(-1), host(host), port(port), is_server(false), connected(false)
    {
        if (!setup_client())
        {
            throw std::system_error(errno, std::generic_category(), "UDP client setup failed");
        }
    }
    explicit UDPTransport(uint16_t port) : socket_fd(-1), port(port), is_server(true), connected(false)
    {
        if (!setup_server())
        {
            throw std::system_error(errno, std::generic_category(), "UDP server setup failed");
        }
    }
    UDPTransport(const UDPTransport&)            = delete;
    UDPTransport& operator=(const UDPTransport&) = delete;
    UDPTransport(UDPTransport&&)                 = delete;
    UDPTransport& operator=(UDPTransport&&)      = delete;
    ~UDPTransport() override
    {
        close();
    }

    bool send(const uint8_t* data, size_t size) override;
    size_t receive(uint8_t* buffer, size_t buffer_size) override;
    bool is_ready() const override
    {
        return connected;
    }
    void close() override;

private:
    bool setup_client();
    bool setup_server();

    int socket_fd;
    std::string host;
    int port;

    bool is_server;
    bool connected;
};

class OSCClient
{
public:
    explicit OSCClient(std::unique_ptr<Transport> transport) : transport(std::move(transport)), buffer(BUFFER_MAX_SIZE)
    {}

    bool send_message(const Message& msg);
    bool send_packet(const uint8_t* data, size_t size);

private:
    std::unique_ptr<Transport> transport;
    std::vector<uint8_t> buffer;
};

class OSCServer
{
public:
    explicit OSCServer(std::unique_ptr<Transport> transport) : transport(std::move(transport)), buffer(BUFFER_MAX_SIZE)
    {}

    using MessageHandler = std::function<void(const Message&)>;
    using BundleHandler  = std::function<void(const Bundle&)>;

    void set_message_handler(MessageHandler handler)
    {
        msg_handler = handler;
    }

    void set_bundle_handler(BundleHandler handler)
    {
        bundle_handler = handler;
    }

    // Non blocking
    bool process_one();
    // Blocking
    int process_all();

private:
    std::unique_ptr<Transport> transport;
    std::vector<uint8_t> buffer;
    MessageHandler msg_handler;
    BundleHandler bundle_handler;
};

namespace detail {
inline size_t align4(size_t n)
{
    return (4 - (n & 3)) & 3;
}
inline void add_u32_be(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}
inline void add_osc_int32(std::vector<uint8_t>& out, int32_t v)
{
    add_u32_be(out, static_cast<uint32_t>(v));
}
inline void add_osc_float(std::vector<uint8_t>& out, float f)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof bits);
    add_u32_be(out, bits);
}
inline void add_osc_string(std::vector<uint8_t>& out, const std::string& s)
{
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0x00);
    size_t pad = align4(s.size() + 1);
    out.insert(out.end(), pad, uint8_t(0x00));
}
inline void add_osc_blob(std::vector<uint8_t>& out, const uint8_t* data, size_t size)
{
    add_u32_be(out, static_cast<uint32_t>(size));
    out.insert(out.end(), data, data + size);
    size_t pad = align4(size);
    out.insert(out.end(), pad, uint8_t(0x00));
}
inline uint32_t read_u32_be(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
inline uint64_t read_u64_be(const uint8_t* p)
{
    return (uint64_t(p[0]) << 56) | (uint64_t(p[1]) << 48) | (uint64_t(p[2]) << 40) | (uint64_t(p[3]) << 32) |
           (uint64_t(p[4]) << 24) | (uint64_t(p[5]) << 16) | (uint64_t(p[6]) << 8) | (uint64_t(p[7]));
}
inline bool is_bundle(const uint8_t* p)
{
    return std::memcmp(p, BUNDLE_ID.data(), 8) == 0;
}
inline int32_t read_osc_int32(const uint8_t* p, size_t& offset)
{
    auto i  = static_cast<int32_t>(read_u32_be(p + offset));
    offset += 4;
    return i;
}
inline int64_t read_osc_int64(const uint8_t* p, size_t& offset)
{
    auto i  = static_cast<int64_t>(read_u64_be(p + offset));
    offset += 8;
    return i;
}
inline float read_osc_float32(const uint8_t* p, size_t& offset)
{
    uint32_t bits = read_u32_be(p + offset);
    float f       = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    offset += 4;
    return f;
}
inline uint64_t read_osc_timetag(const uint8_t* p, size_t& offset)
{
    auto i  = read_u64_be(p + offset);
    offset += 8;
    return i;
}
inline bool read_osc_string(std::string& out, const uint8_t* data, size_t size, size_t& offset)
{
    size_t start = offset;
    while (offset < size && data[offset] != 0x00) ++offset;
    if (offset >= size) throw std::runtime_error("OSC String is not terminated");
    out.assign(reinterpret_cast<const char*>(data + start), offset - start);
    offset = (offset + 4) & ~0x3;
    return true;
}
inline bool read_osc_blob(std::vector<uint8_t>& out, const uint8_t* data, size_t size, size_t& offset)
{
    if (offset + 4 > size) return false;
    uint32_t len  = read_u32_be(data + offset);
    offset       += 4;
    if (offset + len > size) return false;
    out.assign(data + offset, data + offset + len);
    offset += len;
    offset  = (offset + 4) & ~0x3;
    return true;
}

}  // namespace detail

}  // namespace NanoOsc

#endif  // NANO_OSC_HPP
