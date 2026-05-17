#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <string.h>
#include <stdio.h>
#include "ta_mission_defines.h"

/* Ed25519 public key embedded in TA (from mission signing)
 * This should match MISSION_SIGNING_PUBLIC_KEY in missionHashCheck.cpp
 */
const uint8_t ta_mission_pubkey[32] = {
	0x00, 0xad, 0xb8, 0x61, 0xe0, 0x08, 0x8e, 0x34, 0xe4, 0x48, 0x59, 0x3a, 0x9b, 0x55, 0x90, 0xbd,
	0x7d, 0x8c, 0xca, 0x67, 0x09, 0x27, 0x41, 0x7c, 0x67, 0x87, 0xf2, 0x78, 0xdf, 0xad, 0x9b, 0xf7
};

/* Storage object ID for mission */
#define MISSION_STORAGE_ID "mission_secure_store"

/* TA state — stored in secure memory (not persistent) */
static struct {
	ta_mission_item_t items[TA_MISSION_MAX_ITEMS];
	uint16_t count;
	uint8_t hash[32];
	uint32_t uploaded_at;
	uint8_t status;                     /* 0=none, 1=verified, 2=sig_failed */
	uint32_t last_accessed;
} g_mission = {0};

/* Hash chain state — tamper-evident position log */
static struct {
	uint8_t  hash[32];   /* running chain hash, updated on every CMD_CHAIN_POS */
	uint32_t seq;        /* monotonically increasing sequence counter */
} g_chain = {0};

/* ============================================================================
 * Ed25519 Verification using OP-TEE native crypto API
 * ============================================================================
 * Uses TEE_AsymmetricVerifyDigest with TEE_ALG_ED25519 (GP TEE API 1.3.1+,
 * supported in OP-TEE 3.6+ which is present on this Jetson).
 *
 * The message is the 32-byte SHA-256 hash of the canonical mission blob.
 * The public key is the 32-byte Ed25519 public key embedded in the TA.
 */
static TEE_Result ta_verify_ed25519_signature(
	const uint8_t *sig,            /* 64-byte Ed25519 signature */
	const uint8_t *hash,           /* 32-byte message (SHA-256 hash) */
	const uint8_t *pubkey)         /* 32-byte Ed25519 public key */
{
	TEE_Result res;
	TEE_ObjectHandle pub_key = TEE_HANDLE_NULL;
	TEE_OperationHandle op   = TEE_HANDLE_NULL;
	TEE_Attribute attr;

	/* Import the 32-byte raw public key into a transient key object */
	res = TEE_AllocateTransientObject(TEE_TYPE_ED25519_PUBLIC_KEY, 256, &pub_key);
	if (res != TEE_SUCCESS) {
		EMSG("AllocateTransientObject(ED25519_PUB) failed: %#x", res);
		return res;
	}

	TEE_InitRefAttribute(&attr, TEE_ATTR_ED25519_PUBLIC_VALUE,
			     (void *)(uintptr_t)pubkey, 32);

	res = TEE_PopulateTransientObject(pub_key, &attr, 1);
	if (res != TEE_SUCCESS) {
		EMSG("PopulateTransientObject failed: %#x", res);
		TEE_FreeTransientObject(pub_key);
		return res;
	}

	/* Allocate a verify operation for Ed25519 */
	res = TEE_AllocateOperation(&op, TEE_ALG_ED25519, TEE_MODE_VERIFY, 256);
	if (res != TEE_SUCCESS) {
		EMSG("AllocateOperation(ED25519,VERIFY) failed: %#x", res);
		TEE_FreeTransientObject(pub_key);
		return res;
	}

	res = TEE_SetOperationKey(op, pub_key);
	if (res != TEE_SUCCESS) {
		EMSG("SetOperationKey failed: %#x", res);
		goto out;
	}

	/* Verify the 64-byte signature over the 32-byte hash */
	res = TEE_AsymmetricVerifyDigest(op, NULL, 0,
					 (void *)(uintptr_t)hash, 32,
					 (void *)(uintptr_t)sig,  64);
	if (res == TEE_SUCCESS) {
		IMSG("Ed25519 verification: OK");
	} else {
		EMSG("Ed25519 verification: FAILED (%#x)", res);
	}

out:
	TEE_FreeOperation(op);
	TEE_FreeTransientObject(pub_key);
	return res;
}

