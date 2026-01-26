/**
 * Lesson 5: Identify Checkpoint
 * Implements the Identify protocol for peer capability discovery.
 *
 * This lesson builds on Lesson 4 by adding:
 * - Identify protocol for exchanging peer information
 * - Capability discovery (protocols, versions, addresses)
 * - Observed address detection
 *
 * The Identify protocol serves four core purposes:
 * 1. Capability Discovery - Learn what protocols a peer supports
 * 2. Version Information - Exchange software version and agent strings
 * 3. Address Discovery - Learn how peers see your external addresses
 * 4. Protocol Negotiation - Establish common protocols for communication
 */

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>
#include <sstream>
#include <chrono>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/signal_set.hpp>

#include <libp2p/host/host.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multiaddress.hpp>
#include <libp2p/peer/peer_info.hpp>
#include <libp2p/protocol/ping.hpp>
#include <libp2p/protocol/identify/identify.hpp>
#include <libp2p/protocol/identify/identify_msg_processor.hpp>
#include <libp2p/event/bus.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/crypto/random_generator/boost_generator.hpp>
#include <libp2p/crypto/key_marshaller/key_marshaller_impl.hpp>
#include <libp2p/network/connection_manager.hpp>
#include <libp2p/peer/identity_manager.hpp>

namespace {
  const std::string logger_config(R"(
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
  )");

  std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::istringstream stream(str);
    std::string token;
    while (std::getline(stream, token, delimiter)) {
      size_t start = token.find_first_not_of(" \t\n\r");
      size_t end = token.find_last_not_of(" \t\n\r");
      if (start != std::string::npos && end != std::string::npos) {
        tokens.push_back(token.substr(start, end - start + 1));
      }
    }
    return tokens;
  }

  std::string getEnv(const std::string& name, const std::string& defaultValue = "") {
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : defaultValue;
  }

  // Detect transport type from multiaddress
  std::string getTransportType(const libp2p::multi::Multiaddress& addr) {
    std::string addr_str(addr.getStringAddress());
    if (addr_str.find("/quic") != std::string::npos) {
      return "QUIC";
    } else if (addr_str.find("/tcp/") != std::string::npos) {
      return "TCP";
    }
    return "Unknown";
  }
}  // namespace

