#ifndef __TA_MISSION_DEFINES_H
#define __TA_MISSION_DEFINES_H

#include <stdint.h>

/* OP-TEE Mission Trusted Application
 *
 * Securely stores and provides mission waypoints to normal world PX4
 * - Verifies Ed25519 signatures in secure world
 * - Returns waypoints one-at-a-time (prevents bulk extraction)
 * - Uses secure storage for mission data
 * - Provides audit trail of accesses
 */

/* TA UUID — must be unique */
#define TA_MISSION_UUID \
	{ 0x8aaaf200, 0x2450, 0x11e4, \
		{ 0xab, 0xe2, 0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b } }

/* RPC Command IDs */
#define TA_MISSION_CMD_UPLOAD          0  /* Upload mission with signature */
#define TA_MISSION_CMD_GET_WAYPOINT    1  /* Get single waypoint by index */
#define TA_MISSION_CMD_GET_COUNT       2  /* Get mission waypoint count */
#define TA_MISSION_CMD_VERIFY_STATUS   3  /* Get verification status */
#define TA_MISSION_CMD_CLEAR_MISSION   4  /* Clear stored mission (erase) */
#define TA_MISSION_CMD_VERIFY_SIG      5  /* Verify Ed25519 sig over a 32-byte hash */
/* CMD_VERIFY_SIG params:
 *   [0] MEMREF_INPUT  — signature, 64 bytes
 *   [1] MEMREF_INPUT  — SHA-256 hash, 32 bytes
 *   [2] NONE
 *   [3] NONE
 * Returns TEEC_SUCCESS if valid, TEEC_ERROR_SECURITY if invalid
 */

#define TA_MISSION_CMD_CHAIN_POS       6  /* Append a position to the hash chain */
/* CMD_CHAIN_POS params:
<<<<<<< HEAD
 *   [0] MEMREF_INPUT  — ta_chain_pos_t, 28 bytes (lat/lon/alt/timestamp)
 *   [1] MEMREF_OUTPUT — new chain hash, 32 bytes
 *   [2] VALUE_OUTPUT  — a = chain sequence number
 *   [3] NONE
 * Secure world computes: new_hash = SHA256(prev_hash || lat || lon || alt || timestamp)
=======
 *   [0] MEMREF_INPUT  — ta_chain_pos_t, 20 bytes (lat/lon/alt only)
 *   [1] MEMREF_OUTPUT — new chain hash, 32 bytes
 *   [2] VALUE_OUTPUT  — a = chain sequence number
 *   [3] NONE
 * Secure world computes: new_hash = SHA256(prev_hash || lat || lon || alt)
>>>>>>> 2e514d9d17 (PX-4)
 * Stores new_hash in TA state. Returns new_hash + seq.
 */

#define TA_MISSION_CMD_GET_CHAIN_HASH  7  /* Read current chain hash without advancing */
/* CMD_GET_CHAIN_HASH params:
 *   [0] MEMREF_OUTPUT — current chain hash, 32 bytes
 *   [1] VALUE_OUTPUT  — a = chain sequence number
 *   [2] NONE
 *   [3] NONE
 */

#define TA_MISSION_CMD_RESET_CHAIN     8  /* Reset chain to initial state */
/* CMD_RESET_CHAIN params: all NONE — clears chain_hash and chain_seq to zero */

/* Position data sent from normal world for hash chaining */
typedef struct {
<<<<<<< HEAD
	double   lat;           /* degrees */
	double   lon;           /* degrees */
	float    alt;           /* metres AMSL */
	uint64_t timestamp_us;  /* PX4 system time in microseconds */
} ta_chain_pos_t;           /* 28 bytes total */
=======
	double lat;   /* degrees */
	double lon;   /* degrees */
	float  alt;   /* metres above home */
} ta_chain_pos_t;  /* 20 bytes: lat(8) + lon(8) + alt(4) */
>>>>>>> 2e514d9d17 (PX-4)

/* Maximum mission waypoints */
#define TA_MISSION_MAX_ITEMS           500

/* Mission item structure (same as PX4's mission_item_s) */
typedef struct {
	uint16_t command;
	float params[7];
	int32_t x;                     /* latitude or x in 1e7 */
	int32_t y;                     /* longitude or y in 1e7 */
	float z;                       /* altitude or z */
	uint8_t frame;
	uint8_t autocontinue;
	uint8_t type;
	uint8_t reserved;
} ta_mission_item_t;

/* Upload request (normal world → secure world)
 * Params:
 *   [0] = signature data (IN, 64 bytes)
 *   [1] = mission blob (IN, binary mission items + metadata)
 *   [2] = (unused)
 *   [3] = (unused)
 *
 * Returns:
 *   TEEC_SUCCESS if signature verified and mission stored
 *   TEEC_ERROR_SECURITY if signature invalid
 */
typedef struct {
	uint8_t signature[64];         /* Ed25519 signature */
	uint8_t expected_hash[32];     /* Expected SHA-256 hash (for verification) */
	uint16_t item_count;           /* Number of mission items */
	uint32_t mission_size;         /* Total mission blob size (bytes) */
} ta_mission_upload_req_t;

/* Waypoint response (secure world → normal world)
 * Contains ONE waypoint and metadata
 */
typedef struct {
	ta_mission_item_t item;        /* The waypoint */
	uint16_t index;                /* Which waypoint this is (0..count-1) */
	uint16_t count;                /* Total waypoints in mission */
	uint32_t timestamp_ms;         /* When this was returned (TA time) */
} ta_mission_waypoint_resp_t;

/* Status response */
typedef struct {
	uint8_t status;                /* 0=no mission, 1=verified, 2=failed */
	uint8_t hash[32];              /* SHA-256 of current mission */
	uint16_t count;                /* Waypoint count */
	uint32_t uploaded_at_ms;
} ta_mission_status_resp_t;

/* Ed25519 public key (embedded in TA firmware) */
extern const uint8_t ta_mission_pubkey[32];

#endif /* __TA_MISSION_DEFINES_H */
