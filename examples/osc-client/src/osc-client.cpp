#include "nano-osc.hpp"
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

int main(int argc, char* argv[])
{
    std::cout << "Creating OSCClient..." << std::endl;

    auto t                    = std::make_unique<NanoOsc::UDPTransport>("127.0.0.1", 9000);
    NanoOsc::OSCClient client = NanoOsc::OSCClient(std::move(t));
    auto msg                  = NanoOsc::Message("/test");
    msg.add_int32(-1);
    msg.add_float(-0.5f);
    msg.add_string("string");
    std::vector<uint8_t> blob = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    msg.add_blob(blob.data(), blob.size());

    for (;;)
    {
        std::cout << "sending message to port 9001\n";
        auto sent = client.send_message(msg);
        std::cout << "message sent: " << sent << "\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