int main(int argc, char** argv) {
    std::cout << "Starting Universal Connectivity Application..." << std::endl;

    // Initialize logging
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
    auto log = libp2p::log::createLogger("Lesson5");

    // Parse remote peer addresses
    std::vector<libp2p::multi::Multiaddress> remote_addrs;
    std::string remote_peers_env = getEnv("REMOTE_PEERS", "");
    if (!remote_peers_env.empty()) {
        for (const auto& addr_str : split(remote_peers_env, ',')) {
            auto addr_result = libp2p::multi::Multiaddress::create(addr_str);
            if (addr_result.has_value()) {
                remote_addrs.push_back(addr_result.value());
                log->info("Parsed remote address: {} ({})", addr_str,
                         getTransportType(addr_result.value()));
            }
        }
    }

    // Get listening ports
    std::string tcp_port = getEnv("LISTEN_PORT", "9000");
    std::string quic_port = getEnv("QUIC_PORT", "9001");

    // Build multiaddresses for both transports
    std::string tcp_addr_str = "/ip4/0.0.0.0/tcp/" + tcp_port;
    std::string quic_addr_str = "/ip4/0.0.0.0/udp/" + quic_port + "/quic-v1";

    // Create host using dependency injection
    auto injector = libp2p::injector::makeHostInjector();
    auto host = injector.create<std::shared_ptr<libp2p::Host>>();
    auto io_context = injector.create<std::shared_ptr<boost::asio::io_context>>();
    auto bus = injector.create<std::shared_ptr<libp2p::event::Bus>>();
    auto conn_manager = injector.create<std::shared_ptr<libp2p::network::ConnectionManager>>();
    auto identity_manager = injector.create<std::shared_ptr<libp2p::peer::IdentityManager>>();
    auto key_marshaller = injector.create<std::shared_ptr<libp2p::crypto::marshaller::KeyMarshaller>>();

    // Create scheduler for ping protocol
    auto scheduler_backend = std::make_shared<libp2p::basic::AsioSchedulerBackend>(io_context);
    auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
        scheduler_backend,
        libp2p::basic::Scheduler::Config{});

    // Create random generator for ping
    auto random_gen = std::make_shared<libp2p::crypto::random::BoostRandomGenerator>();

    // Configure ping with 1-second interval and 5-second timeout
    libp2p::protocol::PingConfig ping_config;
    ping_config.interval = std::chrono::seconds(1);
    ping_config.timeout = std::chrono::seconds(5);
    ping_config.message_size = 32;

    // Create ping protocol
    auto ping = std::make_shared<libp2p::protocol::Ping>(
        *host, *bus, scheduler, random_gen, ping_config);

    // Create Identify message processor
    auto identify_msg_processor = std::make_shared<libp2p::protocol::IdentifyMessageProcessor>(
        *host, *conn_manager, *identity_manager, key_marshaller);

    // Create Identify protocol
    libp2p::protocol::IdentifyConfig identify_config;
    auto identify = std::make_shared<libp2p::protocol::Identify>(
        identify_config, *host, identify_msg_processor, *bus);

    // Subscribe to identify events
    auto identify_connection = identify->onIdentifyReceived(
        [log](const libp2p::peer::PeerId& peer_id) {
            std::cout << "Identify received from: " << peer_id.toBase58() << std::endl;
        });

    auto peer_id = host->getId();
    std::cout << "Local peer id: " << peer_id.toBase58() << std::endl;
    std::cout << "Agent version: universal-connectivity/0.1.0" << std::endl;
    std::cout << "Protocol version: /ipfs/0.1.0" << std::endl;

    // Signal handling
    boost::asio::signal_set signals(*io_context, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
        std::cout << "Shutting down..." << std::endl;
        io_context->stop();
    });

    // Start host and set up protocols
    boost::asio::post(*io_context, [&] {
        // Listen on TCP
        auto tcp_addr = libp2p::multi::Multiaddress::create(tcp_addr_str).value();
        auto tcp_result = host->listen(tcp_addr);
        if (!tcp_result) {
            log->error("Failed to listen on TCP: {}", tcp_result.error().message());
            io_context->stop();
            return;
        }
        std::cout << "Listening on TCP: " << tcp_addr.getStringAddress() << std::endl;

        // Listen on QUIC
        auto quic_addr = libp2p::multi::Multiaddress::create(quic_addr_str).value();
        auto quic_result = host->listen(quic_addr);
        if (!quic_result) {
            log->warn("Failed to listen on QUIC: {} (QUIC may not be available)",
                     quic_result.error().message());
        } else {
            std::cout << "Listening on QUIC: " << quic_addr.getStringAddress() << std::endl;
        }

        // Set up ping protocol handler
        host->setProtocolHandler(
            {ping->getProtocolId()},
            [ping](libp2p::StreamAndProtocol stream) {
                ping->handle(std::move(stream));
            });

        // Set up identify protocol handler
        host->setProtocolHandler(
            {identify->getProtocolId()},
            [identify](libp2p::StreamAndProtocol stream) {
                identify->handle(std::move(stream));
            });

        // Start identify protocol (watches for new connections)
        identify->start();

        std::cout << "Protocols:" << std::endl;
        std::cout << "  - Ping: " << ping->getProtocolId() << std::endl;
        std::cout << "  - Identify: " << identify->getProtocolId() << std::endl;
        std::cout << "Transports: TCP + QUIC" << std::endl;

        // Connect to remote peers
        for (const auto& addr : remote_addrs) {
            auto peer_id_str = addr.getPeerId();
            if (!peer_id_str) {
                std::cout << "Invalid address (missing peer ID): " << addr.getStringAddress() << std::endl;
                continue;
            }

            auto remote_peer_id = libp2p::peer::PeerId::fromBase58(peer_id_str.value()).value();
            libp2p::peer::PeerInfo peer_info{remote_peer_id, {addr}};

            std::string transport = getTransportType(addr);

            std::cout << "Connecting via " << transport << " to: " << addr.getStringAddress() << std::endl;

            host->connect(
                peer_info,
                [&, addr, remote_peer_id, ping, log, transport](auto&& conn_result) {
                    if (!conn_result.has_value()) {
                        std::cout << "Failed to connect via " << transport << ": "
                                  << conn_result.error().message() << std::endl;
                        return;
                    }

                    std::cout << "Connected via " << transport << " to: "
                              << remote_peer_id.toBase58() << std::endl;

                    // Start pinging the connected peer
                    auto conn = conn_result.value();
                    ping->startPinging(
                        conn,
                        [remote_peer_id, log, transport](auto&& ping_result) {
                            if (ping_result.has_value()) {
                                std::cout << "Ping session started (" << transport << ") with: "
                                          << remote_peer_id.toBase58() << std::endl;
                            } else {
                                std::cout << "Failed to start ping: "
                                          << ping_result.error().message() << std::endl;
                            }
                        });
                });
        }

        std::cout << "Waiting for connections..." << std::endl;
    });

    // Subscribe to ping events
    auto& ping_channel = bus->getChannel<libp2p::event::protocol::PeerIsDeadChannel>();
    auto ping_sub = ping_channel.subscribe([](const libp2p::peer::PeerId& peer) {
        std::cout << "Ping timeout - peer is dead: " << peer.toBase58() << std::endl;
    });

    // Run the event loop
    try {
        io_context->run();
    } catch (const std::exception& e) {
        log->error("Error: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