/* ============================================================================
 * SHA-256 Verification Helper
 * ============================================================================
 */
static TEE_Result ta_verify_mission_hash(
	const uint8_t *mission_data,
	uint32_t mission_size,
	const uint8_t *expected_hash)
{
	uint8_t computed_hash[32];
	size_t hash_len = 32;
	TEE_OperationHandle hash_op = TEE_HANDLE_NULL;
	TEE_Result res;

	res = TEE_AllocateOperation(&hash_op, TEE_ALG_SHA256, TEE_MODE_DIGEST, 0);
	if (res != TEE_SUCCESS) {
		EMSG("AllocateOperation failed: %#x", res);
		return res;
	}

	TEE_DigestUpdate(hash_op, mission_data, mission_size);
	res = TEE_DigestDoFinal(hash_op, NULL, 0, computed_hash, &hash_len);
	TEE_FreeOperation(hash_op);

	if (res != TEE_SUCCESS) {
		EMSG("DigestDoFinal failed: %#x", res);
		return res;
	}

	if (TEE_MemCompare(computed_hash, expected_hash, 32) != 0) {
		EMSG("Mission hash mismatch!");
		return TEE_ERROR_SECURITY;
	}

	TEE_MemMove(g_mission.hash, computed_hash, 32);
	return TEE_SUCCESS;
}

/* ============================================================================
 * Command Handlers
 * ============================================================================
 */

