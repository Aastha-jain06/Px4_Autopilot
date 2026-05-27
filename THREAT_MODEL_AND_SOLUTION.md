# Drone Mission Security — Threat Model and Solution Architecture

**Document Type:** Security Architecture  
**System:** PX4 Autopilot + OP-TEE TrustZone + CFA  
**Date:** May 2026  
**Status:** Proposed

---

## 1. Problem Statement

### 1.1 Background

Modern autonomous drones execute pre-planned GPS waypoint missions uploaded from a ground station. PX4 stores these waypoints in a file-based layer called **dataman** and navigates by continuously comparing the drone's live GPS position against the planned target.

The standard PX4 architecture runs entirely in the **normal world** — the same Linux environment that handles networking, file I/O, and user applications. This creates a fundamental trust gap: there is no hardware boundary separating mission data, verification logic, or execution records from the rest of the system.

---

### 1.2 The Problem

**A drone must be able to prove — cryptographically — that it flew exactly the mission it was given AND that the correct verification logic was executed at every step.**

Three distinct attack surfaces exist across three different phases of execution:

```
┌──────────────────────────────────────────────────────────────────────┐
│  ATTACK SURFACE 1 — BOOT TIME                                        │
│                                                                      │
│  OP-TEE TA binary replaced on disk with malicious version            │
│  → Fake TA always returns REACHED regardless of GPS position         │
│  → Secure Boot not present = no detection                            │
├──────────────────────────────────────────────────────────────────────┤
│  ATTACK SURFACE 2 — RUNTIME MEMORY                                   │
│                                                                      │
│  Normal world Linux process reads/writes secure world RAM            │
│  dataman file modified mid-flight                                    │
│  Hash chain reset and replayed with correct planned coordinates      │
│  → TrustZone not enforced = no detection                             │
├──────────────────────────────────────────────────────────────────────┤
│  ATTACK SURFACE 3 — RUNTIME EXECUTION  (the gap CFA closes)         │
│                                                                      │
│  Binary is verified ✓   Memory is isolated ✓   But:                 │
│  Memory corruption (ROP/JOP) inside TA redirects execution           │
│  Distance check function is SKIPPED via crafted pointer              │
│  TA executes hash_step() and returns REACHED without checking GPS    │
│  → Neither Secure Boot nor TrustZone detects this                   │
│  → No proof exists that verification logic actually ran              │
└──────────────────────────────────────────────────────────────────────┘
```

**Root Cause of Gap 3:**  
Secure Boot proves *what code was loaded*. TrustZone proves *what memory was protected*. Neither proves *which code path was actually executed at runtime*. An attacker who finds a memory corruption bug inside the TA can skip the distance check entirely — the binary is still the signed binary, secure world memory is still isolated, but the logic ran in the wrong order.

---

### 1.3 Requirements

| #  | Requirement |
|----|-------------|
| R1 | Only cryptographically signed code must be allowed to boot |
| R2 | Planned coordinates must be stored where normal world cannot modify them |
| R3 | The waypoint-reached decision must be made inside the hardware trust boundary |
| R4 | A cryptographic chain must link each visited waypoint — skipping is detectable |
| R5 | The execution path inside the TA must be cryptographically recorded and verifiable |
| R6 | An external auditor must be able to verify that the correct logic ran, not just that the correct result was returned |
| R7 | No new hardware — use ARM TrustZone, Secure Boot, and PAC/BTI already on the platform |

---
---

## 2. Proposed Solution

### 2.1 Solution Overview — Three Layers

```
┌─────────────────────────────────────────────────────────────────────────┐
│  LAYER            │  MECHANISM            │  WHAT IT PROVES             │
├─────────────────────────────────────────────────────────────────────────┤
│  Layer 1          │  ARM Secure Boot      │  "The correct binary        │
│  (Boot time)      │                       │   booted"                   │
│                   │                       │  Binary integrity at load   │
├─────────────────────────────────────────────────────────────────────────┤
│  Layer 2          │  ARM TrustZone        │  "Planned coords are        │
│  (Runtime memory) │  + TZASC              │   untouchable at runtime"   │
│                   │                       │  Memory isolation enforced  │
├─────────────────────────────────────────────────────────────────────────┤
│  Layer 3          │  ARM PAC/BTI          │  "The correct code path     │
│  (Runtime         │  + CFA Execution Log  │   actually ran, in the      │
│   execution)      │                       │   correct order"            │
│                   │                       │  Execution integrity proven │
└─────────────────────────────────────────────────────────────────────────┘
```

> **All three layers are required. Any one missing creates an exploitable gap.**

---

### 2.2 How The Three Layers Interlock

