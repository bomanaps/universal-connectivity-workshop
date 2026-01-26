/**
 * Lesson 6: Gossipsub Checkpoint
 * Implements the Gossipsub protocol for publish-subscribe messaging.
 *
 * This lesson builds on Lesson 5 by adding:
 * - Gossipsub pub/sub protocol for topic-based messaging
 * - Message publishing and subscription
 * - Peer discovery through gossip
 *
 * Gossipsub is a scalable pub/sub protocol that:
 * - Uses mesh topology for efficient message delivery
 * - Supports topic-based message routing
 * - Provides message deduplication
 * - Enables peer-to-peer chat and notifications
 */

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>
#include <sstream>
#include <chrono>
#include <ctime>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>

#include <libp2p/host/host.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multiaddress.hpp>
#include <libp2p/peer/peer_info.hpp>
#include <libp2p/protocol/ping.hpp>
#include <libp2p/protocol/identify/identify.hpp>
#include <libp2p/protocol/identify/identify_msg_processor.hpp>
#include <libp2p/protocol/gossip/gossip.hpp>
#include <libp2p/event/bus.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/crypto/random_generator/boost_generator.hpp>
#include <libp2p/crypto/key_marshaller/key_marshaller_impl.hpp>
#include <libp2p/crypto/crypto_provider/crypto_provider_impl.hpp>
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

  // Topic for Universal Connectivity chat
  const std::string CHAT_TOPIC = "universal-connectivity";
  const std::string DISCOVERY_TOPIC = "universal-connectivity-browser-peer-discovery";

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

  std::string getTransportType(const libp2p::multi::Multiaddress& addr) {
    std::string addr_str(addr.getStringAddress());
    if (addr_str.find("/quic") != std::string::npos) {
      return "QUIC";
    } else if (addr_str.find("/tcp/") != std::string::npos) {
      return "TCP";
    }
    return "Unknown";
  }

  // Simple JSON message format for chat
  std::string createChatMessage(const std::string& sender_id, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::ostringstream json;
    json << "{\"sender\":\"" << sender_id << "\","
         << "\"message\":\"" << message << "\","
         << "\"timestamp\":" << timestamp << "}";
    return json.str();
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
    auto log = libp2p::log::createLogger("Lesson6");

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
    auto crypto_provider = injector.create<std::shared_ptr<libp2p::crypto::CryptoProvider>>();

    // Create scheduler
    auto scheduler_backend = std::make_shared<libp2p::basic::AsioSchedulerBackend>(io_context);
    auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
        scheduler_backend,
        libp2p::basic::Scheduler::Config{});

    // Create random generator
    auto random_gen = std::make_shared<libp2p::crypto::random::BoostRandomGenerator>();

    // Configure ping
    libp2p::protocol::PingConfig ping_config;
    ping_config.interval = std::chrono::seconds(1);
    ping_config.timeout = std::chrono::seconds(5);
    ping_config.message_size = 32;
    auto ping = std::make_shared<libp2p::protocol::Ping>(
        *host, *bus, scheduler, random_gen, ping_config);

    // Create Identify
    auto identify_msg_processor = std::make_shared<libp2p::protocol::IdentifyMessageProcessor>(
        *host, *conn_manager, *identity_manager, key_marshaller);
    libp2p::protocol::IdentifyConfig identify_config;
    auto identify = std::make_shared<libp2p::protocol::Identify>(
        identify_config, *host, identify_msg_processor, *bus);

    // Configure Gossipsub
    libp2p::protocol::gossip::Config gossip_config;
    gossip_config.D_min = 2;
    gossip_config.D_max = 4;
    gossip_config.heartbeat_interval_msec = std::chrono::milliseconds(1000);
    gossip_config.protocol_version = "/meshsub/1.0.0";
    gossip_config.sign_messages = true;

    // Create Gossipsub
    auto gossip = libp2p::protocol::gossip::create(
        scheduler, host, identity_manager, crypto_provider, key_marshaller, gossip_config);

    auto peer_id = host->getId();
    std::string peer_id_str = peer_id.toBase58();
    std::cout << "Local peer id: " << peer_id_str << std::endl;
    std::cout << "Agent version: universal-connectivity/0.1.0" << std::endl;

    // Signal handling
    boost::asio::signal_set signals(*io_context, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
        std::cout << "Shutting down..." << std::endl;
        gossip->stop();
        io_context->stop();
    });

    // Subscribe to gossip topics
    libp2p::protocol::Subscription chat_sub;
    libp2p::protocol::Subscription discovery_sub;

    // Start host and protocols
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
            log->warn("Failed to listen on QUIC: {}", quic_result.error().message());
        } else {
            std::cout << "Listening on QUIC: " << quic_addr.getStringAddress() << std::endl;
        }

        // Set up protocol handlers
        host->setProtocolHandler(
            {ping->getProtocolId()},
            [ping](libp2p::StreamAndProtocol stream) {
                ping->handle(std::move(stream));
            });

        host->setProtocolHandler(
            {identify->getProtocolId()},
            [identify](libp2p::StreamAndProtocol stream) {
                identify->handle(std::move(stream));
            });

        identify->start();

        // Subscribe to chat topic
        chat_sub = gossip->subscribe(
            {CHAT_TOPIC},
            [peer_id_str](libp2p::protocol::gossip::Gossip::SubscriptionData data) {
                if (data) {
                    std::string msg(data->data.begin(), data->data.end());
                    std::cout << "Chat message on topic '" << data->topic << "': " << msg << std::endl;
                }
            });

        // Subscribe to discovery topic
        discovery_sub = gossip->subscribe(
            {DISCOVERY_TOPIC},
            [](libp2p::protocol::gossip::Gossip::SubscriptionData data) {
                if (data) {
                    std::string msg(data->data.begin(), data->data.end());
                    std::cout << "Discovery message: " << msg << std::endl;
                }
            });

        // Start gossipsub
        gossip->start();

        std::cout << "Protocols:" << std::endl;
        std::cout << "  - Ping: " << ping->getProtocolId() << std::endl;
        std::cout << "  - Identify: /ipfs/id/1.0.0" << std::endl;
        std::cout << "  - Gossipsub: /meshsub/1.0.0" << std::endl;
        std::cout << "Topics:" << std::endl;
        std::cout << "  - " << CHAT_TOPIC << std::endl;
        std::cout << "  - " << DISCOVERY_TOPIC << std::endl;
        std::cout << "Transports: TCP + QUIC" << std::endl;

        // Add bootstrap peers and connect
        for (const auto& addr : remote_addrs) {
            auto peer_id_opt = addr.getPeerId();
            if (!peer_id_opt) {
                std::cout << "Invalid address (missing peer ID): " << addr.getStringAddress() << std::endl;
                continue;
            }

            auto remote_peer_id = libp2p::peer::PeerId::fromBase58(peer_id_opt.value()).value();

            // Add to gossip bootstrap peers
            gossip->addBootstrapPeer(remote_peer_id, addr);

            libp2p::peer::PeerInfo peer_info{remote_peer_id, {addr}};
            std::string transport = getTransportType(addr);

            std::cout << "Connecting via " << transport << " to: " << addr.getStringAddress() << std::endl;

            host->connect(
                peer_info,
                [&, addr, remote_peer_id, ping, log, transport](auto&& conn_result) {
                    if (!conn_result.has_value()) {
                        std::cout << "Failed to connect: " << conn_result.error().message() << std::endl;
                        return;
                    }
                    std::cout << "Connected via " << transport << " to: "
                              << remote_peer_id.toBase58() << std::endl;

                    auto conn = conn_result.value();
                    ping->startPinging(conn, [remote_peer_id](auto&&) {});
                });
        }

        std::cout << "Waiting for connections and messages..." << std::endl;
    });

    // Periodic message publishing (every 10 seconds)
    auto publish_timer = std::make_shared<boost::asio::steady_timer>(*io_context);
    std::function<void(const boost::system::error_code&)> publish_handler;
    publish_handler = [&, publish_timer, peer_id_str](const boost::system::error_code& ec) {
        if (ec) return;

        // Publish a heartbeat message
        std::string msg = createChatMessage(peer_id_str, "heartbeat");
        libp2p::Bytes data(msg.begin(), msg.end());
        if (gossip->publish(CHAT_TOPIC, data)) {
            std::cout << "Published heartbeat to " << CHAT_TOPIC << std::endl;
        }

        publish_timer->expires_after(std::chrono::seconds(10));
        publish_timer->async_wait(publish_handler);
    };

    publish_timer->expires_after(std::chrono::seconds(5));
    publish_timer->async_wait(publish_handler);

    // Subscribe to identify events
    auto identify_connection = identify->onIdentifyReceived(
        [](const libp2p::peer::PeerId& peer_id) {
            std::cout << "Identify received from: " << peer_id.toBase58() << std::endl;
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
