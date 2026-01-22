/**
 * Lesson 1: Identity and Basic Host
 * Creates a basic libp2p host with cryptographic identity.
 *
 * In cpp-libp2p, the dependency injector (makeHostInjector) automatically
 * generates an Ed25519 keypair for you. This simplifies host creation!
 */

#include <iostream>
#include <memory>
#include <csignal>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/signal_set.hpp>

#include <libp2p/host/host.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

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

    auto log = libp2p::log::createLogger("Lesson1");

    // Step 2: Create host using dependency injection
    // The injector automatically generates an Ed25519 keypair internally!
    // It also sets up: TCP transport, Noise security, Yamux muxer, and more.
    auto injector = libp2p::injector::makeHostInjector();

    // Step 3: Extract the host and io_context from the injector
    auto host = injector.create<std::shared_ptr<libp2p::Host>>();
    auto io_context =
        injector.create<std::shared_ptr<boost::asio::io_context>>();

    // Step 4: Get and print the peer ID
    // The PeerId is derived from the auto-generated Ed25519 public key
    auto peer_id = host->getId();
    std::cout << "Local peer id: " << peer_id.toBase58() << std::endl;

    // Step 5: Set up signal handling for graceful shutdown (Ctrl+C)
    boost::asio::signal_set signals(*io_context, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
        std::cout << "Shutting down..." << std::endl;
        io_context->stop();
    });

    // Step 6: Start the host and run the event loop
    boost::asio::post(*io_context, [&] {
        host->start();
        log->info("Host started with PeerId: {}", peer_id.toBase58());
    });

    // Run the Boost.Asio event loop (blocks until io_context->stop() is called)
    try {
        io_context->run();
    } catch (const std::exception& e) {
        log->error("Error: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