```
        BOOT TIME               RUNTIME MEMORY           RUNTIME EXECUTION
            │                        │                          │
            ▼                        ▼                          ▼
     Secure Boot              ARM TrustZone               PAC/BTI + CFA
            │                        │                          │
   Verifies binary            Isolates secure            PAC: MAC on every
   signature before           world RAM from             return address —
   code loads into            all normal world           overflow cannot
   secure world               DMA and processes          redirect execution
            │                        │                          │
            │                        │                   CFA: SHA256 log of
            │                        │                   every step taken —
            │                        │                   skip attack leaves
            │                        │                   detectable gap
            │                        │                          │
            └───────────┬────────────┘                          │
                        │                                        │
               Without this pair:                  Without this layer:
               attacker replaces TA or             attacker corrupts pointer
               reads secure world RAM              inside valid TA binary,
                                                   skips distance_check(),
                                                   gets REACHED for free
```

---

### 2.3 Complete Security Boundary

```
┌──────────────────────────────┬──────────────────────────────────────────┐
│  Normal World  (untrusted)   │  Secure World  (triple-protected)        │
├──────────────────────────────┼──────────────────────────────────────────┤
│  GPS readings (EKF output)   │  g_mission[]   — planned coordinates     │
│  Sequence number             │  g_expected[]  — pre-computed chain      │
│  MAVLink stack               │  g_chain       — running hash state      │
│  dataman storage             │  g_cfa_log     — execution trace log     │
│  uORB topics                 │  Ed25519 public key                      │
│  Result logging              │  Haversine + SHA-256 functions           │
├──────────────────────────────┼──────────────────────────────────────────┤
│  Protected by:               │  Protected by:                           │
│  Nothing — untrusted         │  Secure Boot:  binary verified at boot   │
│                              │  TrustZone:    memory isolated runtime   │
│                              │  PAC/BTI+CFA:  execution path recorded   │
└──────────────────────────────┴──────────────────────────────────────────┘

  Input to TA    →  GPS lat, lon, alt  +  sequence number
  Output from TA →  REACHED / NOT_REACHED / CHAIN_MISMATCH
  At landing     →  flight chain hash  +  CFA execution log hash
  Never exposed  →  planned coordinates, chain state, keys
```

---

### 2.4 Threat Mitigation Table

| Threat | Secure Boot | TrustZone | PAC/BTI + CFA |
|--------|-------------|-----------|----------------|
| TA binary replaced on disk | ✓ Halts at boot | — | — |
| Kernel patched to bypass TZASC | ✓ Halts at boot | — | — |
| Dataman modified mid-flight | — | ✓ Planned coords in secure world | — |
| Fake GPS sent to TEE | — | ✓ Compared against `g_mission[]` | — |
| Chain reset + replay | — | ✓ Authenticated nonce required | — |
| ROP/JOP inside TA skips distance check | — | — | ✓ PAC blocks return redirect; CFA log missing `DIST_CHECK` entry → audit fails |
| Distance check silently bypassed | — | — | ✓ CFA log hash mismatch at ground station |
| Rogue module fakes `REACHED` | ✓ Unsigned binary rejected | ✓ TA owns decision | ✓ CFA log proves logic ran |
| GPS RF spoofing | Out of scope | Out of scope | Out of scope — hardware attack |

---

### 2.5 Why Each Layer Is Necessary And Sufficient For Its Threat

```
┌──────────────────────────────────────────────────────────────────────┐
│                                                                      │
│  Secure Boot ALONE (no TrustZone):                                  │
│    TA code verified ✓                                                │
│    But planned coords sit in normal world RAM                        │
│    → Attacker modifies g_mission[] at runtime                        │
│    → Still vulnerable                                                │
│                                                                      │
│  TrustZone ALONE (no Secure Boot):                                  │
│    Planned coords isolated at runtime ✓                              │
│    But TA binary can be replaced on disk before boot                 │
│    → Fake TA loads into secure world                                 │
│    → Always returns REACHED                                          │
│    → System bypassed                                                 │
│                                                                      │
│  PAC/BTI + CFA ALONE (no Secure Boot or TrustZone):                │
│    Execution path recorded ✓                                         │
│    But attacker can replace the TA that writes the log               │
│    Or read/modify g_cfa_log in normal world RAM                      │
│    → Log itself is untrustworthy                                     │
│                                                                      │
│  All Three Together:                                                 │
│    TA binary verified before it loads           ✓  (Secure Boot)    │
│    Planned coords isolated after it loads       ✓  (TrustZone)      │
│    Execution path proven after it runs          ✓  (PAC/BTI + CFA)  │
│    → No exploitable gap remains in software                          │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

---

### 2.6 Audit Records Produced At Landing

Two files are written to `PX4_STORAGEDIR/` when the drone lands:

| File | Content | Proves |
|------|---------|--------|
| `wp_flight_chain.sha256` | Final SHA256 of GPS waypoint chain | WHERE the drone flew |
| `wp_cfa_log.sha256` | Final SHA256 of CFA execution log | HOW verification logic ran |

**Ground station verification:**

```
  wp_flight_chain.sha256  ==  planned_final_hash ?
  ✓ MATCH    → Drone visited the correct waypoints in correct order

  wp_cfa_log.sha256  ==  expected_cfa_log ?
  ✓ MATCH    → Distance check, hash step, and chain verify all
               executed correctly at every waypoint

  Both must match for mission to be considered verified.
  Either mismatch → tamper evidence preserved in logs for audit.
```

---

*End of Document*
