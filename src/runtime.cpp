#include "nanoosc/runtime.hpp"

namespace NanoOsc {

bool OSCClient::send_message(const Message& msg)
{
    size_t written = 0;
    if (!msg.encode_into(m_buffer.data(), m_buffer.size(), written)) return false;
    return m_transport->send(m_buffer.data(), written);
}

bool OSCClient::send_bundle(const Bundle& bundle)
{
    size_t written = 0;
    if (!bundle.encode_into(m_buffer.data(), m_buffer.size(), written)) return false;
    return m_transport->send(m_buffer.data(), written);
}

bool OSCClient::send_packet(const uint8_t* data, size_t size)
{
    return m_transport->send(data, size);
}

bool OSCServer::process_one()
{
    size_t received = m_transport->receive(m_buffer.data(), m_buffer.size());
    if (received == 0) return false;

    using namespace detail;
    if (is_bundle(m_buffer.data(), received))
    {
        BundleView bv;
        if (!decode_bundle_view(bv, m_buffer.data(), received))
        {
            if (m_error_handler) m_error_handler(m_buffer.data(), received);
            return false;
        }
        if (!validate_bundle_view(bv))
        {
            if (m_error_handler) m_error_handler(m_buffer.data(), received);
            return false;
        }
        if (m_bundle_view_handler)
        {
            m_bundle_view_handler(bv);
        }
        else if (m_bundle_handler)
        {
            if (!m_bundle.assign(bv))
            {
                if (m_error_handler) m_error_handler(m_buffer.data(), received);
                return false;
            }
            m_bundle_handler(m_bundle);
        }
        return true;
    }

    MessageView mv;
    if (!decode_message_view(mv, m_buffer.data(), received))
    {
        if (m_error_handler) m_error_handler(m_buffer.data(), received);
        return false;
    }
    if (!validate_message_view(mv))
    {
        if (m_error_handler) m_error_handler(m_buffer.data(), received);
        return false;
    }
    if (m_msg_view_handler)
    {
        m_msg_view_handler(mv);
    }
    else if (m_msg_handler)
    {
        if (!m_msg.assign(mv))
        {
            if (m_error_handler) m_error_handler(m_buffer.data(), received);
            return false;
        }
        m_msg_handler(m_msg);
    }
    return true;
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
