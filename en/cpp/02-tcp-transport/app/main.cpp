/**
 * Lesson 2: TCP Transport
 * Establishes TCP connections and handles connection events.
 *
 * This lesson builds on Lesson 1 by adding:
 * - Listening on a TCP port
 * - Parsing remote peer addresses from environment
 * - Dialing remote peers
 * - Handling connection events
 */

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>
#include <sstream>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/signal_set.hpp>

#include <libp2p/host/host.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multiaddress.hpp>
#include <libp2p/peer/peer_info.hpp>

namespace {
  // Logging configuration (YAML format for soralog)
  const std::string logger_config(R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: main
    sink: console
    level: info
    children:
      - name: libp2p
# ----------------
  )");

  // Helper function to split string by delimiter
  std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::istringstream stream(str);
    std::string token;
    while (std::getline(stream, token, delimiter)) {
      // Trim whitespace
      size_t start = token.find_first_not_of(" \t\n\r");
      size_t end = token.find_last_not_of(" \t\n\r");
      if (start != std::string::npos && end != std::string::npos) {
        tokens.push_back(token.substr(start, end - start + 1));
      }
    }
    return tokens;
  }

  // Helper function to get environment variable with default
  std::string getEnv(const std::string& name, const std::string& defaultValue = "") {
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : defaultValue;
  }

}  // namespace

int main(int argc, char** argv) {
    std::cout << "Starting Universal Connectivity Application..." << std::endl;

    // Step 1: Initialize logging system
    auto logging_system = std::make_shared<soralog::LoggingSystem>(
        std::make_shared<soralog::ConfiguratorFromYAML>(
            std::make_shared<libp2p::log::Configurator>(),
            logger_config));
    auto r = logging_system->configure();
    if (r.has_error) {
        std::cerr << r.message << std::endl;
        return EXIT_FAILURE;
    }
    libp2p::log::setLoggingSystem(logging_system);
    libp2p::log::setLevelOfGroup("main", soralog::Level::INFO);

    auto log = libp2p::log::createLogger("Lesson2");

    // Step 2: Parse remote peer addresses from environment variable
    std::vector<libp2p::multi::Multiaddress> remote_addrs;
    std::string remote_peers_env = getEnv("REMOTE_PEERS", "");

    if (!remote_peers_env.empty()) {
        auto addr_strings = split(remote_peers_env, ',');
        for (const auto& addr_str : addr_strings) {
            auto addr_result = libp2p::multi::Multiaddress::create(addr_str);
            if (addr_result.has_value()) {
                remote_addrs.push_back(addr_result.value());
                log->info("Parsed remote address: {}", addr_str);
            } else {
                log->error("Failed to parse multiaddress: {}", addr_str);
            }
        }
    }

    // Step 3: Get listening port from environment
    std::string listen_port = getEnv("LISTEN_PORT", "9000");
    std::string listen_addr_str = "/ip4/0.0.0.0/tcp/" + listen_port;

    // Step 4: Create host using dependency injection
    auto injector = libp2p::injector::makeHostInjector();
    auto host = injector.create<std::shared_ptr<libp2p::Host>>();
    auto io_context = injector.create<std::shared_ptr<boost::asio::io_context>>();

    // Step 5: Get and print the peer ID
    auto peer_id = host->getId();
    std::cout << "Local peer id: " << peer_id.toBase58() << std::endl;

    // Step 6: Set up signal handling for graceful shutdown
    boost::asio::signal_set signals(*io_context, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
        std::cout << "Shutting down..." << std::endl;
        io_context->stop();
    });

    // Step 7: Start listening and connect to peers
    boost::asio::post(*io_context, [&] {
        // Parse listen address
        auto listen_addr_result = libp2p::multi::Multiaddress::create(listen_addr_str);
        if (!listen_addr_result.has_value()) {
            log->error("Failed to parse listen address: {}", listen_addr_str);
            io_context->stop();
            return;
        }
        auto listen_addr = listen_addr_result.value();

        // Start the host
        auto listen_result = host->listen(listen_addr);
        if (!listen_result) {
            log->error("Failed to listen on {}: {}", listen_addr_str,
                       listen_result.error().message());
            io_context->stop();
            return;
        }

        std::cout << "Listening on: " << listen_addr.getStringAddress() << std::endl;
        log->info("Host started with PeerId: {}", peer_id.toBase58());

        // Print all listening addresses
        for (const auto& addr : host->getAddresses()) {
            std::cout << "Address: " << addr.getStringAddress() << std::endl;
        }

        // Connect to remote peers
        for (const auto& addr : remote_addrs) {
            std::cout << "Attempting to connect to: " << addr.getStringAddress() << std::endl;

            // Extract peer ID from multiaddress if present
            auto peer_id_str = addr.getPeerId();
            if (!peer_id_str.has_value()) {
                std::cout << "Invalid multiaddress " << addr.getStringAddress()
                          << ": Missing /p2p/<peer_id>" << std::endl;
                continue;
            }

            auto remote_peer_id_result = libp2p::peer::PeerId::fromBase58(peer_id_str.value());
            if (!remote_peer_id_result.has_value()) {
                std::cout << "Failed to parse peer ID from: " << addr.getStringAddress() << std::endl;
                continue;
            }

            auto remote_peer_id = remote_peer_id_result.value();
            libp2p::peer::PeerInfo peer_info{remote_peer_id, {addr}};

            host->connect(
                peer_info,
                [&, addr, remote_peer_id](auto&& conn_result) {
                    if (conn_result.has_value()) {
                        std::cout << "Connected to: " << remote_peer_id.toBase58()
                                  << " via " << addr.getStringAddress() << std::endl;
                    } else {
                        std::cout << "Failed to connect to " << addr.getStringAddress()
                                  << ": " << conn_result.error().message() << std::endl;
                    }
                }
            );
        }

        std::cout << "Waiting for connections..." << std::endl;
    });

    // Step 8: Run the event loop
    try {
        io_context->run();
    } catch (const std::exception& e) {
        log->error("Error: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
