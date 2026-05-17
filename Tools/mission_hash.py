import json
import hashlib
import argparse

def canonical_mission_blob(plan):
    data = []

    items = plan['mission'].get('items', [])
    data.append("{}\n".format(len(items)))

    for item in items:
        frame = int(item.get("frame", 0))
        command = int(item.get("command", 0))
        auto_continue = 1 if item.get("autoContinue", False) else 0

        params = [p if p is not None else 0.0 for p in item.get("params", [])]
        while len(params) < 7:
            params.append(0.0)

        # MAVLink stores lat=params[4], lon=params[5], alt=params[6]
        # Fall back to named keys for plan formats that use them directly
        x = float(item.get("x", item.get("lat", params[4])))
        y = float(item.get("y", item.get("lon", params[5])))
        z = float(item.get("z", item.get("Altitude", params[6])))

        data.append("{},{},{},{:.6f},{:.6f},{:.6f},{:.6f},{:.8f},{:.8f},{:.3f}\n".format(
            frame, command, auto_continue,
            float(params[0]), float(params[1]),
            float(params[2]), float(params[3]),
            x, y, z
        ))

    return "".join(data).encode("utf-8")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("plan", help="path to .plan file")
    parser.add_argument("out", nargs="?", default="mission.sha256", help="output hash file (default: mission.sha256)")
    args = parser.parse_args()

    with open(args.plan, "r") as f:
        plan = json.load(f)

    digest = hashlib.sha256(canonical_mission_blob(plan)).hexdigest()
    with open(args.out, "w") as f:
        f.write(digest)

if __name__ == "__main__":
    main()
