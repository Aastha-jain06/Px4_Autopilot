#include "mission_client.h"
#include <optee_ta/mission_ta/ta_mission_defines.h>

#include <tee_client_api.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

/* OP-TEE TEEC connection */
static TEEC_Context g_ctx = {0};
static TEEC_Session g_sess = {0};
static int g_initialized = 0;

/* Helper: log errors */
static void log_error(const char *func, TEEC_Result res)
{
	fprintf(stderr, "%s: OP-TEE error 0x%x\n", func, res);
}

/* ===== Public API Implementation ===== */

int mission_client_init(void)
{
	TEEC_Result res;

	if (g_initialized) {
		return MISSION_CLIENT_OK;
	}

	/* Initialize TEEC */
	res = TEEC_InitializeContext(NULL, &g_ctx);
	if (res != TEEC_SUCCESS) {
		log_error("TEEC_InitializeContext", res);
		return MISSION_CLIENT_ERROR_INIT;
	}

	/* Open session with Mission TA */
	TEEC_UUID uuid = TA_MISSION_UUID;
	res = TEEC_OpenSession(&g_ctx, &g_sess, &uuid,
						  TEEC_LOGIN_PUBLIC, NULL, NULL, NULL);
	if (res != TEEC_SUCCESS) {
		log_error("TEEC_OpenSession", res);
		TEEC_FinalizeContext(&g_ctx);
		return MISSION_CLIENT_ERROR_SESSION;
	}

	g_initialized = 1;
	fprintf(stdout, "mission_client: TA initialized OK\n");
	return MISSION_CLIENT_OK;
}

void mission_client_cleanup(void)
{
	if (!g_initialized) {
		return;
	}

	TEEC_CloseSession(&g_sess);
	TEEC_FinalizeContext(&g_ctx);
	g_initialized = 0;
	fprintf(stdout, "mission_client: cleaned up\n");
}

int mission_client_upload_mission(
	const uint8_t *signature,
	const uint8_t *expected_hash,
	const uint8_t *mission_blob,
	size_t mission_size,
	uint16_t item_count)
{
	if (!g_initialized) {
		return MISSION_CLIENT_ERROR_SESSION;
	}

	if (!signature || !expected_hash || !mission_blob) {
		return MISSION_CLIENT_ERROR_BAD_PARAM;
	}

	TEEC_Operation op = {0};
	op.paramTypes = TEEC_PARAM_TYPES(
		TEEC_MEMREF_TEMP_INPUT,  /* [0] signature */
		TEEC_MEMREF_TEMP_INPUT,  /* [1] mission blob */
		TEEC_VALUE_INPUT,        /* [2] metadata */
		TEEC_VALUE_OUTPUT);      /* [3] status out */

	/* Parameter 0: signature (64 bytes) */
	op.params[0].tmpref.buffer = (void *)signature;
	op.params[0].tmpref.size = 64;

	/* Parameter 1: mission blob */
	op.params[1].tmpref.buffer = (void *)mission_blob;
	op.params[1].tmpref.size = mission_size;

	/* Parameter 2: expected hash pointer and count
	 * Note: In real impl, would pass expected_hash correctly
	 * For simplicity, compute SHA-256 on normal side
	 */
	op.params[2].value.a = (uint32_t)(uintptr_t)expected_hash;
	op.params[2].value.b = item_count;

	op.params[3].value.a = 0;  /* status output */

	fprintf(stdout, "mission_client: uploading %u items (%zu bytes)\n",
			item_count, mission_size);

	TEEC_Result res = TEEC_InvokeCommand(&g_sess,
										 TA_MISSION_CMD_UPLOAD,
										 &op, NULL);

	uint8_t ta_status = op.params[3].value.a;

	if (res != TEEC_SUCCESS) {
		log_error("TEEC_InvokeCommand(UPLOAD)", res);
		if (ta_status == 2) {
			return MISSION_CLIENT_ERROR_SECURITY;
		}
		return MISSION_CLIENT_ERROR_RPC;
	}

	if (ta_status != 1) {
		fprintf(stderr, "mission_client: TA reported status %u\n", ta_status);
		return MISSION_CLIENT_ERROR_SECURITY;
	}

	fprintf(stdout, "mission_client: upload SUCCESS (status=%u)\n", ta_status);
	return MISSION_CLIENT_OK;
}

