#ifndef NANOOSC_UDP_TRANSPORT_HPP
#define NANOOSC_UDP_TRANSPORT_HPP

#include "nanoosc/runtime.hpp"

#if defined(NANOOSC_ENABLE_RUNTIME_STATS)
#include "nanoosc/runtime_stats.hpp"
#endif

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

#if defined(NANOOSC_ENABLE_RUNTIME_STATS)
class RuntimeStatsUDPTransport final : public Transport, public OsDropProvider
{
public:
    RuntimeStatsUDPTransport(const std::string& host, uint16_t port);
    explicit RuntimeStatsUDPTransport(uint16_t port);
    RuntimeStatsUDPTransport(const RuntimeStatsUDPTransport&)            = delete;
    RuntimeStatsUDPTransport& operator=(const RuntimeStatsUDPTransport&) = delete;
    RuntimeStatsUDPTransport(RuntimeStatsUDPTransport&&)                 = delete;
    RuntimeStatsUDPTransport& operator=(RuntimeStatsUDPTransport&&)      = delete;
    ~RuntimeStatsUDPTransport() override;

    bool   send(const uint8_t* data, size_t size) override;
    size_t receive(uint8_t* buffer, size_t buffer_size) override;
    bool   is_ready() const override { return m_connected; }
    void   close() override;

    OsDropDelta sample() noexcept override;
    bool        os_drops_supported() const noexcept { return m_os_drop_scope != OsDropScope::Unavailable; }

private:
    bool setup_client();
    bool setup_server();
    void enable_per_socket_os_drops() noexcept;
    void record_linux_rxq_overflow(uint32_t kernel_value) noexcept;

    int         m_socket_fd {-1};
    std::string m_host;
    int         m_port {0};
    bool        m_connected {false};

    uint64_t    m_pending_os_drops {0};
    uint32_t    m_last_rxq_ovfl_value {0};
    bool        m_seen_os_drops {false};
    OsDropScope m_os_drop_scope {OsDropScope::Unavailable};
};

class BsdHostUdpProvider final : public OsDropProvider
{
public:
    BsdHostUdpProvider();

    OsDropDelta sample() noexcept override;
    bool        is_supported() const noexcept { return m_supported; }

private:
    uint32_t m_last_value {0};
    bool     m_supported {false};
};
#endif

} // namespace NanoOsc

#endif // NANOOSC_UDP_TRANSPORT_HPP
