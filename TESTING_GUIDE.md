# Secure World Mission Verification - Testing Guide

## Quick Summary

Your system is **ready for testing**:
- ✅ OP-TEE running and working
- ✅ Mission files created (hash + signature)
- ✅ Client library integrated into PX4 build
- ⚠️ TA needs to be compiled (requires OP-TEE SDK)

---

## Testing Scenarios

### Scenario A: Verify Client Library Compiles (5 min)

**Goal**: Make sure the normal world code builds without errors.

```bash
cd /home/jetson/PX4-Autopilot

# Build just the health checks module
cd src/modules/commander/HealthAndArmingChecks
make -C ../../.. px4_sitl 2>&1 | grep -i "optee\|teec\|mission_client"
```

**Expected output**:
```
Building CXX object ... optee_mission_client
Linking CXX static library ... health_and_arming_checks.a
```

**If it fails**:
- Check TEEC libraries: `pkg-config teec --cflags`
- Install: `apt-get install libteec-dev`

---

### Scenario B: Test Mission Upload Flow (Without TA Compiled) (10 min)

**Goal**: Verify the mission signing and file generation works.

```bash
cd /home/jetson/PX4-Autopilot

# Step 1: Generate fresh keypair
python3 Tools/generate_keypair.py

# Step 2: Sign mission
python3 Tools/mission_sign.py Tools/test_mission.plan

# Step 3: Verify files
ls -lh mission.sha256 mission.sig
cat mission.sha256
cat mission.sig

# Step 4: Check file format
echo "Hash file (should be 64 hex chars):"
wc -c mission.sha256

echo "Sig file (should be 128 hex chars):"
wc -c mission.sig

# Step 5: Validate hex format
echo "Testing hash parsing:"
python3 -c "
with open('mission.sha256') as f:
    h = f.read().strip()
    print(f'Hash: {h[:32]}...{h[-32:]}')
    assert len(h) == 64, f'Expected 64 chars, got {len(h)}'
    assert all(c in '0123456789abcdef' for c in h), 'Invalid hex'
print('✓ Hash file valid')
"

echo "Testing sig parsing:"
python3 -c "
with open('mission.sig') as f:
    s = f.read().strip()
    print(f'Sig:  {s[:32]}...{s[-32:]}')
    assert len(s) == 128, f'Expected 128 chars, got {len(s)}'
    assert all(c in '0123456789abcdef' for c in s), 'Invalid hex'
print('✓ Sig file valid')
"
```

**Expected output**:
```
Hash:  7eb24d086c5e006fb391f2ddd5ce7066...3089da16de688df85a9c006b7488eb4
Sig:   0d6b91ce7a515cfc715f354d2ed94a45...112bd83a9901150a
✓ Hash file valid
✓ Sig file valid
```

---

### Scenario C: Build PX4 with OP-TEE Support (15 min)

**Goal**: Make sure PX4 builds with the client library linked.

```bash
cd /home/jetson/PX4-Autopilot

# Clean build
make distclean

# Build SITL
make px4_sitl gz_x500 2>&1 | tee build.log

# Check for OP-TEE references
echo "=== Checking build output ==="
grep -i "optee\|mission_client\|teec" build.log | head -20

# Check final binary
nm build/px4_sitl_default/bin/px4 | grep -i mission_client || echo "mission_client not found (expected if TA not linked)"
```

**Expected**:
- Build completes without errors
- mission_client library links successfully
- Binary contains mission_client symbols (if TEEC headers available)

---

### Scenario D: Test Mission Upload with SITL (25 min)

**Requirement**: OP-TEE SDK installed OR use simulation mode

#### **Option D1: With OP-TEE SDK Installed**

```bash
# 1. Compile the TA
cd /home/jetson/PX4-Autopilot/optee_ta/mission_ta
make
sudo cp 8aaaf200-2450-11e4-abe2-0002a5d5c51b.ta /lib/optee_armtz/

# 2. Reload TA
sudo systemctl restart tee-supplicant

# 3. Build PX4
cd /home/jetson/PX4-Autopilot
make px4_sitl gz_x500

# 4. Start SITL (Terminal 1)
make px4_sitl gz_x500
# Wait for "Ready for takeoff" message

# 5. Upload mission (Terminal 2)
python3 Tools/mission_sign.py Tools/test_mission.plan
python3 Tools/test_hash.py

# 6. Watch console for (Terminal 1):
#    - "mission_client: TA initialized OK"
#    - "Mission hash OK"
#    - "ARMED — hash matched!"
```

#### **Option D2: Simulation Mode (No TA SDK Needed)**

If you don't have the SDK, test the client library code logic:

```bash
# 1. Create a mock TA test
cd /home/jetson/PX4-Autopilot

cat > test_mission_client.c << 'EOF'
#include "src/lib/optee_mission_client/mission_client.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("Testing mission_client API (mock test)\n");

    // These will fail without OP-TEE, but verify the API is correct
    uint16_t count = mission_client_get_count();
    printf("Mission count: %u\n", count);

    return 0;
}
EOF

# 2. Try to compile
gcc -I. -I/usr/include test_mission_client.c \
    src/lib/optee_mission_client/mission_client.c \
    -o test_mission_client 2>&1 | head -20

# 3. Run
./test_mission_client 2>&1 || true
# Expected: Will fail to connect to TA, but proves compilation works
```