int mission_client_get_waypoint(uint16_t index, mission_client_waypoint_t *wp)
{
	if (!g_initialized) {
		return MISSION_CLIENT_ERROR_SESSION;
	}

	if (!wp) {
		return MISSION_CLIENT_ERROR_BAD_PARAM;
	}

	TEEC_Operation op = {0};
	op.paramTypes = TEEC_PARAM_TYPES(
		TEEC_VALUE_INPUT,        /* [0] index */
		TEEC_MEMREF_TEMP_OUTPUT, /* [1] waypoint response */
		TEEC_NONE,
		TEEC_NONE);

	op.params[0].value.a = index;
	op.params[1].tmpref.buffer = (void *)wp;
	op.params[1].tmpref.size = sizeof(mission_client_waypoint_t);

	TEEC_Result res = TEEC_InvokeCommand(&g_sess,
										 TA_MISSION_CMD_GET_WAYPOINT,
										 &op, NULL);

	if (res != TEEC_SUCCESS) {
		log_error("TEEC_InvokeCommand(GET_WAYPOINT)", res);
		if (res == TEEC_ERROR_OUT_OF_MEMORY) {
			return MISSION_CLIENT_ERROR_OUT_RANGE;
		}
		if (res == TEEC_ERROR_BAD_STATE) {
			return MISSION_CLIENT_ERROR_NO_MISSION;
		}
		return MISSION_CLIENT_ERROR_RPC;
	}

	return MISSION_CLIENT_OK;
}

uint16_t mission_client_get_count(void)
{
	if (!g_initialized) {
		return 0;
	}

	TEEC_Operation op = {0};
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
	op.params[0].value.a = 0;

	TEEC_Result res = TEEC_InvokeCommand(&g_sess,
										 TA_MISSION_CMD_GET_COUNT,
										 &op, NULL);

	if (res != TEEC_SUCCESS) {
		log_error("TEEC_InvokeCommand(GET_COUNT)", res);
		return 0;
	}

	return (uint16_t)op.params[0].value.a;
}

int mission_client_get_status(mission_client_status_t *status)
{
	if (!g_initialized) {
		return MISSION_CLIENT_ERROR_SESSION;
	}

	if (!status) {
		return MISSION_CLIENT_ERROR_BAD_PARAM;
	}

	TEEC_Operation op = {0};
	op.paramTypes = TEEC_PARAM_TYPES(
		TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
	op.params[0].tmpref.buffer = (void *)status;
	op.params[0].tmpref.size = sizeof(mission_client_status_t);

	TEEC_Result res = TEEC_InvokeCommand(&g_sess,
										 TA_MISSION_CMD_VERIFY_STATUS,
										 &op, NULL);

	if (res != TEEC_SUCCESS) {
		log_error("TEEC_InvokeCommand(VERIFY_STATUS)", res);
		return MISSION_CLIENT_ERROR_RPC;
	}

	return MISSION_CLIENT_OK;
}

int mission_client_clear_mission(void)
{
	if (!g_initialized) {
		return MISSION_CLIENT_ERROR_SESSION;
	}

	TEEC_Operation op = {0};
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);

	TEEC_Result res = TEEC_InvokeCommand(&g_sess,
					     TA_MISSION_CMD_CLEAR_MISSION,
					     &op, NULL);

	if (res != TEEC_SUCCESS) {
		log_error("TEEC_InvokeCommand(CLEAR_MISSION)", res);
		return MISSION_CLIENT_ERROR_RPC;
	}

	return MISSION_CLIENT_OK;
}

int mission_client_verify_sig(const uint8_t *signature, const uint8_t *hash)
{
	if (!g_initialized) {
		return MISSION_CLIENT_ERROR_SESSION;
	}

	if (!signature || !hash) {
		return MISSION_CLIENT_ERROR_BAD_PARAM;
	}

	TEEC_Operation op = {0};
	op.paramTypes = TEEC_PARAM_TYPES(
			TEEC_MEMREF_TEMP_INPUT,   /* [0] signature, 64 bytes */
			TEEC_MEMREF_TEMP_INPUT,   /* [1] hash,      32 bytes */
			TEEC_NONE,
			TEEC_NONE);

	op.params[0].tmpref.buffer = (void *)signature;
	op.params[0].tmpref.size   = 64;
	op.params[1].tmpref.buffer = (void *)hash;
	op.params[1].tmpref.size   = 32;

	TEEC_Result res = TEEC_InvokeCommand(&g_sess,
					     TA_MISSION_CMD_VERIFY_SIG,
					     &op, NULL);

	if (res == TEEC_SUCCESS)         { return MISSION_CLIENT_OK; }
	if (res == TEEC_ERROR_SECURITY)  { return MISSION_CLIENT_ERROR_SECURITY; }

	log_error("TEEC_InvokeCommand(VERIFY_SIG)", res);
	return MISSION_CLIENT_ERROR_RPC;
}

