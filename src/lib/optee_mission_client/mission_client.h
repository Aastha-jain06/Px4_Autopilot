#ifndef __MISSION_CLIENT_H__
#define __MISSION_CLIENT_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mission Client Library — Interface to OP-TEE Mission TA
 *
 * Usage:
 *   1. mission_client_init() — load TA and open session
 *   2. mission_client_upload_mission(sig, hash, blob, count) — upload and verify
 *   3. mission_client_get_waypoint(index) — retrieve one waypoint
 *   4. mission_client_get_count() — get total waypoints
 *   5. mission_client_cleanup() — close session
 *
 * All APIs are blocking. TA verifies signature and stores mission in secure memory.
 */

/* Return codes */
#define MISSION_CLIENT_OK                0
#define MISSION_CLIENT_ERROR_INIT       -1  /* Failed to init TA */
#define MISSION_CLIENT_ERROR_SESSION    -2  /* Failed to open session */
#define MISSION_CLIENT_ERROR_SECURITY   -3  /* Signature verification failed */
#define MISSION_CLIENT_ERROR_BAD_PARAM  -4  /* Invalid parameters */
#define MISSION_CLIENT_ERROR_NO_MISSION -5  /* No mission loaded */
#define MISSION_CLIENT_ERROR_OUT_RANGE  -6  /* Index out of range */
#define MISSION_CLIENT_ERROR_RPC        -7  /* TA communication error */

/* Mission item (must match OP-TEE TA definition) */
typedef struct {
	uint16_t command;
	float params[7];
	int32_t x;
	int32_t y;
	float z;
	uint8_t frame;
	uint8_t autocontinue;
	uint8_t type;
	uint8_t reserved;
} mission_client_item_t;

/* Waypoint response */
typedef struct {
	mission_client_item_t item;
	uint16_t index;
	uint16_t count;
	uint32_t timestamp_ms;
} mission_client_waypoint_t;

/* Status response */
typedef struct {
	uint8_t status;         /* 0=none, 1=verified, 2=failed */
	uint8_t hash[32];       /* SHA-256 */
	uint16_t count;
	uint32_t uploaded_at_ms;
} mission_client_status_t;

/* ===== Public API ===== */

/**
 * Initialize the mission client (load TA, open session)
 * @return MISSION_CLIENT_OK on success
 */
int mission_client_init(void);

/**
 * Cleanup (close session, free resources)
 */
void mission_client_cleanup(void);

/**
 * Upload mission with signature
 *
 * @param signature      Ed25519 signature (64 bytes)
 * @param expected_hash  SHA-256 hash of mission blob (32 bytes)
 * @param mission_blob   Binary mission data (items in native format)
 * @param mission_size   Size of mission_blob in bytes
 * @param item_count     Number of mission items
 *
 * @return MISSION_CLIENT_OK if signature verified and mission stored
 *         MISSION_CLIENT_ERROR_SECURITY if signature invalid
 *         Other error codes on other failures
 *
 * Mission is stored in TA's secure memory. Waypoints can then be
 * retrieved one-at-a-time via mission_client_get_waypoint().
 */
int mission_client_upload_mission(
	const uint8_t *signature,
	const uint8_t *expected_hash,
	const uint8_t *mission_blob,
	size_t mission_size,
	uint16_t item_count);

/**
 * Get a single waypoint from the uploaded mission
 *
 * @param index  Waypoint index (0 to count-1)
 * @param wp     Output: waypoint data
 *
 * @return MISSION_CLIENT_OK on success
 *         MISSION_CLIENT_ERROR_NO_MISSION if no mission loaded
 *         MISSION_CLIENT_ERROR_OUT_RANGE if index >= count
 */
int mission_client_get_waypoint(uint16_t index, mission_client_waypoint_t *wp);

/**
 * Get mission waypoint count
 * @return Number of waypoints (0 if no mission)
 */
uint16_t mission_client_get_count(void);

/**
 * Get mission verification status
 * @param status Output: status struct
 * @return MISSION_CLIENT_OK on success
 */
int mission_client_get_status(mission_client_status_t *status);

/**
 * Clear mission from secure memory (erase)
 * @return MISSION_CLIENT_OK on success
 */
int mission_client_clear_mission(void);

/**
 * Verify an Ed25519 signature inside the secure world (TA_MISSION_CMD_VERIFY_SIG).
 *
 * @param signature  Ed25519 signature, 64 bytes
 * @param hash       SHA-256 hash of the canonical mission blob, 32 bytes
 * @return MISSION_CLIENT_OK / MISSION_CLIENT_ERROR_SECURITY / other
 */
int mission_client_verify_sig(const uint8_t *signature, const uint8_t *hash);

/* ===== Hash Chain API ===== */

/**
 * Append one GPS position to the tamper-evident hash chain in secure world.
 *
 * Secure world computes:
 *   new_hash = SHA256(prev_hash || lat || lon || alt || timestamp_us)
 * and stores new_hash in TA state.  Normal world receives the result.
 *
 * @param lat          Latitude in degrees
 * @param lon          Longitude in degrees
 * @param alt          Altitude AMSL in metres
 * @param timestamp_us PX4 system time in microseconds
 * @param hash_out     32-byte output buffer for the new chain hash (may be NULL)
 * @param seq_out      Output sequence number (may be NULL)
 * @return MISSION_CLIENT_OK on success
 */
int mission_client_chain_pos(double lat, double lon, float alt,
			     uint64_t timestamp_us,
			     uint8_t hash_out[32], uint32_t *seq_out);

/**
 * Read the current chain hash and sequence number without advancing the chain.
 *
 * @param hash_out  32-byte output buffer
 * @param seq_out   Output sequence number (may be NULL)
 * @return MISSION_CLIENT_OK on success
 */
int mission_client_get_chain_hash(uint8_t hash_out[32], uint32_t *seq_out);

/**
 * Reset the hash chain to the genesis state (all-zeros hash, seq = 0).
 * @return MISSION_CLIENT_OK on success
 */
int mission_client_reset_chain(void);

#ifdef __cplusplus
}
#endif

#endif /* __MISSION_CLIENT_H__ */
