#!/usr/bin/env python3
"""
Generate mission.sha256 and mission.sig from a .plan file.

Usage:
  python3 Tools/mission_sign.py <plan>  [hash_out]  [--key <pem>]  [--sig <sig_out>]

Defaults:
  hash_out  = mission.sha256
  --key     = Tools/mission_privkey.pem
  --sig     = mission.sig

If the private key file is absent, only the hash file is written and a warning
is printed.  Run Tools/generate_keypair.py first to create a keypair.
"""

import json
import hashlib
import argparse
import os
import sys

try:
    from cryptography.hazmat.primitives.serialization import load_pem_private_key
except ImportError:
    load_pem_private_key = None

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_KEY = os.path.join(TOOLS_DIR, "mission_privkey.pem")


def canonical_mission_blob(plan):
    data = []
    items = plan["mission"].get("items", [])
    data.append("{}\n".format(len(items)))

    for item in items:
        frame   = int(item.get("frame", 0))
        command = int(item.get("command", 0))
        autocontinue = 1 if item.get("autoContinue", False) else 0

        params = [p if p is not None else 0.0 for p in item.get("params", [])]
        while len(params) < 7:
            params.append(0.0)

        x = float(item.get("x", item.get("lat", params[4])))
        y = float(item.get("y", item.get("lon", params[5])))
        z = float(item.get("z", item.get("Altitude", params[6])))

        data.append("{},{},{},{:.6f},{:.6f},{:.6f},{:.6f},{:.8f},{:.8f},{:.3f}\n".format(
            frame, command, autocontinue,
            float(params[0]), float(params[1]),
            float(params[2]), float(params[3]),
            x, y, z
        ))

    return "".join(data).encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("plan", help="path to .plan file")
    parser.add_argument("out", nargs="?", default="mission.sha256",
                        help="output hash file (default: mission.sha256)")
    parser.add_argument("--key", default=DEFAULT_KEY,
                        help="Ed25519 private key PEM file")
    parser.add_argument("--sig", default="mission.sig",
                        help="output signature file (default: mission.sig)")
    args = parser.parse_args()

    with open(args.plan) as f:
        plan = json.load(f)

    blob         = canonical_mission_blob(plan)
    digest_bytes = hashlib.sha256(blob).digest()
    digest_hex   = digest_bytes.hex()

    with open(args.out, "w") as f:
        f.write(digest_hex)
    print(f"Hash  : {digest_hex}")
    print(f"Wrote : {args.out}")

    # Sign if private key is available
    if not os.path.exists(args.key):
        print(f"\nWARNING: No private key at {args.key}")
        print("         Signature file NOT written.")
        print("         Run: python3 Tools/generate_keypair.py")
        return

    if load_pem_private_key is None:
        print("\nERROR: 'cryptography' package not installed.  pip3 install cryptography")
        sys.exit(1)

    with open(args.key, "rb") as f:
        privkey = load_pem_private_key(f.read(), password=None)

    sig     = privkey.sign(digest_bytes)   # Ed25519 — signs the 32-byte SHA-256 hash
    sig_hex = sig.hex()

    with open(args.sig, "w") as f:
        f.write(sig_hex)
    print(f"Sig   : {sig_hex}")
    print(f"Wrote : {args.sig}")


if __name__ == "__main__":
    main()
