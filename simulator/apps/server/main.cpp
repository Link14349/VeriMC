#include "server.hpp"
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    try {
        unsigned long port = 28765;
        if (argc > 1) port = std::stoul(argv[1]);
        if (port == 0 || port > 65535) throw std::invalid_argument("端口必须为 1–65535");
        return runServer(static_cast<unsigned short>(port));
    } catch (const std::exception& error) { std::cerr << "Simulator: " << error.what() << '\n'; return 1; }
}
