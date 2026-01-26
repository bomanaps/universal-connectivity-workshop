# Lesson 6: Gossipsub Checkpoint

In this lesson, you'll implement **Gossipsub**, a scalable publish-subscribe protocol for peer-to-peer messaging.

## Learning Objectives

- Understand publish-subscribe messaging patterns
- Implement Gossipsub for topic-based communication
- Subscribe to and publish messages on topics
- Handle peer subscription and discovery events

## What is Gossipsub?

Gossipsub (`/meshsub/1.0.0`) is a pub/sub protocol that uses a mesh topology:

- **Topics**: Messages are organized by topic (e.g., "chat", "discovery")
- **Mesh**: Each peer maintains connections to a subset of topic subscribers
- **Gossip**: Peers share metadata about messages they've seen
- **Deduplication**: Messages are only processed once per peer

## Key Concepts

| Concept | Description |
|---------|-------------|
| **Topic** | A named channel for messages |
| **Subscribe** | Register interest in a topic |
| **Publish** | Send a message to all topic subscribers |
| **Mesh** | The subset of peers a node directly exchanges messages with |
| **Gossip** | Metadata about seen messages shared with non-mesh peers |

## Topics Used

- `universal-connectivity` - Main chat topic
- `universal-connectivity-browser-peer-discovery` - Peer discovery announcements

## Code Walkthrough

### Configure Gossipsub

```cpp
#include <libp2p/protocol/gossip/gossip.hpp>

// Configure Gossipsub
libp2p::protocol::gossip::Config gossip_config;
gossip_config.D_min = 2;        // Minimum mesh peers
gossip_config.D_max = 4;        // Maximum mesh peers
gossip_config.heartbeat_interval_msec = std::chrono::milliseconds(1000);
gossip_config.protocol_version = "/meshsub/1.0.0";
gossip_config.sign_messages = true;

// Create Gossipsub
auto gossip = libp2p::protocol::gossip::create(
    scheduler, host, identity_manager, crypto_provider, key_marshaller, gossip_config);
```

### Subscribe to Topics

```cpp
const std::string CHAT_TOPIC = "universal-connectivity";

auto chat_sub = gossip->subscribe(
    {CHAT_TOPIC},
    [](libp2p::protocol::gossip::Gossip::SubscriptionData data) {
        if (data) {
            std::string msg(data->data.begin(), data->data.end());
            std::cout << "Message on '" << data->topic << "': " << msg << std::endl;
        }
    });

gossip->start();
```

### Publish Messages

```cpp
std::string message = "{\"sender\":\"peer123\",\"message\":\"Hello!\"}";
libp2p::Bytes data(message.begin(), message.end());

if (gossip->publish(CHAT_TOPIC, data)) {
    std::cout << "Message published!" << std::endl;
}
```

### Add Bootstrap Peers

```cpp
// Add peer to gossip's known peers
gossip->addBootstrapPeer(remote_peer_id, multiaddress);
```

## Message Format

Messages use simple JSON format:

```json
{
  "sender": "12D3KooW...",
  "message": "Hello, Universal Connectivity!",
  "timestamp": 1706400000000
}
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `LISTEN_PORT` | 9000 | TCP listening port |
| `QUIC_PORT` | 9001 | QUIC listening port |
| `REMOTE_PEERS` | (empty) | Comma-separated peer multiaddresses |

## Building and Running

```bash
# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .

# Run first peer
./lesson

# Run second peer connected to first
LISTEN_PORT=9002 QUIC_PORT=9003 \
REMOTE_PEERS="/ip4/127.0.0.1/tcp/9000/p2p/12D3KooW..." ./lesson
```

## Expected Output

```
Starting Universal Connectivity Application...
Local peer id: 12D3KooWxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
Agent version: universal-connectivity/0.1.0
Listening on TCP: /ip4/0.0.0.0/tcp/9000
Listening on QUIC: /ip4/0.0.0.0/udp/9001/quic-v1
Protocols:
  - Ping: /ipfs/ping/1.0.0
  - Identify: /ipfs/id/1.0.0
  - Gossipsub: /meshsub/1.0.0
Topics:
  - universal-connectivity
  - universal-connectivity-browser-peer-discovery
Transports: TCP + QUIC
Waiting for connections and messages...
Published heartbeat to universal-connectivity
```

## Next Steps

In Lesson 7, you'll complete the final checkpoint combining all protocols for full Universal Connectivity.
