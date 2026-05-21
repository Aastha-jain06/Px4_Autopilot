#include "WpHashChain.hpp"

#include <px4_platform_common/log.h>
#include <px4_platform_common/defines.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)
#  include <mission_client.h>
#endif

ModuleBase::Descriptor WpHashChain::desc{task_spawn, custom_command, print_usage};

WpHashChain::WpHashChain() :
	WorkItem(MODULE_NAME, px4::wq_configurations::lp_default),
	_optee_call_perf(perf_alloc(PC_ELAPSED, MODULE_NAME": optee_call")),
	_loop_perf(perf_alloc(PC_ELAPSED, MODULE_NAME": loop"))
{
}

WpHashChain::~WpHashChain()
{
	_nav_item_sub.unregisterCallback();
	_land_sub.unregisterCallback();

#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)
	if (_optee_ok) {
		mission_client_cleanup();
	}
#endif

	perf_free(_optee_call_perf);
	perf_free(_loop_perf);
}

bool WpHashChain::start()
{
#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)
	if (mission_client_init() == MISSION_CLIENT_OK) {
		mission_client_reset_chain();
		_optee_ok = true;
		PX4_INFO("OP-TEE session open — wp_hash_chain ready");
	} else {
		PX4_WARN("OP-TEE init failed — wp_hash_chain disabled");
		return false;
	}
#else
	PX4_WARN("OP-TEE not available — wp_hash_chain disabled");
	return false;
#endif

	if (!_nav_item_sub.registerCallback() || !_land_sub.registerCallback()) {
		PX4_ERR("callback registration failed");
		return false;
	}

	PX4_INFO("Listening for waypoint events...");
	return true;
}

void WpHashChain::Run()
{
	if (should_exit()) {
		_nav_item_sub.unregisterCallback();
		exit_and_cleanup(desc);
		return;
	}

	perf_begin(_loop_perf);

	/* ── 1. Track flight state (callback fires on every land-state change) ── */
	vehicle_land_detected_s land{};

	if (_land_sub.update(&land)) {
		bool now_flying = !land.landed;

		if (now_flying && !_in_flight) {
			/* Takeoff: reset chain and coord cache */
#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)
			mission_client_reset_chain();
#endif
			_chain_seq        = 0;
			_last_seq_reached = -1;
			memset(_chain_hash, 0, sizeof(_chain_hash));
			for (auto &c : _cache) { c = WpCoordCache{}; }
			for (auto &f : _seq_frozen) { f = false; }
			PX4_INFO("Takeoff — waypoint hash chain started");
		}

		if (!now_flying && _in_flight) {
			on_landing();
		}

		_in_flight = now_flying;
	}

	if (!_in_flight) {
		perf_end(_loop_perf);
		return;
	}

	/* ── 2. Freeze + hash when mission_result reports a waypoint reached ── */
	/* Poll mission_result BEFORE nav_item: navigator publishes seq_reached
	 * first, then the loiter nav_item.  By freezing the slot here we ensure
	 * the loiter altitude update below cannot overwrite the planned value. */
	mission_result_s result{};

	if (_mission_result_sub.update(&result)) {
		int32_t reached = result.seq_reached;

		if (reached >= 0 && reached != _last_seq_reached) {
			_last_seq_reached = reached;

			if (reached < CACHE_SIZE) {
				_seq_frozen[reached] = true;
			}

			hash_waypoint((uint16_t)reached);
		}
	}

	/* ── 3. Cache planned coords from navigator_mission_item ─────────── */
	navigator_mission_item_s nav_item{};

	if (_nav_item_sub.update(&nav_item)) {
		uint16_t seq = nav_item.sequence_current;

		/* Slot frozen once seq_reached fired — loiter altitude adjustments
		 * arriving in this same Run() or later are ignored. */
		if (seq < CACHE_SIZE && !_seq_frozen[seq]) {
			_cache[seq].lat     = nav_item.latitude;
			_cache[seq].lon     = nav_item.longitude;
			_cache[seq].alt     = nav_item.altitude;
			_cache[seq].nav_cmd = nav_item.nav_cmd;
			_cache[seq].valid   = true;
			PX4_DEBUG("WP%u cached: (%.6f, %.6f, %.1f)",
				  seq, (double)nav_item.latitude,
				  (double)nav_item.longitude, (double)nav_item.altitude);
		}
	}

	perf_end(_loop_perf);
}

void WpHashChain::hash_waypoint(uint16_t seq)
{
	if (seq >= CACHE_SIZE || !_cache[seq].valid) {
		PX4_WARN("WP%u reached but coords not cached yet", seq);
		return;
	}

	/* LAND altitude is adjusted at runtime by PX4 — exclude to keep chain
	 * deterministic and matchable against the ground-station planned chain. */
	if (_cache[seq].nav_cmd == 21) {
		PX4_DEBUG("WP%u is LAND — skipped in chain", seq);
		return;
	}

	const WpCoordCache &wp = _cache[seq];

	PX4_INFO("WP%u REACHED — hashing planned (%.6f, %.6f, %.1f) in TrustZone",
		 seq, (double)wp.lat, (double)wp.lon, (double)wp.alt);

	wp_hash_chain_s record{};
	record.timestamp   = hrt_absolute_time();
	record.wp_seq      = seq;
	record.nav_cmd     = wp.nav_cmd;
	record.planned_lat = wp.lat;
	record.planned_lon = wp.lon;
	record.planned_alt = wp.alt;
	record.optee_used  = _optee_ok;

#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)
	hrt_abstime t0 = hrt_absolute_time();
	perf_begin(_optee_call_perf);

	/*
	 * Pass planned lat/lon/alt (float32 widened to double) into OP-TEE.
	 * Timestamp = 0: ground chain also omits timestamps, so both sides
	 * hash the same 52-byte input and produce identical hashes.
	 */
	int rc = mission_client_chain_pos(
			(double)wp.lat, (double)wp.lon, wp.alt,
			record.chain_hash, &record.chain_seq);

	perf_end(_optee_call_perf);
	record.compute_time_us = (uint32_t)(hrt_absolute_time() - t0);

	if (rc == MISSION_CLIENT_OK) {
		memcpy(_chain_hash, record.chain_hash, 32);
		_chain_seq = record.chain_seq;
		PX4_INFO("WP%u hash: %02x%02x%02x%02x.. (%" PRIu32 " us)",
			 seq,
			 record.chain_hash[0], record.chain_hash[1],
			 record.chain_hash[2], record.chain_hash[3],
			 record.compute_time_us);
	} else {
		PX4_ERR("WP%u: OP-TEE chain_pos failed (%d)", seq, rc);
		return;
	}
#endif

	_chain_pub.publish(record);
}

