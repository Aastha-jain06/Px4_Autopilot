/****************************************************************************
 *
 *   Copyright (c) 2024 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#include "missionHashCheck.hpp"

#include <navigator/navigation.h>
#include <px4_platform_common/log.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <cmath>

#ifdef __PX4_POSIX
#if defined(OPTEE_AVAILABLE)
#include <mission_client.h>          // OP-TEE client (primary path)
#endif
#include <openssl/evp.h>             // OpenSSL (fallback when OPTEE_AVAILABLE not set)
#elif defined(PX4_CRYPTO)
#include <optional/monocypher-ed25519.h>
#endif

// Ed25519 public key for mission signature verification.
// Regenerate with:  python3 Tools/generate_keypair.py
// Then replace this array and rebuild.
static const uint8_t MISSION_SIGNING_PUBLIC_KEY[32] = {
	0x00, 0xad, 0xb8, 0x61, 0xe0, 0x08, 0x8e, 0x34, 0xe4, 0x48, 0x59, 0x3a, 0x9b, 0x55, 0x90, 0xbd,
	0x7d, 0x8c, 0xca, 0x67, 0x09, 0x27, 0x41, 0x7c, 0x67, 0x87, 0xf2, 0x78, 0xdf, 0xad, 0x9b, 0xf7
};

// ---------------------------------------------------------------------------
// Minimal self-contained SHA-256 (public domain)
// ---------------------------------------------------------------------------
namespace {

static const uint32_t SHA256_K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

struct Sha256Ctx {
	uint32_t h[8];
	uint8_t  block[64];
	uint32_t block_len;
	uint64_t total_len;
};

#define SHA256_ROTR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define SHA256_CH(x,y,z)  (((x)&(y))^(~(x)&(z)))
#define SHA256_MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define SHA256_EP0(x) (SHA256_ROTR(x,2)^SHA256_ROTR(x,13)^SHA256_ROTR(x,22))
#define SHA256_EP1(x) (SHA256_ROTR(x,6)^SHA256_ROTR(x,11)^SHA256_ROTR(x,25))
#define SHA256_SIG0(x)(SHA256_ROTR(x,7)^SHA256_ROTR(x,18)^((x)>>3))
#define SHA256_SIG1(x)(SHA256_ROTR(x,17)^SHA256_ROTR(x,19)^((x)>>10))

static void sha256_transform(Sha256Ctx *ctx, const uint8_t *data)
{
	uint32_t a, b, c, d, e, f, g, h, t1, t2, w[64];

	for (int i = 0; i < 16; i++) {
		w[i] = ((uint32_t)data[i * 4] << 24) | ((uint32_t)data[i * 4 + 1] << 16) |
		       ((uint32_t)data[i * 4 + 2] << 8) | (uint32_t)data[i * 4 + 3];
	}

	for (int i = 16; i < 64; i++) {
		w[i] = SHA256_SIG1(w[i - 2]) + w[i - 7] + SHA256_SIG0(w[i - 15]) + w[i - 16];
	}

	a = ctx->h[0]; b = ctx->h[1]; c = ctx->h[2]; d = ctx->h[3];
	e = ctx->h[4]; f = ctx->h[5]; g = ctx->h[6]; h = ctx->h[7];

	for (int i = 0; i < 64; i++) {
		t1 = h + SHA256_EP1(e) + SHA256_CH(e, f, g) + SHA256_K[i] + w[i];
		t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}

	ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
	ctx->h[4] += e; ctx->h[5] += f; ctx->h[6] += g; ctx->h[7] += h;
}

static void sha256_init(Sha256Ctx *ctx)
{
	ctx->h[0] = 0x6a09e667; ctx->h[1] = 0xbb67ae85;
	ctx->h[2] = 0x3c6ef372; ctx->h[3] = 0xa54ff53a;
	ctx->h[4] = 0x510e527f; ctx->h[5] = 0x9b05688c;
	ctx->h[6] = 0x1f83d9ab; ctx->h[7] = 0x5be0cd19;
	ctx->block_len = 0;
	ctx->total_len = 0;
}

static void sha256_update(Sha256Ctx *ctx, const void *data, size_t len)
{
	const uint8_t *p = static_cast<const uint8_t *>(data);
	ctx->total_len += len;

	while (len > 0) {
		uint32_t space = 64 - ctx->block_len;
		uint32_t n = (len < space) ? (uint32_t)len : space;
		memcpy(ctx->block + ctx->block_len, p, n);
		ctx->block_len += n;
		p += n;
		len -= n;

		if (ctx->block_len == 64) {
			sha256_transform(ctx, ctx->block);
			ctx->block_len = 0;
		}
	}
}

static void sha256_final(Sha256Ctx *ctx, uint8_t out[32])
{
	ctx->block[ctx->block_len++] = 0x80;

	if (ctx->block_len > 56) {
		memset(ctx->block + ctx->block_len, 0, 64 - ctx->block_len);
		sha256_transform(ctx, ctx->block);
		ctx->block_len = 0;
	}

	memset(ctx->block + ctx->block_len, 0, 56 - ctx->block_len);

	uint64_t bit_len = ctx->total_len * 8;
	ctx->block[56] = (uint8_t)(bit_len >> 56);
	ctx->block[57] = (uint8_t)(bit_len >> 48);
	ctx->block[58] = (uint8_t)(bit_len >> 40);
	ctx->block[59] = (uint8_t)(bit_len >> 32);
	ctx->block[60] = (uint8_t)(bit_len >> 24);
	ctx->block[61] = (uint8_t)(bit_len >> 16);
	ctx->block[62] = (uint8_t)(bit_len >>  8);
	ctx->block[63] = (uint8_t)(bit_len >>  0);
	sha256_transform(ctx, ctx->block);

	for (int i = 0; i < 8; i++) {
		out[i * 4 + 0] = (uint8_t)(ctx->h[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(ctx->h[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(ctx->h[i] >>  8);
		out[i * 4 + 3] = (uint8_t)(ctx->h[i] >>  0);
	}
}

#undef SHA256_ROTR
#undef SHA256_CH
#undef SHA256_MAJ
#undef SHA256_EP0
#undef SHA256_EP1
#undef SHA256_SIG0
#undef SHA256_SIG1

} // namespace

// ---------------------------------------------------------------------------

bool MissionHashCheck::loadExpectedHash()
{
#ifdef PX4_STORAGEDIR
	FILE *f = fopen(PX4_STORAGEDIR "/mission.sha256", "r");

	if (!f) {
		return false;
	}

	char hex[65] = {};
	size_t n = fread(hex, 1, 64, f);
	fclose(f);

	if (n != 64) {
		return false;
	}

	for (int i = 0; i < 32; i++) {
		unsigned int val = 0;

		if (sscanf(&hex[i * 2], "%2x", &val) != 1) {
			return false;
		}

		_expected_hash[i] = (uint8_t)val;
	}

	return true;
#else
	return false;
#endif
}

bool MissionHashCheck::loadExpectedSig()
{
#ifdef PX4_STORAGEDIR
	FILE *f = fopen(PX4_STORAGEDIR "/mission.sig", "r");

	if (!f) {
		return false;
	}

	char hex[129] = {};
	size_t n = fread(hex, 1, 128, f);
	fclose(f);

	if (n != 128) {
		return false;
	}

	for (int i = 0; i < 64; i++) {
		unsigned int val = 0;

		if (sscanf(&hex[i * 2], "%2x", &val) != 1) {
			return false;
		}

		_expected_sig[i] = (uint8_t)val;
	}

	return true;
#else
	return false;
#endif
}

MissionHashCheck::~MissionHashCheck()
{
#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)
	if (_optee_initialized) {
		mission_client_cleanup();
	}
#endif
}

bool MissionHashCheck::verifySignature(const uint8_t hash[32])
{
#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)
	// --- Primary path: Ed25519 verification inside TrustZone secure world ---
	if (!_optee_initialized) {
		int rc = mission_client_init();

		if (rc != MISSION_CLIENT_OK) {
			PX4_ERR("Mission OP-TEE init failed (%d) — falling back to OpenSSL", rc);
			goto openssl_fallback;
		}

		_optee_initialized = true;
		PX4_INFO("Mission OP-TEE session opened (TrustZone active)");
	}

	{
		int rc = mission_client_verify_sig(_expected_sig, hash);

		if (rc == MISSION_CLIENT_OK)            { return true; }

		if (rc == MISSION_CLIENT_ERROR_SECURITY) { return false; }

		// TA communication error — fall back to OpenSSL
		PX4_WARN("Mission OP-TEE call failed (%d) — falling back to OpenSSL", rc);
	}

openssl_fallback:
#endif

#ifdef __PX4_POSIX
	// --- Fallback path: OpenSSL in normal world ---
	{
		EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
				 MISSION_SIGNING_PUBLIC_KEY, 32);

		if (!pkey) { return false; }

		EVP_MD_CTX *ctx = EVP_MD_CTX_new();

		if (!ctx) { EVP_PKEY_free(pkey); return false; }

		bool ok = (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) == 1) &&
			  (EVP_DigestVerify(ctx, _expected_sig, 64, hash, 32) == 1);

		EVP_MD_CTX_free(ctx);
		EVP_PKEY_free(pkey);
		return ok;
	}

#elif defined(PX4_CRYPTO)
	return crypto_ed25519_check(_expected_sig, MISSION_SIGNING_PUBLIC_KEY, hash, 32) == 0;

#else
	(void)hash;
	PX4_WARN("Mission signature check not available on this platform");
	return false;
#endif
}

bool MissionHashCheck::computeHash(dm_item_t dataman_id, uint16_t count, uint8_t out[32])
{
	Sha256Ctx ctx;
	sha256_init(&ctx);

	char buf[128];
	int len = snprintf(buf, sizeof(buf), "%u\n", (unsigned)count);

	if (len > 0) {
		sha256_update(&ctx, buf, (size_t)len);
	}

	for (uint16_t i = 0; i < count; i++) {
		mission_item_s item{};

		if (!_dataman_client.readSync(dataman_id, i,
					      reinterpret_cast<uint8_t *>(&item), sizeof(item), 2000_ms)) {
			PX4_ERR("mission hash: dataman read failed at index %u", (unsigned)i);
			return false;
		}

		auto clean = [](float v) -> double { return std::isnan(v) ? 0.0 : (double)v; };

		// Normalize INT frame variants to their non-INT equivalents so the
		// canonical text matches .plan files that use the base frame values.
		static const uint8_t FRAME_INT_MAP[][2] = {{5, 0}, {6, 3}, {7, 1}, {8, 4}};
		uint8_t frame = item.frame;
		for (auto &m : FRAME_INT_MAP) { if (frame == m[0]) { frame = m[1]; break; } }

		len = snprintf(buf, sizeof(buf),
			       "%u,%u,%u,%.6f,%.6f,%.6f,%.6f,%.8f,%.8f,%.3f\n",
			       (unsigned)frame,
			       (unsigned)item.nav_cmd,
			       item.autocontinue ? 1u : 0u,
			       clean(item.params[0]),
			       clean(item.params[1]),
			       clean(item.params[2]),
			       clean(item.params[3]),
			       item.lat,
			       item.lon,
			       clean(item.altitude));

		if (len > 0) {
			sha256_update(&ctx, buf, (size_t)len);
		}
	}

	sha256_final(&ctx, out);
	return true;
}

void MissionHashCheck::checkAndReport(const Context &context, Report &reporter)
{
	mission_s mission{};

	if (!_mission_sub.copy(&mission) || mission.count == 0) {
		return;
	}

	// Recompute only when the mission changes.
	if (!_evaluated || mission.mission_id != _cached_mission_id) {
		_cached_mission_id  = mission.mission_id;
		_evaluated          = true;
		_hash_file_present  = loadExpectedHash();
		_sig_file_present   = loadExpectedSig();
		_hash_matches       = false;
		_sig_verified       = false;

		if (_hash_file_present || _sig_file_present) {
			uint8_t actual[32]{};
			const dm_item_t dm_id = (mission.mission_dataman_id == 0)
						? DM_KEY_WAYPOINTS_OFFBOARD_0
						: DM_KEY_WAYPOINTS_OFFBOARD_1;

			const hrt_abstime t0 = hrt_absolute_time();

			if (computeHash(dm_id, mission.count, actual)) {
				const uint64_t elapsed_us = hrt_absolute_time() - t0;

				if (_hash_file_present) {
					_hash_matches = (memcmp(_expected_hash, actual, 32) == 0);

					if (_hash_matches) {
						PX4_INFO("Mission hash OK (items=%u, took=%" PRIu64 " us)",
							 (unsigned)mission.count, elapsed_us);
					} else {
						PX4_ERR("Mission hash MISMATCH (items=%u)", (unsigned)mission.count);
					}
				}

				if (_sig_file_present) {
					_sig_verified = verifySignature(actual);

					if (_sig_verified) {
						PX4_INFO("Mission signature verified OK");
					} else {
						PX4_ERR("Mission signature INVALID");
					}
				}

			} else {
				PX4_ERR("Mission hash: failed to read from dataman");
			}
		}
	}

	if (_hash_file_present && !_hash_matches) {
		/* EVENT
		 * @description
		 * The mission stored on the vehicle does not match the expected hash.
		 * Re-upload the correct mission or remove /fs/microsd/mission.sha256.
		 */
		reporter.armingCheckFailure(NavModes::All, health_component_t::system,
					    events::ID("check_mission_hash_mismatch"),
					    events::Log::Error, "Mission hash mismatch");

		if (reporter.mavlink_log_pub()) {
			mavlink_log_critical(reporter.mavlink_log_pub(), "Preflight Fail: Mission hash mismatch");
		}
	}

	if (_sig_file_present && !_sig_verified) {
		/* EVENT
		 * @description
		 * The Ed25519 signature for the uploaded mission is invalid or does not
		 * match the embedded public key.  Re-sign with the correct private key
		 * or remove /fs/microsd/mission.sig.
		 */
		reporter.armingCheckFailure(NavModes::All, health_component_t::system,
					    events::ID("check_mission_sig_invalid"),
					    events::Log::Error, "Mission signature invalid");

		if (reporter.mavlink_log_pub()) {
			mavlink_log_critical(reporter.mavlink_log_pub(), "Preflight Fail: Mission signature invalid");
		}
	}
}