---

### Scenario E: Manual TA Testing (With SDK) (20 min)

If you have the OP-TEE SDK, test the TA directly:

```bash
# 1. Enable TA logging
cd /home/jetson/PX4-Autopilot/optee_ta/mission_ta
# Edit ta_mission.c: uncomment IMSG/EMSG lines

# 2. Rebuild
make CFG_DEBUG=y

# 3. Reload
sudo cp 8aaaf200-2450-11e4-abe2-0002a5d5c51b.ta /lib/optee_armtz/
sudo systemctl restart tee-supplicant

# 4. Watch dmesg output while testing
watch 'dmesg | tail -20'

# 5. In another terminal, trigger TA commands
python3 -c "
import sys
sys.path.insert(0, 'src/lib/optee_mission_client')
# Direct API test would go here
"
```

---

### Scenario F: Security Validation Tests (15 min each)

#### **Test 1: Signature Rejection**

```bash
cd /home/jetson/PX4-Autopilot

# 1. Create a corrupted signature
python3 << 'EOF'
with open('mission.sig') as f:
    sig = f.read()
# Flip a bit in the middle
corrupted = sig[:64] + 'a' + sig[65:]
with open('mission.sig.bad') as f:
    f.write(corrupted)
EOF

# 2. Try to upload with bad signature
cp mission.sig.bad mission.sig
python3 Tools/test_hash.py

# Expected: "Arm blocked: Mission signature invalid"
```

#### **Test 2: Hash Rejection**

```bash
cd /home/jetson/PX4-Autopilot

# 1. Corrupt the hash file
echo "0000000000000000000000000000000000000000000000000000000000000000" > mission.sha256

# 2. Try to upload
python3 Tools/test_hash.py

# Expected: "Arm blocked: Mission hash mismatch"
```

#### **Test 3: Waypoint One-at-a-Time Verification**

Edit `src/lib/optee_mission_client/mission_client.c` and add logging:

```cpp
int mission_client_get_waypoint(uint16_t index, mission_client_waypoint_t *wp)
{
    fprintf(stdout, "GET_WAYPOINT called with index=%u (ONE waypoint per RPC)\n", index);
    // ... rest of function
}
```

Rebuild and run `test_hash.py`. Should see:
```
GET_WAYPOINT called with index=0
GET_WAYPOINT called with index=1
GET_WAYPOINT called with index=2
```

---

## Quick Test Commands (Copy-Paste)

```bash
#!/bin/bash
# Quick test of everything

cd /home/jetson/PX4-Autopilot

echo "=== Phase 1: Verify files ==="
ls -lh mission.*.py Tools/mission_privkey.pem optee_ta/mission_ta/ta_mission.c

echo ""
echo "=== Phase 2: Generate mission files ==="
python3 Tools/mission_sign.py Tools/test_mission.plan
ls -lh mission.*

echo ""
echo "=== Phase 3: Build PX4 ==="
make px4_sitl gz_x500 2>&1 | tail -5

echo ""
echo "=== Phase 4: Files ready to upload ==="
echo "Hash:"
cat mission.sha256 | cut -c1-32
echo "..."
echo ""
echo "Sig:"
cat mission.sig | cut -c1-32
echo "..."

echo ""
echo "=== Ready to test ==="
echo "1. make px4_sitl gz_x500"
echo "2. python3 Tools/test_hash.py"
```

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| "TEEC_OpenSession failed" | OP-TEE not running: `sudo systemctl restart tee-supplicant` |
| "Client lib not found" | Run: `apt-get install libteec-dev` |
| "TA compilation fails" | Install OP-TEE SDK (see Phase 4 in test script) |
| "Mission hash mismatch" | Mission was modified; regenerate: `python3 Tools/mission_sign.py ...` |
| "Signature invalid" | Keypair changed; regenerate: `python3 Tools/generate_keypair.py` |
| "No mission loaded" | Upload first: `python3 Tools/test_hash.py` |

---

## Next: Full OP-TEE SDK Setup (Optional)

If you want to compile the TA, follow one of these approaches:

### Option 1: Prebuilt SDK (Easiest)

```bash
# Check if NVIDIA provides prebuilt for Orin
apt-cache search optee | grep -i sdk
```

### Option 2: Build from Source

```bash
cd /tmp
git clone https://github.com/OP-TEE/optee_os.git
cd optee_os
make all

# Then compile our TA
cd /home/jetson/PX4-Autopilot/optee_ta/mission_ta
export TA_DEV_KIT_DIR=/tmp/optee_os/out/arm-plat-virt/export-ta_arm64
make
```

### Option 3: Docker Container

```bash
docker run -it --rm -v $PWD:/work optee/optee_os:latest bash
# Inside container:
cd /work
make
```

---

## Summary

✅ **Immediate**: Test Phases A, B, C (no TA SDK needed)
⚠️ **Next**: Install OP-TEE SDK for Phases D, E, F
🎯 **Final**: Full integration test with PX4 SITL

Start with Phase A and B — they're quick wins! 🚀
