#ifndef NANOOSC_UDP_TRANSPORT_HPP
#define NANOOSC_UDP_TRANSPORT_HPP

#include "nanoosc/runtime.hpp"

#include <cstdint>
#include <string>

namespace NanoOsc {

class UDPTransport final : public Transport
{
public:
    UDPTransport(const std::string& host, uint16_t port);
    explicit UDPTransport(uint16_t port);
    UDPTransport(const UDPTransport&)            = delete;
    UDPTransport& operator=(const UDPTransport&) = delete;
    UDPTransport(UDPTransport&&)                 = delete;
    UDPTransport& operator=(UDPTransport&&)      = delete;
    ~UDPTransport() override;

    bool   send(const uint8_t* data, size_t size) override;
    size_t receive(uint8_t* buffer, size_t buffer_size) override;
    bool   is_ready() const override { return m_connected; }
    void   close() override;

private:
    bool setup_client();
    bool setup_server();

    int         m_socket_fd;
    std::string m_host;
    int         m_port;

    bool m_is_server;
    bool m_connected;
};

} // namespace NanoOsc

#endif // NANOOSC_UDP_TRANSPORT_HPP
