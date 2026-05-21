# Waypoint Hash Chain (`wp_hash_chain`)

Tamper-evident proof that a drone flew **exactly** the planned mission — using ARM TrustZone (OP-TEE) to build a cryptographic hash chain over planned waypoint coordinates.

---

## The Problem

After a drone completes a mission, anyone can claim it flew the planned path. Log files and GPS data can be forged. There is no cryptographic proof unless the computation happens somewhere **untamperable**.

---

## The Solution

A **SHA-256 hash chain** computed inside TrustZone secure world:

```
chain_hash[0] = SHA256( zeros[32]          | WP0_lat | WP0_lon | WP0_alt )
chain_hash[1] = SHA256( chain_hash[0]      | WP1_lat | WP1_lon | WP1_alt )
chain_hash[2] = SHA256( chain_hash[1]      | WP2_lat | WP2_lon | WP2_alt )
...
chain_hash[N] = SHA256( chain_hash[N-1]    | WPN_lat | WPN_lon | WPN_alt )
```

Each hash includes the previous hash — so changing **any single waypoint** changes every subsequent hash and the final result completely.

The chain state (`prev_hash`) **never leaves TrustZone**. Even if Linux or PX4 is compromised, the chain cannot be forged.

---

## Architecture

```
GROUND STATION                          DRONE (Jetson + OP-TEE)
──────────────                          ──────────────────────────────────

.plan file                              PX4 SITL running
     |                                       |
waypoint_chain.py                       wp_hash_chain module starts
     |                                       |
Compute planned chain                   mission_client_init()
(same SHA-256 formula)                  mission_client_reset_chain()
     |                                       |
planned_final_hash                      Takeoff detected → chain reset
                                             |
                                        navigator_mission_item fires
                                        → planned coords cached
                                             |
                                        mission_result.seq_reached fires
                                        → mission_client_chain_pos() called
                                        → OP-TEE computes SHA-256
                                        → new hash returned
                                             |
                                        Landing detected
                                        → wp_flight_chain.sha256 written
                                             |
                    Compare ←───────── flight_final_hash

MATCH   = drone flew exactly the planned mission ✅
MISMATCH = something changed ❌
```

---

## Hardware

- **Board:** NVIDIA Jetson AGX Orin
- **Secure world OS:** OP-TEE (Open Portable Trusted Execution Environment)
- **Trusted Application:** `mission_ta` (UUID: `8aaaf200-2450-11e4-abe2-0002a5d5c51b`)

```
ARM Processor
├── Normal World    → Linux + PX4 (wp_hash_chain module runs here)
└── Secure World    → OP-TEE + mission_ta (hash computed here)
```

---

## Hash Formula

```
Input  = prev_hash(32 bytes) + lat(8 bytes) + lon(8 bytes) + alt(4 bytes)
       = 52 bytes total

Output = SHA256(input) = 32 bytes

Types:
  prev_hash  → uint8[32]  (all zeros at genesis)
  lat        → float64 little-endian  (float32 value widened to float64)
  lon        → float64 little-endian  (float32 value widened to float64)
  alt        → float32 little-endian
```

**Why float32 widened to float64?**
`navigator_mission_item` publishes coordinates as `float32`. The value is widened to `float64` (8 bytes) before hashing. The Python ground script applies the same `_f32()` round-trip so both sides pack identical bytes.

**Why LAND (cmd=21) is excluded?**
PX4 adjusts the LAND waypoint altitude at runtime. The planned value and the actual value never match. Both sides skip it.

---

## Files

```
src/modules/wp_hash_chain/
├── WpHashChain.hpp              Module class definition
├── WpHashChain.cpp              Module implementation
├── wp_hash_chain_main.cpp       Entry point (ModuleBase::main)
├── CMakeLists.txt               Build config
├── Kconfig                      PX4 config system entry
└── README.md                    This file

msg/
└── WpHashChain.msg              uORB message (logged in .ulg flight log)

optee_ta/mission_ta/
├── ta_mission.c                 Trusted Application (runs in secure world)
└── ta_mission_defines.h         Shared structs (ta_chain_pos_t = 20 bytes)

src/lib/optee_mission_client/
├── mission_client.h             Client API (normal world → TA)
└── mission_client.c             TEEC calls (world switch)

Tools/
├── waypoint_chain.py            Ground station: compute planned chain
├── run_chain_mission.py         End-to-end: fly mission + compare hashes
└── mc_mission_5wp.plan          Sample 5-waypoint mission file
```

---

## How It Works — Step by Step

### 1. Coordinate Cache

`navigator_mission_item` fires when navigator starts heading **toward** waypoint N.
`mission_result.seq_reached` fires when drone **arrives at** waypoint N.