static TEE_Result cmd_upload_mission(
	uint32_t param_types,
	TEE_Param params[4])
{
	uint32_t exp_param_types = TEE_PARAM_TYPES(
		TEE_PARAM_TYPE_MEMREF_INPUT,   /* [0] = signature (64 bytes) */
		TEE_PARAM_TYPE_MEMREF_INPUT,   /* [1] = mission blob */
		TEE_PARAM_TYPE_VALUE_INPUT,    /* [2] = count and hash pointer */
		TEE_PARAM_TYPE_VALUE_OUTPUT);  /* [3] = status out */

	if (param_types != exp_param_types) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	/* Extract parameters */
	uint8_t *sig_data = (uint8_t *)params[0].memref.buffer;
	uint32_t sig_len = params[0].memref.size;
	uint8_t *mission_blob = (uint8_t *)params[1].memref.buffer;
	uint32_t mission_size = params[1].memref.size;
	uint8_t *expected_hash = (uint8_t *)((uint64_t)params[2].value.a);
	uint16_t item_count = (uint16_t)params[2].value.b;

	IMSG("MISSION_UPLOAD: %u items, %u bytes, sig_len=%u",
		 item_count, mission_size, sig_len);

	/* Validate inputs */
	if (sig_len != 64) {
		EMSG("Invalid signature length: %u (expected 64)", sig_len);
		params[3].value.a = 2;  /* sig_failed */
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (item_count > TA_MISSION_MAX_ITEMS) {
		EMSG("Too many items: %u (max %u)", item_count, TA_MISSION_MAX_ITEMS);
		params[3].value.a = 2;
		return TEE_ERROR_BAD_PARAMETERS;
	}

	/* Step 1: Verify mission hash first (integrity check) */
	TEE_Result res = ta_verify_mission_hash(mission_blob, mission_size, expected_hash);
	if (res != TEE_SUCCESS) {
		EMSG("Mission hash verification failed");
		g_mission.status = 2;  /* failed */
		params[3].value.a = 2;
		return TEE_ERROR_SECURITY;
	}

	/* Step 2: Verify Ed25519 signature (authenticity check) */
	res = ta_verify_ed25519_signature(sig_data, expected_hash, ta_mission_pubkey);
	if (res != TEE_SUCCESS) {
		EMSG("Ed25519 signature verification failed");
		g_mission.status = 2;  /* failed */
		params[3].value.a = 2;
		return TEE_ERROR_SECURITY;
	}

	/* Step 3: Parse and store mission items in secure memory */
	/* Mission blob format: [count: u16][item1][item2]... */
	uint8_t *ptr = mission_blob;
	uint16_t blob_count = 0;
	TEE_MemMove(&blob_count, ptr, 2);
	ptr += 2;

	if (blob_count != item_count) {
		EMSG("Item count mismatch: blob says %u, param says %u",
			 blob_count, item_count);
		g_mission.status = 2;
		params[3].value.a = 2;
		return TEE_ERROR_BAD_PARAMETERS;
	}

	/* Copy mission items into secure TA memory */
	g_mission.count = item_count;
	for (uint16_t i = 0; i < item_count; i++) {
		if (ptr + sizeof(ta_mission_item_t) > mission_blob + mission_size) {
			EMSG("Mission blob truncated at item %u", i);
			g_mission.status = 2;
			params[3].value.a = 2;
			return TEE_ERROR_BAD_PARAMETERS;
		}

		TEE_MemMove(&g_mission.items[i], ptr, sizeof(ta_mission_item_t));
		ptr += sizeof(ta_mission_item_t);
	}

	g_mission.status = 1;   /* verified */
	{ TEE_Time _t; TEE_GetSystemTime(&_t); g_mission.uploaded_at = _t.seconds; }

	IMSG("Mission stored: %u waypoints, status=VERIFIED", g_mission.count);

	params[3].value.a = 1;  /* status = verified */
	return TEE_SUCCESS;
}

static TEE_Result cmd_get_waypoint(
	uint32_t param_types,
	TEE_Param params[4])
{
	uint32_t exp_param_types = TEE_PARAM_TYPES(
		TEE_PARAM_TYPE_VALUE_INPUT,    /* [0].a = waypoint index */
		TEE_PARAM_TYPE_MEMREF_OUTPUT,  /* [1] = response buffer */
		TEE_PARAM_TYPE_NONE,
		TEE_PARAM_TYPE_NONE);

	if (param_types != exp_param_types) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	uint16_t index = (uint16_t)params[0].value.a;
	ta_mission_waypoint_resp_t *resp =
		(ta_mission_waypoint_resp_t *)params[1].memref.buffer;
	uint32_t resp_size = params[1].memref.size;

	if (resp_size < sizeof(ta_mission_waypoint_resp_t)) {
		EMSG("Response buffer too small: %u < %u",
			 resp_size, (uint32_t)sizeof(ta_mission_waypoint_resp_t));
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (g_mission.status != 1) {
		EMSG("No verified mission (status=%u)", g_mission.status);
		return TEE_ERROR_BAD_STATE;
	}

	if (index >= g_mission.count) {
		EMSG("Waypoint index out of range: %u >= %u", index, g_mission.count);
		return TEE_ERROR_OUT_OF_MEMORY;
	}

	/* Build response — only ONE waypoint at a time */
	resp->index = index;
	resp->count = g_mission.count;
	{ TEE_Time _t; TEE_GetSystemTime(&_t); resp->timestamp_ms = _t.seconds; }
	TEE_MemMove(&resp->item, &g_mission.items[index],
				sizeof(ta_mission_item_t));

	{ TEE_Time _t; TEE_GetSystemTime(&_t); g_mission.last_accessed = _t.seconds; }

	IMSG("GET_WAYPOINT: index=%u/%u", index, g_mission.count);

	return TEE_SUCCESS;
}

static TEE_Result cmd_get_count(
	uint32_t param_types,
	TEE_Param params[4])
{
	uint32_t exp_param_types = TEE_PARAM_TYPES(
		TEE_PARAM_TYPE_VALUE_OUTPUT,
		TEE_PARAM_TYPE_NONE,
		TEE_PARAM_TYPE_NONE,
		TEE_PARAM_TYPE_NONE);

	if (param_types != exp_param_types) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (g_mission.status != 1) {
		params[0].value.a = 0;  /* No mission */
		IMSG("GET_COUNT: status not verified, returning 0");
		return TEE_SUCCESS;
	}

	params[0].value.a = g_mission.count;
	IMSG("GET_COUNT: %u", g_mission.count);
	return TEE_SUCCESS;
}

static TEE_Result cmd_verify_status(
	uint32_t param_types,
	TEE_Param params[4])
{
	uint32_t exp_param_types = TEE_PARAM_TYPES(
		TEE_PARAM_TYPE_MEMREF_OUTPUT,  /* [0] = status response */
		TEE_PARAM_TYPE_NONE,
		TEE_PARAM_TYPE_NONE,
		TEE_PARAM_TYPE_NONE);

	if (param_types != exp_param_types) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	ta_mission_status_resp_t *status_resp =
		(ta_mission_status_resp_t *)params[0].memref.buffer;

	if (params[0].memref.size < sizeof(ta_mission_status_resp_t)) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	status_resp->status = g_mission.status;
	status_resp->count = g_mission.count;
	status_resp->uploaded_at_ms = g_mission.uploaded_at;
	TEE_MemMove(status_resp->hash, g_mission.hash, 32);

	IMSG("VERIFY_STATUS: status=%u, count=%u",
		 g_mission.status, g_mission.count);

	return TEE_SUCCESS;
}

static TEE_Result cmd_clear_mission(
	uint32_t param_types,
	TEE_Param params[4])
{
	uint32_t exp_param_types = TEE_PARAM_TYPES(
		TEE_PARAM_TYPE_NONE,
		TEE_PARAM_TYPE_NONE,
		TEE_PARAM_TYPE_NONE,
		TEE_PARAM_TYPE_NONE);

	if (param_types != exp_param_types) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	TEE_MemFill(&g_mission, 0, sizeof(g_mission));
	IMSG("Mission cleared from secure memory");
	return TEE_SUCCESS;
}

/* ============================================================================
 * CMD_VERIFY_SIG — verify an Ed25519 signature in secure world
 * Params: [0]=sig(64B MEMREF_IN), [1]=hash(32B MEMREF_IN), [2]=NONE, [3]=NONE
 * ============================================================================
 */
static TEE_Result cmd_verify_sig(uint32_t param_types, TEE_Param params[4])
{
	uint32_t exp = TEE_PARAM_TYPES(
			TEE_PARAM_TYPE_MEMREF_INPUT,
			TEE_PARAM_TYPE_MEMREF_INPUT,
			TEE_PARAM_TYPE_NONE,
			TEE_PARAM_TYPE_NONE);

	if (param_types != exp) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (params[0].memref.size != 64 || params[1].memref.size != 32) {
		EMSG("VERIFY_SIG: bad sizes sig=%u hash=%u",
		     (unsigned int)params[0].memref.size, (unsigned int)params[1].memref.size);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	const uint8_t *sig  = (const uint8_t *)params[0].memref.buffer;
	const uint8_t *hash = (const uint8_t *)params[1].memref.buffer;

	IMSG("VERIFY_SIG: sig=%02x%02x.. hash=%02x%02x..",
	     sig[0], sig[1], hash[0], hash[1]);

	return ta_verify_ed25519_signature(sig, hash, ta_mission_pubkey);
}

/* ============================================================================
 * CMD_CHAIN_POS — append one position record to the tamper-evident hash chain
 *
 * Hash step: new_hash = SHA256(prev_hash[32] || lat[8] || lon[8] || alt[4] || ts[8])
 * All state lives in TrustZone. Normal world gets the new hash and seq number.
 * ============================================================================ */
static TEE_Result cmd_chain_pos(uint32_t param_types, TEE_Param params[4])
{
	uint32_t exp = TEE_PARAM_TYPES(
		TEE_PARAM_TYPE_MEMREF_INPUT,   /* [0] ta_chain_pos_t (28 bytes) */
		TEE_PARAM_TYPE_MEMREF_OUTPUT,  /* [1] new chain hash (32 bytes) */
		TEE_PARAM_TYPE_VALUE_OUTPUT,   /* [2] .a = chain sequence number */
		TEE_PARAM_TYPE_NONE);

	if (param_types != exp) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (params[0].memref.size != sizeof(ta_chain_pos_t)) {
		EMSG("CHAIN_POS: bad input size %u", (unsigned int)params[0].memref.size);
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (params[1].memref.size < 32) {
		return TEE_ERROR_SHORT_BUFFER;
	}

	const ta_chain_pos_t *pos = (const ta_chain_pos_t *)params[0].memref.buffer;

	/* Build hash input: prev_hash(32) || lat(8) || lon(8) || alt(4) || timestamp(8) */
	uint8_t input[60];
	uint8_t *p = input;
	TEE_MemMove(p, g_chain.hash, 32);      p += 32;
	TEE_MemMove(p, &pos->lat, 8);          p += 8;
	TEE_MemMove(p, &pos->lon, 8);          p += 8;
	TEE_MemMove(p, &pos->alt, 4);          p += 4;
	TEE_MemMove(p, &pos->timestamp_us, 8); p += 8;

	/* SHA-256 entirely inside secure world */
	TEE_OperationHandle hash_op = TEE_HANDLE_NULL;
	TEE_Result res = TEE_AllocateOperation(&hash_op, TEE_ALG_SHA256, TEE_MODE_DIGEST, 0);
	if (res != TEE_SUCCESS) {
		EMSG("CHAIN_POS: AllocateOperation failed %#x", res);
		return res;
	}

	size_t hash_len = 32;
	TEE_DigestUpdate(hash_op, input, sizeof(input));
	res = TEE_DigestDoFinal(hash_op, NULL, 0, g_chain.hash, &hash_len);
	TEE_FreeOperation(hash_op);

	if (res != TEE_SUCCESS) {
		EMSG("CHAIN_POS: DigestDoFinal failed %#x", res);
		return res;
	}

	g_chain.seq++;
	TEE_MemMove(params[1].memref.buffer, g_chain.hash, 32);
	params[2].value.a = g_chain.seq;

	IMSG("CHAIN_POS: seq=%u hash=%02x%02x%02x%02x..",
	     g_chain.seq,
	     g_chain.hash[0], g_chain.hash[1], g_chain.hash[2], g_chain.hash[3]);

	return TEE_SUCCESS;
}

/* CMD_GET_CHAIN_HASH — read current chain state without advancing it */
static TEE_Result cmd_get_chain_hash(uint32_t param_types, TEE_Param params[4])
{
	uint32_t exp = TEE_PARAM_TYPES(
		TEE_PARAM_TYPE_MEMREF_OUTPUT,
		TEE_PARAM_TYPE_VALUE_OUTPUT,
		TEE_PARAM_TYPE_NONE,
		TEE_PARAM_TYPE_NONE);

	if (param_types != exp) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (params[0].memref.size < 32) {
		return TEE_ERROR_SHORT_BUFFER;
	}

	TEE_MemMove(params[0].memref.buffer, g_chain.hash, 32);
	params[1].value.a = g_chain.seq;
	return TEE_SUCCESS;
}

/* CMD_RESET_CHAIN — clear chain back to genesis (all-zeros) */
static TEE_Result cmd_reset_chain(uint32_t param_types, TEE_Param params[4] __unused)
{
	uint32_t exp = TEE_PARAM_TYPES(
		TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
		TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);

	if (param_types != exp) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	TEE_MemFill(&g_chain, 0, sizeof(g_chain));
	IMSG("CHAIN_RESET: chain cleared to genesis");
	return TEE_SUCCESS;
}

/* ============================================================================
 * OP-TEE TA Entry Points
 * ============================================================================
 */

TEE_Result TA_CreateEntryPoint(void)
{
	IMSG("Mission TA: CreateEntryPoint");
	TEE_MemFill(&g_mission, 0, sizeof(g_mission));
	return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void)
{
	IMSG("Mission TA: DestroyEntryPoint");
}

TEE_Result TA_OpenSessionEntryPoint(
	uint32_t param_types,
	TEE_Param __unused *params,
	void __unused **sess_ctx)
{
	IMSG("Mission TA: OpenSession");
	return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void __unused *sess_ctx)
{
	IMSG("Mission TA: CloseSession");
}

TEE_Result TA_InvokeCommandEntryPoint(
	void __unused *sess_ctx,
	uint32_t cmd_id,
	uint32_t param_types,
	TEE_Param params[4])
{
	switch (cmd_id) {
	case TA_MISSION_CMD_UPLOAD:
		return cmd_upload_mission(param_types, params);
	case TA_MISSION_CMD_GET_WAYPOINT:
		return cmd_get_waypoint(param_types, params);
	case TA_MISSION_CMD_GET_COUNT:
		return cmd_get_count(param_types, params);
	case TA_MISSION_CMD_VERIFY_STATUS:
		return cmd_verify_status(param_types, params);
	case TA_MISSION_CMD_CLEAR_MISSION:
		return cmd_clear_mission(param_types, params);
	case TA_MISSION_CMD_VERIFY_SIG:
		return cmd_verify_sig(param_types, params);
	case TA_MISSION_CMD_CHAIN_POS:
		return cmd_chain_pos(param_types, params);
	case TA_MISSION_CMD_GET_CHAIN_HASH:
		return cmd_get_chain_hash(param_types, params);
	case TA_MISSION_CMD_RESET_CHAIN:
		return cmd_reset_chain(param_types, params);
	default:
		EMSG("Unknown command %#x", cmd_id);
		return TEE_ERROR_BAD_PARAMETERS;
	}
}
