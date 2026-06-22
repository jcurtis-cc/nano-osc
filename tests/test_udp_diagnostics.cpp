#include "doctest.h"
#include "nanoosc/udp_transport.hpp"

using namespace NanoOsc;

TEST_CASE("BsdHostUdpProvider is safe to sample")
{
    BsdHostUdpProvider host_provider;
    OsDropDelta        host_drops = host_provider.sample();
    if (host_provider.is_supported())
    {
        CHECK(host_drops.scope == OsDropScope::HostUdp);
    }
    else
    {
        CHECK(host_drops.scope == OsDropScope::Unavailable);
    }
}
