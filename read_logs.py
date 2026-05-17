#!/usr/bin/env python3
"""
Simple log reader — displays PX4 flight data in readable format
Usage: python3 read_logs.py <path_to_.ulg_file>
"""

import sys
from pyulog import ULog

if len(sys.argv) < 2:
    print("Usage: python3 read_logs.py <log_file.ulg>")
    sys.exit(1)

log_file = sys.argv[1]
print(f"\nReading log file: {log_file}\n")
log = ULog(log_file)

print("=" * 120)
print(f"LOG SUMMARY — Duration: {log.duration:.1f} seconds, Topics: {len(log.data_list)}")
print("=" * 120)

# Show all topics
print("\nAvailable Topics:")
for data in log.data_list:
    print(f"  • {data.name:<40} — {len(data.data):>6} messages")

print("\n" + "=" * 120)
print("VEHICLE_GLOBAL_POSITION — Drone's GPS Coordinates (Where it is on Earth)")
print("=" * 120)

for data in log.data_list:
    if data.name == "vehicle_global_position":
        if len(data.data) == 0:
            print("No data")
            break

        # Get start time
        start_time = data.data[0]['timestamp'] / 1e6

        # Print header
        print(f"\n{'#':<4} {'Time(s)':<10} {'Latitude':<12} {'Longitude':<12} {'Alt(m)':<10} {'H_Err(m)':<10} {'V_Err(m)':<10} {'Valid':<10}")
        print("-" * 120)

        # Print samples
        for i in range(min(15, len(data.data))):
            sample = data.data[i]
            elapsed = sample['timestamp'] / 1e6 - start_time
            print(f"{i:<4} {elapsed:<10.2f} {sample['lat']:<12.6f} {sample['lon']:<12.6f} {sample['alt']:<10.2f} {sample['eph']:<10.2f} {sample['epv']:<10.2f} {str(sample.get('lat_lon_valid', '?')):<10}")

        print(f"\n✓ Total GPS samples: {len(data.data)}")
        break

print("\n" + "=" * 120)
print("VEHICLE_LOCAL_POSITION — Position from Launch Point (NED frame)")
print("=" * 120)

for data in log.data_list:
    if data.name == "vehicle_local_position":
        if len(data.data) == 0:
            print("No data")
            break

        start_time = data.data[0]['timestamp'] / 1e6

        print(f"\n{'#':<4} {'Time(s)':<10} {'X_N(m)':<10} {'Y_E(m)':<10} {'Z_D(m)':<10} {'Vx(m/s)':<10} {'Vy(m/s)':<10} {'Vz(m/s)':<10}")
        print("-" * 120)

        for i in range(min(15, len(data.data))):
            sample = data.data[i]
            elapsed = sample['timestamp'] / 1e6 - start_time
            print(f"{i:<4} {elapsed:<10.2f} {sample['x']:<10.2f} {sample['y']:<10.2f} {sample['z']:<10.2f} {sample['vx']:<10.2f} {sample['vy']:<10.2f} {sample['vz']:<10.2f}")

        print(f"\n✓ Total local position samples: {len(data.data)}")
        break

print("\n" + "=" * 120)
print("VEHICLE_ATTITUDE — Drone's Orientation (Quaternion: w, x, y, z)")
print("=" * 120)

for data in log.data_list:
    if data.name == "vehicle_attitude":
        if len(data.data) == 0:
            print("No data")
            break

        start_time = data.data[0]['timestamp'] / 1e6

        print(f"\n{'#':<4} {'Time(s)':<10} {'q[0](w)':<10} {'q[1](x)':<10} {'q[2](y)':<10} {'q[3](z)':<10}")
        print("-" * 120)

        for i in range(min(15, len(data.data))):
            sample = data.data[i]
            elapsed = sample['timestamp'] / 1e6 - start_time
            q = sample['q']
            print(f"{i:<4} {elapsed:<10.2f} {q[0]:<10.4f} {q[1]:<10.4f} {q[2]:<10.4f} {q[3]:<10.4f}")

        print(f"\n✓ Total attitude samples: {len(data.data)}")
        break

print("\n" + "=" * 120)
print("ACTUATOR_MOTORS — Motor Control Commands (Normalized 0-1)")
print("=" * 120)

for data in log.data_list:
    if data.name == "actuator_motors":
        if len(data.data) == 0:
            print("No data")
            break

        start_time = data.data[0]['timestamp'] / 1e6

        print(f"\n{'#':<4} {'Time(s)':<10} {'Motor1':<10} {'Motor2':<10} {'Motor3':<10} {'Motor4':<10}")
        print("-" * 120)

        for i in range(min(10, len(data.data))):
            sample = data.data[i]
            elapsed = sample['timestamp'] / 1e6 - start_time
            ctrl = sample['control']
            print(f"{i:<4} {elapsed:<10.2f} {ctrl[0]:<10.3f} {ctrl[1]:<10.3f} {ctrl[2]:<10.3f} {ctrl[3]:<10.3f}")

        print(f"\n✓ Total motor control samples: {len(data.data)}")
        break

print("\n" + "=" * 120)
print("MISSION_RESULT — Waypoint/Mission Events")
print("=" * 120)

for data in log.data_list:
    if data.name == "mission_result":
        if len(data.data) == 0:
            print("No mission data logged")
            break

        for i, sample in enumerate(data.data):
            print(f"\nMission Event #{i+1}:")
            for key in sorted(sample.keys()):
                print(f"  {key:<30} = {sample[key]}")
        break

print("\n" + "=" * 120)
print("END OF LOG")
print("=" * 120)
