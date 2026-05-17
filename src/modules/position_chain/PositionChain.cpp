#include "PositionChain.hpp"

#include <px4_platform_common/log.h>
#include <px4_platform_common/defines.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>

#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)
#  include <mission_client.h>
#endif

ModuleBase::Descriptor PositionChain::desc{task_spawn, custom_command, print_usage};

PositionChain::PositionChain() :
	WorkItem(MODULE_NAME, px4::wq_configurations::lp_default),
	_optee_call_perf(perf_alloc(PC_ELAPSED, MODULE_NAME": optee_call")),
	_loop_perf(perf_alloc(PC_ELAPSED, MODULE_NAME": loop"))
{
}

PositionChain::~PositionChain()
{
	_nav_item_sub.unregisterCallback();

#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)
	if (_optee_ok) {
		mission_client_cleanup();
	}
#endif

	perf_free(_optee_call_perf);
	perf_free(_loop_perf);
}

bool PositionChain::start()
{
#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)
	int rc = mission_client_init();

	if (rc == MISSION_CLIENT_OK) {
		mission_client_reset_chain();
		_optee_ok = true;
		PX4_INFO("OP-TEE session open — hash chain in TrustZone");

	} else {
		PX4_WARN("OP-TEE init failed (%d) — position_chain disabled", rc);
		return false;
	}

#else
	PX4_WARN("OP-TEE not available on this platform — position_chain disabled");
	return false;
#endif

	/* Register callback on navigator_mission_item:
	 * Run() fires once per waypoint transition, not on every EKF2 tick */
	if (!_nav_item_sub.registerCallback()) {
		PX4_ERR("navigator_mission_item callback registration failed");
		return false;
	}

	PX4_INFO("Waiting for mission waypoint events...");
	return true;
}

void PositionChain::Run()
{
	if (should_exit()) {
		_nav_item_sub.unregisterCallback();
		exit_and_cleanup(desc);
		return;
	}

	perf_begin(_loop_perf);

	/* Check flight state */
	vehicle_land_detected_s land{};

	if (_land_sub.update(&land)) {
		bool now_flying = !land.landed;

		if (now_flying && !_in_flight) {
#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)
			mission_client_reset_chain();
#endif
			_chain_seq   = 0;
			_last_wp_seq = UINT16_MAX;
			memset(_chain_hash, 0, sizeof(_chain_hash));
			PX4_INFO("Takeoff detected — waypoint hash chain started");
		}

		if (!now_flying && _in_flight) {
			PX4_INFO("Landing detected — chain sealed: seq=%" PRIu32
				 " hash=%02x%02x%02x%02x..",
				 _chain_seq,
				 _chain_hash[0], _chain_hash[1],
				 _chain_hash[2], _chain_hash[3]);

			/* Write full 64-char hex hash to flight_chain.sha256 */
			FILE *f = fopen(PX4_STORAGEDIR "/flight_chain.sha256", "w");

			if (f) {
				for (int i = 0; i < 32; i++) {
					fprintf(f, "%02x", _chain_hash[i]);
				}
				fprintf(f, "\n");
				fclose(f);
				PX4_INFO("Final hash saved: %s/flight_chain.sha256", PX4_STORAGEDIR);

			} else {
				PX4_ERR("Failed to write flight_chain.sha256");
			}
		}

		_in_flight = now_flying;
	}

	if (!_in_flight) {
		perf_end(_loop_perf);
		return;
	}

	/*
	 * Run() fires when the navigator publishes a new mission item —
	 * i.e. each time it transitions to the next waypoint.
	 * Read it to get the waypoint sequence number.
	 */
	navigator_mission_item_s nav_item{};

	if (!_nav_item_sub.update(&nav_item)) {
		perf_end(_loop_perf);
		return;
	}

	/* Skip if same waypoint fired again (navigator can re-publish same item) */
	if (nav_item.sequence_current == _last_wp_seq) {
		perf_end(_loop_perf);
		return;
	}

	_last_wp_seq = nav_item.sequence_current;

	/* Snapshot current EKF2 position at this exact waypoint transition moment */
	vehicle_global_position_s pos{};

	if (!_pos_sub.copy(&pos) || !pos.lat_lon_valid) {
		PX4_WARN("WP%u: no valid EKF2 position yet", nav_item.sequence_current);
		perf_end(_loop_perf);
		return;
	}

	PX4_INFO("WP%u reached — hashing EKF2 position (%.6f, %.6f, %.1fm) in TrustZone",
		 nav_item.sequence_current,
		 (double)pos.lat, (double)pos.lon, (double)pos.alt);

	position_hash_chain_s record{};
	record.timestamp  = hrt_absolute_time();
	record.lat        = pos.lat;
	record.lon        = pos.lon;
	record.alt        = pos.alt;
	record.optee_used = _optee_ok;

#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)
	hrt_abstime t0 = hrt_absolute_time();
	perf_begin(_optee_call_perf);

	int rc = mission_client_chain_pos(
			pos.lat, pos.lon, pos.alt, pos.timestamp,
			record.chain_hash, &record.chain_seq);

	perf_end(_optee_call_perf);
	record.compute_time_us = (uint32_t)(hrt_absolute_time() - t0);

	if (rc == MISSION_CLIENT_OK) {
		memcpy(_chain_hash, record.chain_hash, 32);
		_chain_seq = record.chain_seq;
		PX4_INFO("WP%u hash: %02x%02x%02x%02x.. (took %" PRIu32 " us)",
			 nav_item.sequence_current,
			 record.chain_hash[0], record.chain_hash[1],
			 record.chain_hash[2], record.chain_hash[3],
			 record.compute_time_us);

	} else {
		PX4_ERR("WP%u: chain_pos failed (%d)", nav_item.sequence_current, rc);
		perf_end(_loop_perf);
		return;
	}
#endif

	_chain_pub.publish(record);
	perf_end(_loop_perf);
}

