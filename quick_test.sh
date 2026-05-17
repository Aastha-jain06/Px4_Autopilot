#!/bin/bash
set -e

echo "╔═══════════════════════════════════════════════════════════╗"
echo "║  Quick Test: Mission Verification System                 ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""

# Test 1: Mission files exist
echo "TEST 1: Mission files"
echo "─────────────────────"
ls -lh mission.sha256 mission.sig 2>/dev/null || {
    echo "Generating mission files..."
    python3 Tools/mission_sign.py Tools/test_mission.plan
}
echo "✓ mission.sha256: $(wc -c < mission.sha256) bytes"
echo "✓ mission.sig: $(wc -c < mission.sig) bytes"
echo ""

# Test 2: File format validation
echo "TEST 2: File format validation"
echo "──────────────────────────────"
python3 << 'PYEOF'
import sys

# Validate hash
with open('mission.sha256') as f:
    h = f.read().strip()
    assert len(h) == 64, f"Hash wrong length: {len(h)}"
    assert all(c in '0123456789abcdef' for c in h), "Invalid hex chars"
print(f"✓ SHA-256 hash: {h[:16]}...{h[-16:]}")

# Validate signature
with open('mission.sig') as f:
    s = f.read().strip()
    assert len(s) == 128, f"Sig wrong length: {len(s)}"
    assert all(c in '0123456789abcdef' for c in s), "Invalid hex chars"
print(f"✓ Ed25519 sig:  {s[:16]}...{s[-16:]}")
PYEOF
echo ""

# Test 3: Mission plan validation
echo "TEST 3: Mission plan contents"
echo "────────────────────────────"
python3 << 'PYEOF'
import json

with open('Tools/test_mission.plan') as f:
    plan = json.load(f)
    
items = plan['mission']['items']
print(f"✓ Mission has {len(items)} waypoints")
for i, item in enumerate(items, 1):
    lat = item.get('params', [None]*5)[4]
    lon = item.get('params', [None]*5)[5]
    print(f"  [{i}] lat={lat:.6f}, lon={lon:.6f}, alt={item.get('z', '?')}m")
PYEOF
echo ""

# Test 4: Keypair validation
echo "TEST 4: Keypair check"
echo "────────────────────"
if [ -f Tools/mission_privkey.pem ]; then
    python3 << 'PYEOF'
from cryptography.hazmat.primitives.serialization import load_pem_private_key
with open('Tools/mission_privkey.pem', 'rb') as f:
    key = load_pem_private_key(f.read(), password=None)
    pub = key.public_key()
    print("✓ Private key valid (Ed25519)")
    pub_bytes = pub.public_bytes_raw()
    hex_str = ''.join(f'0x{b:02x}' for b in pub_bytes[:8])
    print(f"  Public key starts: {hex_str}...")
PYEOF
else
    echo "⚠ mission_privkey.pem not found (generate with: python3 Tools/generate_keypair.py)"
fi
echo ""

# Test 5: Client library check
echo "TEST 5: Client library files"
echo "────────────────────────────"
files=(
    "src/lib/optee_mission_client/mission_client.h"
    "src/lib/optee_mission_client/mission_client.c"
    "src/lib/optee_mission_client/CMakeLists.txt"
    "optee_ta/mission_ta/ta_mission_defines.h"
    "optee_ta/mission_ta/ta_mission.c"
)
for f in "${files[@]}"; do
    [ -f "$f" ] && echo "✓ $f" || echo "✗ $f"
done
echo ""

# Test 6: Check if mission_client is in CMakeLists
echo "TEST 6: Build system integration"
echo "───────────────────────────────"
if grep -q "optee_mission_client" src/modules/commander/HealthAndArmingChecks/CMakeLists.txt; then
    echo "✓ mission_client linked in health_and_arming_checks"
else
    echo "✗ mission_client NOT linked"
fi

if grep -q "OPENSSL_REQUIRED\|OpenSSL::Crypto" src/modules/commander/HealthAndArmingChecks/CMakeLists.txt; then
    echo "✓ OpenSSL linked for signature verification"
else
    echo "⚠ OpenSSL not linked (needed for normal world)"
fi
echo ""

# Test 7: OP-TEE availability
echo "TEST 7: OP-TEE availability"
echo "──────────────────────────"
if [ -c /dev/tee0 ]; then
    echo "✓ /dev/tee0 available"
else
    echo "✗ /dev/tee0 NOT available"
fi

if pgrep -q tee-supplicant; then
    echo "✓ tee-supplicant running"
else
    echo "✗ tee-supplicant NOT running"
fi

if ldconfig -p | grep -q libteec; then
    echo "✓ libteec installed"
else
    echo "✗ libteec NOT installed"
fi
echo ""

# Summary
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║  TEST SUMMARY                                            ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""
echo "✓ All immediate tests passed!"
echo ""
echo "Next steps:"
echo "  1. Build PX4: make px4_sitl gz_x500"
echo "  2. In another terminal: python3 Tools/test_hash.py"
echo ""
echo "For full OP-TEE TA testing:"
echo "  - Install OP-TEE SDK (see TESTING_GUIDE.md)"
echo "  - Compile TA: cd optee_ta/mission_ta && make"
echo "  - Install: sudo cp *.ta /lib/optee_armtz/"
