#include "nano-osc.hpp"
#include <iomanip>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

int main(int argc, char* argv[])
{
    std::cout << "Creating OSCServer..." << std::endl;

    auto t                    = std::make_unique<NanoOsc::UDPTransport>(9000);
    NanoOsc::OSCServer server = NanoOsc::OSCServer(std::move(t));

	std::function<void(const NanoOsc::Message&)> msg_handler = [](const NanoOsc::Message& msg) {
		std::cout << msg.address << " tags: " << msg.tags; 
		for (const auto& arg : msg.arguments) {
			std::visit(
				[&](const auto& value)
				{
					using T = std::decay_t<decltype(value)>;
					if constexpr (std::is_same_v<T, NanoOsc::OSCBlob>)
					{
						auto f = std::cout.flags();
						for (uint8_t b : value) {
							std::cout << " 0x"
								<< std::hex << std::uppercase
								<< std::setw(2) << std::setfill('0')
								<< static_cast<unsigned>(b);
						}
						std::cout.flags(f);
						std::cout << " [" << value.size() << " bytes]";
					}
					else {
						std::cout << " " << value;
					}
				},
				arg
			);
		}
		std::cout << "\n";
	};

	server.set_message_handler(msg_handler);

    for (;;)
    {
        std::cout << "listening for message on port 9000\n";
        auto received = server.process_one();
        if (received) {
			std::cout << "message received: " << received << "\n";
		}
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