/* -------------------------------------------------------------------------
 * Performance benchmark: run N synthetic OP-TEE hash-chain calls,
 * measure per-call wall-clock time and process CPU usage.
 * ------------------------------------------------------------------------- */
void PositionChain::run_perf_test(int iterations)
{
#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)

	if (iterations <= 0 || iterations > 100000) {
		PX4_ERR("iterations must be 1..100000");
		return;
	}

	PX4_INFO("Hash chain perf test: %d iterations via OP-TEE...", iterations);

	/* Init session if not already open (e.g. called before module start).
	 * Never cleanup afterward — if the module is running it owns the session. */
	mission_client_init();
	mission_client_reset_chain();

	/* CPU ticks before */
	long utime_before = 0, stime_before = 0;
	{
		FILE *f = fopen("/proc/self/stat", "r");
		if (f) {
			/* skip 13 fields, then read utime(14) stime(15) */
			long d; int di; char ds[256]; char dc;
			int r = fscanf(f, "%d %s %c %d %d %d %d %d"
				       " %lu %lu %lu %lu %lu %ld %ld",
				       &di, ds, &dc, &di, &di, &di, &di, &di,
				       (unsigned long *)&d, (unsigned long *)&d,
				       (unsigned long *)&d, (unsigned long *)&d,
				       (unsigned long *)&d,
				       &utime_before, &stime_before);
			(void)r;
			fclose(f);
		}
	}

	uint64_t *times = (uint64_t *)malloc(sizeof(uint64_t) * iterations);
	if (!times) { PX4_ERR("malloc failed"); return; }

	hrt_abstime wall_start = hrt_absolute_time();

	for (int i = 0; i < iterations; i++) {
		/* Vary lat each iteration so every hash differs */
		double lat = 47.398 + i * 0.0001;
		hrt_abstime t0 = hrt_absolute_time();
		mission_client_chain_pos(lat, 8.5455f, 20.0f, (uint64_t)t0, NULL, NULL);
		times[i] = hrt_absolute_time() - t0;
	}

	hrt_abstime wall_us = hrt_absolute_time() - wall_start;

	/* CPU ticks after */
	long utime_after = 0, stime_after = 0;
	{
		FILE *f = fopen("/proc/self/stat", "r");
		if (f) {
			long d; int di; char ds[256]; char dc;
			int r = fscanf(f, "%d %s %c %d %d %d %d %d"
				       " %lu %lu %lu %lu %lu %ld %ld",
				       &di, ds, &dc, &di, &di, &di, &di, &di,
				       (unsigned long *)&d, (unsigned long *)&d,
				       (unsigned long *)&d, (unsigned long *)&d,
				       (unsigned long *)&d,
				       &utime_after, &stime_after);
			(void)r;
			fclose(f);
		}
	}

	/* Compute statistics */
	uint64_t sum = 0, mn = UINT64_MAX, mx = 0;
	for (int i = 0; i < iterations; i++) {
		sum += times[i];
		if (times[i] < mn) mn = times[i];
		if (times[i] > mx) mx = times[i];
	}

	/* Sort for percentiles (insertion sort — fast enough up to 10k) */
	uint64_t *sorted = (uint64_t *)malloc(sizeof(uint64_t) * iterations);
	if (sorted) {
		memcpy(sorted, times, sizeof(uint64_t) * iterations);
		for (int i = 1; i < iterations; i++) {
			uint64_t key = sorted[i];
			int j = i - 1;
			while (j >= 0 && sorted[j] > key) { sorted[j + 1] = sorted[j]; j--; }
			sorted[j + 1] = key;
		}
		uint64_t p95 = sorted[(int)(iterations * 0.95)];
		uint64_t p99 = sorted[(int)(iterations * 0.99)];
		free(sorted);

		long cpu_ticks = (utime_after + stime_after) - (utime_before + stime_before);
		long clk_tck   = sysconf(_SC_CLK_TCK);
		double cpu_ms  = clk_tck > 0 ? cpu_ticks * 1000.0 / clk_tck : 0.0;
		double wall_ms = wall_us / 1000.0;

		PX4_INFO("--- OP-TEE Hash Chain Performance ---");
		PX4_INFO("  iterations  : %d", iterations);
		PX4_INFO("  min         : %" PRIu64 " us", mn);
		PX4_INFO("  avg         : %" PRIu64 " us", sum / (uint64_t)iterations);
		PX4_INFO("  p95         : %" PRIu64 " us", p95);
		PX4_INFO("  p99         : %" PRIu64 " us", p99);
		PX4_INFO("  max         : %" PRIu64 " us", mx);
		PX4_INFO("  wall time   : %.1f ms  (%.0f calls/s)",
			 wall_ms, iterations * 1000.0 / wall_ms);
		PX4_INFO("  process cpu : %.1f ms  (%.1f%% of wall)",
			 cpu_ms, cpu_ms * 100.0 / wall_ms);
	}

	free(times);

#else
	(void)iterations;
	PX4_ERR("OP-TEE not available — cannot run perf test");
#endif
}

