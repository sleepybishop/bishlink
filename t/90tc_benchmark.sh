#!/bin/bash
# t/90tc_benchmark.sh

set -e

# Compile the kernel-only benchmark binary
make t/00util/test_tc_benchmark

echo "=================================== KERNEL tc BENCHMARK REPORT ==================================="

# Test Profile 1: Pristine (0% loss, 0ms delay)
unshare -Urn bash -c "
    ip link set lo up
    tc qdisc add dev lo root netem loss 0% delay 0ms
    ./t/00util/test_tc_benchmark 'Pristine Network (0% Loss)'
"

# Test Profile 2: Moderate network degradation (5% loss, 5ms delay)
unshare -Urn bash -c "
    ip link set lo up
    tc qdisc add dev lo root netem loss 5% delay 5ms
    ./t/00util/test_tc_benchmark 'Moderate Loss (5% Loss, 5ms Delay)'
"

# Test Profile 3: Severe network degradation (20% loss, 20ms delay)
unshare -Urn bash -c "
    ip link set lo up
    tc qdisc add dev lo root netem loss 20% delay 20ms
    ./t/00util/test_tc_benchmark 'Severe Loss (20% Loss, 20ms Delay)'
"

echo "=================================================================================================="
