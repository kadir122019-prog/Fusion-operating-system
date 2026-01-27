#!/bin/bash
# Test script to verify clear command works within window bounds

cd /home/r1pper/Fusion

echo "Testing Fusion OS clear command behavior..."
echo ""
echo "=== Test 1: Basic help and clear ==="

timeout 8 qemu-system-x86_64 -cdrom fusion.iso -m 256M -serial stdio 2>&1 << 'EOF' | grep -A 20 "Welcome"
help
clear
meminfo
EOF

echo ""
echo "=== Test complete ==="
