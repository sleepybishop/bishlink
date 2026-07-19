#!/bin/bash
# run_diag.sh
set -e

# Mount tmpfs over /run to support nested ip netns without host root
mount -t tmpfs tmpfs /run
mkdir -p /run/netns

# Create isolated namespaces for host and client
ip netns add ns_host
ip netns add ns_client

# Create veth pair to connect host and client namespaces
ip link add veth_host type veth peer name veth_client
ip link set veth_host netns ns_host
ip link set veth_client netns ns_client

# Configure the host-side namespace interface
ip netns exec ns_host ip link set lo up
ip netns exec ns_host ip addr add 192.168.1.1/24 dev veth_host
ip netns exec ns_host ip link set veth_host up

# Configure the client-side namespace interface
ip netns exec ns_client ip link set lo up
ip netns exec ns_client ip addr add 192.168.1.2/24 dev veth_client
ip netns exec ns_client ip link set veth_client up

# Start server daemon
echo "=== Starting Server Daemon ==="
ip netns exec ns_host ./bishlinkd --listen 8888 --bind 192.168.1.1 --socket bishlink-data --track tund/tun-client &
PID_HOST=$!

# Start client daemon
echo "=== Starting Client Daemon ==="
ip netns exec ns_client ./bishlinkd --peer 192.168.1.1:8888 --socket bishlink-data-client --track tund/tun-host &
PID_CLIENT=$!
sleep 2

# Start server tund
echo "=== Starting Server Tund ==="
ip netns exec ns_host ./bishlink-tund --socket bishlink-data -i tun-host -a 10.8.0.1/24 --track tund/tun-host &
PID_TUND_HOST=$!

# Start client tund
echo "=== Starting Client Tund ==="
ip netns exec ns_client ./bishlink-tund --socket bishlink-data-client -i tun-client -a 10.8.0.2/24 --track tund/tun-client &
PID_TUND_CLIENT=$!
sleep 2

# Start tcpdump in both namespaces (line-buffered)
echo "=== Starting tcpdump ==="
ip netns exec ns_host tcpdump -l -Z root -xx -n -i tun-host > logs/tcpdump_host.log 2>&1 &
PID_TCPDUMP_HOST=$!
ip netns exec ns_client tcpdump -l -Z root -xx -n -i tun-client > logs/tcpdump_client.log 2>&1 &
PID_TCPDUMP_CLIENT=$!
sleep 1

# Disable checksum offloading on TUN interfaces
echo "=== Disabling Checksum Offloading ==="
ip netns exec ns_host ethtool -K tun-host tx off rx off gso off tso off gro off || true
ip netns exec ns_client ethtool -K tun-client tx off rx off gso off tso off gro off || true

# Print routes and IPs
echo "=== Host Network Configuration ==="
ip netns exec ns_host ip addr show || true
ip netns exec ns_host ip route show || true
echo "=== Client Network Configuration ==="
ip netns exec ns_client ip addr show || true
ip netns exec ns_client ip route show || true

# Ping test
echo "=== Ping Test ==="
ip netns exec ns_client ping -c 2 10.8.0.1 || true

# Webserver
echo "=== Starting Webserver ==="
ip netns exec ns_host python3 -m http.server --bind 10.8.0.1 8080 &
PID_WEBSERVER=$!
sleep 1

# Curl test
echo "=== Curl Test ==="
ip netns exec ns_client curl -v --connect-timeout 3 --max-time 5 http://10.8.0.1:8080/ || true

# Cleanup
echo "=== Cleanup ==="
kill $PID_TCPDUMP_HOST $PID_TCPDUMP_CLIENT $PID_WEBSERVER $PID_TUND_CLIENT $PID_TUND_HOST $PID_CLIENT $PID_HOST
wait

echo "=== TCPDUMP HOST ==="
cat logs/tcpdump_host.log || true
echo "=== TCPDUMP CLIENT ==="
cat logs/tcpdump_client.log || true

echo "=== FIREWALL RULES HOST ==="
ip netns exec ns_host iptables -S || true
ip netns exec ns_host nft list ruleset || true

echo "=== FIREWALL RULES CLIENT ==="
ip netns exec ns_client iptables -S || true
ip netns exec ns_client nft list ruleset || true

ip netns del ns_host
ip netns del ns_client
echo "=== Done ==="
