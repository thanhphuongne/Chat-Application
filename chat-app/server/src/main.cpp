#include "chat_server.hpp"
#include <iostream>
#include <exception>
#include <boost/asio.hpp>

int main(int argc, char* argv[]) {
    try {
        short port = 8080;
        if (argc > 1) {
            port = static_cast<short>(std::atoi(argv[1]));
        }

        std::cout << "==============================================" << std::endl;
        std::cout << "      Asynchronous Chat Server Starting...   " << std::endl;
        std::cout << "==============================================" << std::endl;

        boost::asio::io_context io_context;

        // Set up signal handling for graceful shutdown (SIGINT, SIGTERM)
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait(
            [&io_context](const boost::system::error_code& /*ec*/, int signal) {
                std::cout << "\n[SERVER] Shutdown signal received (" << signal << "). Cleaning up and shutting down gracefully..." << std::endl;
                io_context.stop();
            });

        // Initialize chat server
        chat::chat_server server(io_context, port);

        std::cout << "[SERVER] Run loop active. Waiting for clients..." << std::endl;
        io_context.run();
        
        std::cout << "[SERVER] Graceful shutdown completed. Goodbye!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[CRITICAL] Exception caught in main: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