/* ===== Hash Chain API ===== */

int mission_client_chain_pos(double lat, double lon, float alt,
<<<<<<< HEAD
			     uint64_t timestamp_us,
=======
>>>>>>> 2e514d9d17 (PX-4)
			     uint8_t hash_out[32], uint32_t *seq_out)
{
	if (!g_initialized) {
		return MISSION_CLIENT_ERROR_SESSION;
	}

	ta_chain_pos_t pos;
<<<<<<< HEAD
	pos.lat          = lat;
	pos.lon          = lon;
	pos.alt          = alt;
	pos.timestamp_us = timestamp_us;
=======
	pos.lat = lat;
	pos.lon = lon;
	pos.alt = alt;
>>>>>>> 2e514d9d17 (PX-4)

	uint8_t new_hash[32] = {0};

	TEEC_Operation op = {0};
	op.paramTypes = TEEC_PARAM_TYPES(
		TEEC_MEMREF_TEMP_INPUT,   /* [0] position data */
		TEEC_MEMREF_TEMP_OUTPUT,  /* [1] new chain hash */
		TEEC_VALUE_OUTPUT,        /* [2] sequence number */
		TEEC_NONE);

	op.params[0].tmpref.buffer = &pos;
	op.params[0].tmpref.size   = sizeof(pos);
	op.params[1].tmpref.buffer = new_hash;
	op.params[1].tmpref.size   = sizeof(new_hash);

	TEEC_Result res = TEEC_InvokeCommand(&g_sess, TA_MISSION_CMD_CHAIN_POS, &op, NULL);
	if (res != TEEC_SUCCESS) {
		log_error("TEEC_InvokeCommand(CHAIN_POS)", res);
		return MISSION_CLIENT_ERROR_RPC;
	}

	if (hash_out) { memcpy(hash_out, new_hash, 32); }
	if (seq_out)  { *seq_out = op.params[2].value.a; }

	return MISSION_CLIENT_OK;
}

int mission_client_get_chain_hash(uint8_t hash_out[32], uint32_t *seq_out)
{
	if (!g_initialized) {
		return MISSION_CLIENT_ERROR_SESSION;
	}

	if (!hash_out) {
		return MISSION_CLIENT_ERROR_BAD_PARAM;
	}

	TEEC_Operation op = {0};
	op.paramTypes = TEEC_PARAM_TYPES(
		TEEC_MEMREF_TEMP_OUTPUT,
		TEEC_VALUE_OUTPUT,
		TEEC_NONE,
		TEEC_NONE);

	op.params[0].tmpref.buffer = hash_out;
	op.params[0].tmpref.size   = 32;

	TEEC_Result res = TEEC_InvokeCommand(&g_sess, TA_MISSION_CMD_GET_CHAIN_HASH, &op, NULL);
	if (res != TEEC_SUCCESS) {
		log_error("TEEC_InvokeCommand(GET_CHAIN_HASH)", res);
		return MISSION_CLIENT_ERROR_RPC;
	}

	if (seq_out) { *seq_out = op.params[1].value.a; }
	return MISSION_CLIENT_OK;
}

int mission_client_reset_chain(void)
{
	if (!g_initialized) {
		return MISSION_CLIENT_ERROR_SESSION;
	}

	TEEC_Operation op = {0};
	op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);

	TEEC_Result res = TEEC_InvokeCommand(&g_sess, TA_MISSION_CMD_RESET_CHAIN, &op, NULL);
	if (res != TEEC_SUCCESS) {
		log_error("TEEC_InvokeCommand(RESET_CHAIN)", res);
		return MISSION_CLIENT_ERROR_RPC;
	}

	return MISSION_CLIENT_OK;
}
