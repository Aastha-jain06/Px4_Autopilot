#!/usr/bin/env python3
"""
Upload test_mission.plan, arm, takeoff, fly the full mission, land.
Tests both the mission hash check (arming) and the flight hash chain (position_chain module).

Usage:
  1. Generate hash:     python3 Tools/mission_hash.py Tools/test_mission.plan
  2. Copy files:        cp mission.sha256 mission.sig build/px4_sitl_default/rootfs/
  3. Start SITL:        make px4_sitl_default gz_x500
  4. Start module:      pxh> position_chain start
  5. Run this script:   python3 Tools/test_hash.py
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
    await drone.mission.set_return_to_launch_after_mission(True)
    print("Mission uploaded — waiting 4 s for dataman write and hash check cycle...")
    await asyncio.sleep(4)

    print("Arming (triggers mission hash check)...")
    try:
        await drone.action.arm()
        print("ARMED — hash matched!")
    except Exception as e:
        print(f"Arm blocked: {e}")
        return

    # Takeoff — this triggers land_detected.landed = false
    # which starts the position_chain hash chain in TrustZone
    print("Taking off to 10 m...")
    await drone.action.takeoff()
    await asyncio.sleep(8)

    # Start the mission — drone flies the 3 waypoints
    print("Starting mission (3 waypoints)...")
    await drone.mission.start_mission()

    # Wait for mission to finish
    print("Flying mission — waiting for completion...")
    async for mission_progress in drone.mission.mission_progress():
        print(f"  Waypoint {mission_progress.current} / {mission_progress.total}")
        if mission_progress.current == mission_progress.total:
            print("Mission complete!")
            break

    # Land — triggers land_detected.landed = true
    # which seals the flight hash chain
    print("Landing (seals hash chain)...")
    await drone.action.land()

    # Wait until drone is on the ground
    print("Waiting for landing...")
    async for in_air in drone.telemetry.in_air():
        if not in_air:
            print("Landed — flight hash chain sealed!")
            break

    print("\nDone. Run in PX4 shell:")
    print("  position_chain status    <- shows final chain hash")


asyncio.run(main())
