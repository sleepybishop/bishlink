#!/bin/bash
# examples/run_video_benchmark.sh

set -e

# Compile the benchmark
echo "Compiling video latency benchmark..."
make examples/video_latency_benchmark

# Run the benchmark in an unshared user/network namespace
echo "Entering isolated namespace and applying 40ms RTT + 15% packet loss on loopback..."
unshare -Urn bash -c '
    set -e
    
    # Enable loopback
    ip link set lo up
    
    # Add 20ms one-way delay (40ms RTT) and 15% packet loss to loopback
    tc qdisc add dev lo root netem delay 20ms loss 15%
    
    echo ""
    echo "====================================================="
    echo "  TEST 1: RaptorQ FEC Datagram Mode"
    echo "====================================================="
    ./examples/video_latency_benchmark
    
    echo ""
    echo "====================================================="
    echo "  TEST 2: Vanilla Reliable QUIC Stream Mode"
    echo "====================================================="
    ./examples/video_latency_benchmark --reliable
    
    echo ""
'
