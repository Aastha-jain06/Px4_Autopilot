# 🚀 Quick Start Testing (5 Minutes)

## What's Ready to Test RIGHT NOW

```bash
cd /home/jetson/PX4-Autopilot

# TEST 1: Verify all mission files exist
ls -lh mission.sha256 mission.sig Tools/mission_privkey.pem

# TEST 2: Check that files are valid hex (64 and 128 chars)
echo "Hash length: $(wc -c < mission.sha256) (should be 64)"
echo "Sig length:  $(wc -c < mission.sig) (should be 128)"

# TEST 3: View mission coordinates
python3 << 'PYEOF'
import json
with open('Tools/test_mission.plan') as f:
    items = json.load(f)['mission']['items']
    for i, item in enumerate(items, 1):
        print(f"  Waypoint {i}: {item.get('params', [])[4]:.6f}, {item.get('params', [])[5]:.6f}")
PYEOF

# TEST 4: Verify OP-TEE is running
ls -la /dev/tee0 && echo "✓ OP-TEE ready"

# TEST 5: List all created files
echo ""
echo "Files created for secure world:"
ls -lh optee_ta/mission_ta/ta_mission.* src/lib/optee_mission_client/* 2>/dev/null | grep -E "\.(c|h)$" | wc -l
echo "  (should be ~5 files)"
```

**Expected Output:**
```
Hash length: 64 (should be 64)
Sig length:  128 (should be 128)
  Waypoint 1: 47.398331, 8.545508
  Waypoint 2: 47.399333, 8.544815
  Waypoint 3: 47.399089, 8.543440
✓ OP-TEE ready
```

---

## Full Testing Workflow (15 Minutes)

### Step 1: Build PX4 with OP-TEE Support

```bash
cd /home/jetson/PX4-Autopilot
make px4_sitl gz_x500 2>&1 | tail -20
```

**Check**: Build should succeed without errors.

### Step 2: Start PX4 SITL

```bash
# Terminal 1
make px4_sitl gz_x500
```

Wait for:
```
[commander] Ready for takeoff
pxh>
```

### Step 3: Upload Mission with Signature

```bash
# Terminal 2
python3 Tools/test_hash.py
```

**Expected output:**
```
Connecting...
Connected
Uploading mission...
Mission uploaded — waiting 4 s for dataman write and hash check cycle...
Arming (triggers hash check)...
ARMED — hash matched!
```

### Step 4: Monitor Secure World Behavior

```bash
# Terminal 3: Watch for OP-TEE messages
dmesg -w | grep -i "mission\|tee"
```

**You should see** (if TA is compiled):
```
mission_client: TA initialized OK
mission_client: uploading 3 items
```

---

## Testing Without OP-TEE SDK (Simulation Mode)

If you don't have the SDK to compile the TA, you can still test the normal world code:

```bash
# 1. Check mission_client API compiles
cd /home/jetson/PX4-Autopilot
cat > /tmp/test_api.c << 'CEOF'
#include "src/lib/optee_mission_client/mission_client.h"
int main() {
    mission_client_init();
    uint16_t count = mission_client_get_count();
    mission_client_cleanup();
    return 0;
}
CEOF

# 2. Try to compile (will fail at link time without OP-TEE, but shows API is right)
gcc -I. /tmp/test_api.c src/lib/optee_mission_client/mission_client.c -I/usr/include 2>&1 | head -5

# If it fails with "TEEC_OpenSession" — that's EXPECTED (no OP-TEE available)
# If it fails with syntax errors — that's a PROBLEM
```

---

## Verification Checklist

- [ ] ✓ mission.sha256 exists (64 bytes, hex)
- [ ] ✓ mission.sig exists (128 bytes, hex)  
- [ ] ✓ mission_privkey.pem exists
- [ ] ✓ /dev/tee0 exists (OP-TEE running)
- [ ] ✓ PX4 builds with `make px4_sitl gz_x500`
- [ ] ✓ Mission uploads and drone arms with `python3 Tools/test_hash.py`
- [ ] ✓ (Optional) TA compiles and OP-TEE messages appear in dmesg

---

## Next: Install OP-TEE SDK for Full Testing

```bash
# Option 1: Package manager (if available)
apt-cache search optee-os

# Option 2: From source
git clone https://github.com/OP-TEE/optee_os.git /tmp/optee_os
cd /tmp/optee_os
make all

# Option 3: Docker (easiest)
docker pull optee/optee_os:latest
docker run -it --rm -v $PWD:/work optee/optee_os:latest bash

# Then compile TA:
cd /home/jetson/PX4-Autopilot/optee_ta/mission_ta
make
sudo cp 8aaaf200-2450-11e4-abe2-0002a5d5c51b.ta /lib/optee_armtz/
```

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| "Cannot connect to OP-TEE" | `sudo systemctl restart tee-supplicant` |
| "libteec not found" | `apt-get install libteec-dev` |
| "PX4 build fails" | `make distclean && make px4_sitl gz_x500` |
| "Mission upload fails" | Regenerate: `python3 Tools/mission_sign.py Tools/test_mission.plan` |
| "Arm blocked" | Check console logs; hash/sig mismatch |

---

**Status:** ✅ Ready to test. Start with Step 1-3 above!