int PositionChain::print_status()
{
	PX4_INFO("OP-TEE active : %s", _optee_ok ? "YES (TrustZone)" : "NO");
	PX4_INFO("flight state  : %s", _in_flight ? "AIRBORNE — hashing EKF2 position" : "ON GROUND — idle");
	PX4_INFO("chain seq     : %" PRIu32, _chain_seq);

	if (_chain_seq > 0) {
		PX4_INFO("chain hash    : %02x%02x%02x%02x%02x%02x%02x%02x..",
			 _chain_hash[0], _chain_hash[1], _chain_hash[2], _chain_hash[3],
			 _chain_hash[4], _chain_hash[5], _chain_hash[6], _chain_hash[7]);
	}

	perf_print_counter(_optee_call_perf);
	perf_print_counter(_loop_perf);
	return 0;
}

int PositionChain::task_spawn(int argc, char *argv[])
{
	PositionChain *obj = new PositionChain();

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

int PositionChain::custom_command(int argc, char *argv[])
{
	if (argc >= 1 && strcmp(argv[0], "perf") == 0) {
		int n = (argc >= 2) ? atoi(argv[1]) : 1000;
		run_perf_test(n);
		return 0;
	}

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

	if (argc >= 1 && strcmp(argv[0], "reset") == 0) {
#if defined(__PX4_POSIX) && defined(OPTEE_AVAILABLE)
		mission_client_reset_chain();
		PX4_INFO("chain reset to genesis");
#endif
		return 0;
	}

	return print_usage("unknown command");
}

int PositionChain::print_usage(const char *reason)
{
	if (reason) { PX4_WARN("%s\n", reason); }

	PRINT_MODULE_DESCRIPTION(
		R"DESCR_STR(
### Description
Tamper-evident GPS hash chain using OP-TEE TrustZone.

Each vehicle_global_position update is fed into the TA in secure world:
  new_hash = SHA256(prev_hash || lat || lon || alt || timestamp_us)

The chain state (prev_hash) never leaves TrustZone.
Results are published as position_hash_chain and logged in the .ulg file.

### Examples
  position_chain start
  position_chain perf 1000     # benchmark 1000 OP-TEE calls
  position_chain hash          # print current chain hash from TA
  position_chain reset         # reset chain to genesis
)DESCR_STR");

	PRINT_MODULE_USAGE_NAME("position_chain", "system");
	PRINT_MODULE_USAGE_COMMAND("start");
	PRINT_MODULE_USAGE_COMMAND_DESCR("perf", "[N]   Benchmark N OP-TEE calls (default 1000)");
	PRINT_MODULE_USAGE_COMMAND_DESCR("hash", "      Print current chain hash from TA");
	PRINT_MODULE_USAGE_COMMAND_DESCR("reset", "     Reset chain to genesis state");
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();
	return 0;
}
