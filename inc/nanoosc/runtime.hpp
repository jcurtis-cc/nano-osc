#ifndef NANOOSC_RUNTIME_HPP
#define NANOOSC_RUNTIME_HPP

#include "nanoosc/core.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace NanoOsc {

class Transport
{
public:
    Transport()                            = default;
    Transport(const Transport&)            = delete;
    Transport(Transport&&)                 = delete;
    Transport& operator=(const Transport&) = delete;
    Transport& operator=(Transport&&)      = delete;
    virtual ~Transport()                   = default;

    virtual bool   send(const uint8_t* data, size_t size)       = 0;
    virtual size_t receive(uint8_t* buffer, size_t buffer_size) = 0;
    virtual bool   is_ready() const                             = 0;
    virtual void   close()                                      = 0;
};

class OSCClient
{
public:
    explicit OSCClient(std::unique_ptr<Transport> transport)
        : m_transport(std::move(transport)), m_buffer(BUFFER_MAX_SIZE)
    {}

    bool send_message(const Message& msg);
    bool send_bundle(const Bundle& bundle);
    bool send_packet(const uint8_t* data, size_t size);

private:
    std::unique_ptr<Transport> m_transport;
    std::vector<uint8_t>       m_buffer;
};

class OSCServer
{
public:
    explicit OSCServer(std::unique_ptr<Transport> transport)
        : m_transport(std::move(transport)), m_buffer(BUFFER_MAX_SIZE)
    {}

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

    bool process_one();
    int  process_all(int max = -1);

private:
    std::unique_ptr<Transport> m_transport;
    std::vector<uint8_t>       m_buffer;
    Message                    m_msg;
    Bundle                     m_bundle;
    MessageHandler             m_msg_handler;
    BundleHandler              m_bundle_handler;
    MessageViewHandler         m_msg_view_handler;
    BundleViewHandler          m_bundle_view_handler;
    ErrorHandler               m_error_handler;
};

} // namespace NanoOsc

#endif // NANOOSC_RUNTIME_HPP
