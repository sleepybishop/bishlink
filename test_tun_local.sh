#!/bin/bash
# test_tun_local.sh - Run a local end-to-end loopback test using real TUN interfaces

set -e

# Compile everything first
echo "Building bishlink..."
make clean all

# Define clean shutdown
cleanup() {
    echo "Cleaning up processes..."
    sudo kill $TUN_SERVER_PID $TUN_CLIENT_PID 2>/dev/null || true
    kill $SERVER_PID $CLIENT_PID 2>/dev/null || true
    echo "Cleanup complete."
}
trap cleanup EXIT

# Request sudo upfront so the script doesn't block mid-run
echo "This test requires sudo privileges to create and configure virtual TUN interfaces."
sudo -v

# 1. Start Server Daemon (subscribing to client's TUN track)
echo "Starting Server bishlinkd..."
./bishlinkd --listen 9999 --socket bishlink-server --cert t/assets/server.crt --key t/assets/server.key --track tund/tun-client > /tmp/server_daemon.log 2>&1 &
SERVER_PID=$!

# 2. Start Client Daemon (subscribing to server's TUN track)
echo "Starting Client bishlinkd..."
./bishlinkd --peer 127.0.0.1:9999 --socket bishlink-client --track tund/tun-server > /tmp/client_daemon.log 2>&1 &
CLIENT_PID=$!

# Give connection some time to complete TLS handshake and subscriptions
sleep 1.5

# 3. Start Server TUN Daemon (as root)
echo "Starting Server TUN Daemon (tun-server: 10.8.1.1)..."
sudo ./bishlink-tund --socket bishlink-server -i tun-server -a 10.8.1.1/24 > /tmp/tun_server.log 2>&1 &
TUN_SERVER_PID=$!

# 4. Start Client TUN Daemon (as root)
echo "Starting Client TUN Daemon (tun-client: 10.8.1.2)..."
sudo ./bishlink-tund --socket bishlink-client -i tun-client -a 10.8.1.2/24 > /tmp/tun_client.log 2>&1 &
TUN_CLIENT_PID=$!

# Give interfaces a moment to come up and route tables to refresh
echo "Waiting for interfaces to come up..."
sleep 2.0

# Show interfaces status
echo "--- Interfaces Status ---"
ip addr show dev tun-server || true
ip addr show dev tun-client || true
echo "-------------------------"

# 5. Run Ping Test
echo "Pinging Client IP (10.8.1.2) from Server interface (tun-server)..."
if ping -c 4 -I tun-server 10.8.1.2; then
    echo ""
    echo "SUCCESS: Ping transited successfully through: tun-server -> Server Daemon -> Client Daemon -> tun-client!"
    exit 0
else
    echo ""
    echo "FAILURE: Ping failed!"
    echo "=== Server Daemon Log ==="
    cat /tmp/server_daemon.log
    echo "=== Client Daemon Log ==="
    cat /tmp/client_daemon.log
    echo "=== TUN Server Log ==="
    sudo cat /tmp/tun_server.log
    echo "=== TUN Client Log ==="
    sudo cat /tmp/tun_client.log
    exit 1
fi
