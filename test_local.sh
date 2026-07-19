#!/bin/bash
# test_local.sh - Run a local end-to-end loopback test using bishlinkd and bishlink-tund in mock mode (no sudo required)

set -e

# Compile everything first
echo "Building bishlink..."
make clean all

# Define clean shutdown
cleanup() {
    echo "Cleaning up processes..."
    kill $TUN_SERVER_PID $TUN_CLIENT_PID $SERVER_PID $CLIENT_PID 2>/dev/null || true
    echo "Cleanup complete."
}
trap cleanup EXIT

# 1. Start Server Daemon (subscribing to the client's TUN track)
echo "Starting Server bishlinkd..."
./bishlinkd --listen 9999 --socket bishlink-server --cert t/assets/server.crt --key t/assets/server.key --track tund/tun-client > /tmp/server_daemon.log 2>&1 &
SERVER_PID=$!

# 2. Start Client Daemon (subscribing to the server's TUN track)
echo "Starting Client bishlinkd..."
./bishlinkd --peer 127.0.0.1:9999 --socket bishlink-client --track tund/tun-server > /tmp/client_daemon.log 2>&1 &
CLIENT_PID=$!

# Give connection some time to complete TLS handshake
sleep 1.0

# 3. Start Server TUN Daemon in mock mode (does not need root)
echo "Starting Server TUN Daemon in mock mode..."
./bishlink-tund --mock --socket bishlink-server -i tun-server -a 10.8.1.1/24 > /tmp/tun_server.log 2>&1 &
TUN_SERVER_PID=$!

# 4. Start Client TUN Daemon in mock mode (does not need root)
echo "Starting Client TUN Daemon in mock mode..."
./bishlink-tund --mock --socket bishlink-client -i tun-client -a 10.8.1.2/24 > /tmp/tun_client.log 2>&1 &
TUN_CLIENT_PID=$!

# Let them run for 1.5 seconds to generate and exchange mock packets
echo "Waiting for mock packet transit..."
sleep 1.5

# 5. Check if the server-side mock TUN received the packet
if grep -q "MOCK: received packet" /tmp/tun_server.log; then
    echo "--- Server TUN Daemon Log ---"
    cat /tmp/tun_server.log
    echo "-----------------------------"
    echo "SUCCESS: Mock IP packet successfully transited from Client TUN -> Client Daemon -> Server Daemon -> Server TUN!"
    exit 0
else
    echo "FAILURE: Mock packets did not transit!"
    echo "=== Server Daemon Log ==="
    cat /tmp/server_daemon.log
    echo "=== Client Daemon Log ==="
    cat /tmp/client_daemon.log
    echo "=== TUN Server Log ==="
    cat /tmp/tun_server.log
    echo "=== TUN Client Log ==="
    cat /tmp/tun_client.log
    exit 1
fi