These are two different events. We cache coordinates from the first event and hash them on the second:

```
                 nav_item (seq=N) fires
                         |
                         v
                  _cache[N] = {lat, lon, alt}    ← store planned coords

                 [drone flies to waypoint N...]

                 mission_result.seq_reached=N fires
                         |
                         v
                  _seq_frozen[N] = true           ← lock the cache slot
                  hash_waypoint(N)                ← send to OP-TEE
```



### 2. OP-TEE Call

```cpp
mission_client_chain_pos(
    (double)wp.lat,    // float32 → float64
    (double)wp.lon,    // float32 → float64
    wp.alt,            // float32
    record.chain_hash, // 32-byte output
    &record.chain_seq  // sequence counter output
);
```

Inside TrustZone, the TA:
1. Builds 52-byte input: `prev_hash + lat + lon + alt`
2. Computes `SHA256(input)` entirely in secure world
3. Updates `g_chain.hash` (state stays in secure world)
4. Returns new hash and sequence number to normal world

### 3. On Landing

```
wp_flight_chain.sha256 written to PX4_STORAGEDIR
Contents: 64 hex characters = 32 bytes = final chain hash
```

---

## uORB Topics

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `navigator_mission_item` | Subscribe (callback) | Cache planned coords |
| `vehicle_land_detected` | Subscribe (callback) | Detect takeoff / landing |
| `mission_result` | Subscribe (polled) | Trigger hash on seq_reached |
| `wp_hash_chain` | Publish | Log each waypoint hash to .ulg |

---

## Build

```bash
# Enable in board config (already done for SITL)
# boards/px4/sitl/default.px4board:
#   CONFIG_MODULES_WP_HASH_CHAIN=y

# Build
cd /home/jetson/PX4-Autopilot
make px4_sitl_default

# Build and deploy TA (if TA source changed)
cd optee_ta/mission_ta
make clean && make
sudo cp out/ta/8aaaf200-2450-11e4-abe2-0002a5d5c51b.ta /usr/lib/optee_armtz/
sudo cp out/ta/8aaaf200-2450-11e4-abe2-0002a5d5c51b.ta /lib/optee_armtz/
```

---

## Run

### Terminal 1 — Start SITL
```bash
cd /home/jetson/PX4-Autopilot
./build/px4_sitl_default/bin/px4 build/px4_sitl_default/rootfs
```

### PX4 Shell — Start Module
```bash
pxh> wp_hash_chain start
```

### Terminal 2 — Fly and Compare (automated)
```bash
python3 Tools/run_chain_mission.py
```

### Or — Compute Ground Chain Only
```bash
python3 Tools/waypoint_chain.py Tools/mc_mission_5wp.plan
```

### Or — Compare After Manual Flight
```bash
python3 Tools/waypoint_chain.py Tools/mc_mission_5wp.plan \
  --compare build/px4_sitl_default/rootfs/wp_flight_chain.sha256
```

---

## Module Commands

```bash
wp_hash_chain start     # Start the module
wp_hash_chain stop      # Stop the module
wp_hash_chain status    # Print current state
wp_hash_chain hash      # Print full 32-byte chain hash from TA
```

**`status` output:**
```
INFO  OP-TEE active  : YES (TrustZone)
INFO  flight state   : AIRBORNE
INFO  chain seq      : 4 waypoints hashed
INFO  last wp reached: 3
INFO  chain hash     : 88dc3053f77d9eb4..
```

---

## Expected Output (Successful Test)

```
=== Planned Hash Chain (ground side) ===
 Seq  Cmd        Lat           Lon       Alt  Hash
   0  TAKEOFF    47.397742     8.545621  20.0  88dc3053..
   1  WAYPOINT   47.398500     8.546200  25.0  64721440..
   2  WAYPOINT   47.399100     8.545100  30.0  b8142ca8..
   3  WAYPOINT   47.399000     8.543800  30.0  33fdcc1f..
   4  WAYPOINT   47.397800     8.543500  25.0  0844c0e1..

INFO  [wp_hash_chain] WP0 REACHED — hashing planned (47.397743, 8.545621, 20.0)
INFO  [wp_hash_chain] WP0 hash: 88dc3053.. (9800 us)
INFO  [wp_hash_chain] WP1 REACHED — hashing planned (47.398499, 8.546200, 25.0)
INFO  [wp_hash_chain] WP1 hash: 64721440.. (10200 us)
...
INFO  [wp_hash_chain] Landing — chain sealed: 5 waypoints

========================================
Planned (ground):  0844c0e190e05413...
Flight  (OP-TEE):  0844c0e190e05413...

RESULT: MATCH — drone flew exactly the planned mission ✅
```


