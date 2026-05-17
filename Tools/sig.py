#!/usr/bin/env python3
"""
Upload test_mission.plan via MAVSDK and verify the mission hash/signature check allows arming.
Does NOT takeoff or fly — only tests the arming check (hash + Ed25519 sig in TrustZone).

Usage:
  1. Generate hash+sig:  python3 Tools/mission_sign.py Tools/test_mission.plan
  2. Copy files:         cp mission.sha256 mission.sig build/px4_sitl_default/rootfs/
  3. Start SITL:         ./build/px4_sitl_default/bin/px4 build/px4_sitl_default/rootfs
  4. Run this script:    python3 Tools/sig.py

Expected output on match:    "ARMED — hash matched!"
Expected output on mismatch: "Arm blocked: ..."
"""

import asyncio
from mavsdk import System
from mavsdk.mission import MissionItem, MissionPlan


async def main():
    drone = System()
    await drone.connect(system_address="udpin://0.0.0.0:14540")

    print("Connecting...")
    async for state in drone.core.connection_state():
        if state.is_connected:
            print("Connected")
            break

    nan = float('nan')
    ca  = MissionItem.CameraAction.NONE
    va  = MissionItem.VehicleAction.NONE

    # acceptance_radius_m=0.0 — prevents params[1]=3.0 mismatch that breaks the hash
    items = [
        MissionItem(47.398331, 8.545508, 20, nan, True, nan, nan, ca, nan, nan, 0.0, nan, nan, va),
        MissionItem(47.399333, 8.544815, 20, nan, True, nan, nan, ca, nan, nan, 0.0, nan, nan, va),
        MissionItem(47.399089, 8.543440, 30, nan, True, nan, nan, ca, nan, nan, 0.0, nan, nan, va),
    ]

    print("Uploading mission...")
    await drone.mission.upload_mission(MissionPlan(items))
    print("Mission uploaded — waiting 4 s for dataman write and hash check cycle...")
    await asyncio.sleep(4)

    print("Arming (triggers mission hash + signature check via TrustZone)...")
    try:
        await drone.action.arm()
        print("ARMED — hash matched!")
    except Exception as e:
        print(f"Arm blocked: {e}")


asyncio.run(main())