void WpHashChain::on_landing()
{
	PX4_INFO("Landing — chain sealed: %u waypoints, hash=%02x%02x%02x%02x..",
		 _chain_seq,
		 _chain_hash[0], _chain_hash[1],
		 _chain_hash[2], _chain_hash[3]);

	FILE *f = fopen(PX4_STORAGEDIR "/wp_flight_chain.sha256", "w");

	if (f) {
		for (int i = 0; i < 32; i++) {
			fprintf(f, "%02x", _chain_hash[i]);
		}
		fprintf(f, "\n");
		fclose(f);
		PX4_INFO("Flight chain saved: %s/wp_flight_chain.sha256", PX4_STORAGEDIR);
	} else {
		PX4_ERR("Failed to write wp_flight_chain.sha256");
	}
}

int WpHashChain::print_status()
{
	PX4_INFO("OP-TEE active  : %s", _optee_ok ? "YES (TrustZone)" : "NO");
	PX4_INFO("flight state   : %s", _in_flight ? "AIRBORNE" : "ON GROUND");
	PX4_INFO("chain seq      : %" PRIu32 " waypoints hashed", _chain_seq);
	PX4_INFO("last wp reached: %" PRId32, _last_seq_reached);

	if (_chain_seq > 0) {
		PX4_INFO("chain hash     : %02x%02x%02x%02x%02x%02x%02x%02x..",
			 _chain_hash[0], _chain_hash[1], _chain_hash[2], _chain_hash[3],
			 _chain_hash[4], _chain_hash[5], _chain_hash[6], _chain_hash[7]);
	}

	perf_print_counter(_optee_call_perf);
	perf_print_counter(_loop_perf);
	return 0;
}

int WpHashChain::task_spawn(int argc, char *argv[])
{
	WpHashChain *obj = new WpHashChain();

	if (!obj) {
		PX4_ERR("alloc failed");
		return -1;
	}

	if (!obj->start()) {
		delete obj;
		return -1;
	}

	desc.object.store(obj);
	desc.task_id = task_id_is_work_queue;
	return 0;
}

int WpHashChain::custom_command(int argc, char *argv[])
{
	if (argc >= 1 && strcmp(argv[0], "hash") == 0) {
#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)
		uint8_t h[32]; uint32_t seq;

		if (mission_client_get_chain_hash(h, &seq) == MISSION_CLIENT_OK) {
			PX4_INFO("seq=%u hash=%02x%02x%02x%02x%02x%02x%02x%02x"
				 "%02x%02x%02x%02x%02x%02x%02x%02x"
				 "%02x%02x%02x%02x%02x%02x%02x%02x"
				 "%02x%02x%02x%02x%02x%02x%02x%02x",
				 seq,
				 h[0],h[1],h[2],h[3],h[4],h[5],h[6],h[7],
				 h[8],h[9],h[10],h[11],h[12],h[13],h[14],h[15],
				 h[16],h[17],h[18],h[19],h[20],h[21],h[22],h[23],
				 h[24],h[25],h[26],h[27],h[28],h[29],h[30],h[31]);
		}

#else
		PX4_ERR("OP-TEE not available");
#endif
		return 0;
	}

	return print_usage("unknown command");
}

int WpHashChain::print_usage(const char *reason)
{
	if (reason) { PX4_WARN("%s\n", reason); }

	PRINT_MODULE_DESCRIPTION(
		R"DESCR_STR(
### Description
Hash chain of planned waypoint coordinates using OP-TEE TrustZone.

Triggers on mission_result.seq_reached — i.e. exactly when the drone
marks a waypoint as reached and begins moving to the next one.

Uses planned coordinates (from navigator_mission_item) so the chain
matches the ground-station chain computed by Tools/waypoint_chain.py.

  chain_hash[N] = SHA256( chain_hash[N-1] | lat<f64> | lon<f64> | alt<f32> )

timestamp_us = 0 so both sides produce identical hashes.
Final hash written to PX4_STORAGEDIR/wp_flight_chain.sha256 on landing.

### Examples
  wp_hash_chain start
  wp_hash_chain hash     # print current chain hash from TA
)DESCR_STR");

	PRINT_MODULE_USAGE_NAME("wp_hash_chain", "system");
	PRINT_MODULE_USAGE_COMMAND("start");
	PRINT_MODULE_USAGE_COMMAND_DESCR("hash", "  Print current chain hash from TA");
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();
	return 0;
}
