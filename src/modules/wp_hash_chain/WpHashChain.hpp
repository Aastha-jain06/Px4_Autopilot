#pragma once

#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/defines.h>
#include <px4_platform_common/module.h>
#include <px4_platform_common/module_params.h>
#include <px4_platform_common/px4_work_queue/WorkItem.hpp>
#include <lib/perf/perf_counter.h>
#include <drivers/drv_hrt.h>

#include <uORB/Subscription.hpp>
#include <uORB/SubscriptionCallback.hpp>
#include <uORB/Publication.hpp>
#include <uORB/topics/navigator_mission_item.h>
#include <uORB/topics/mission_result.h>
#include <uORB/topics/vehicle_land_detected.h>
#include <uORB/topics/wp_hash_chain.h>

/*
 * Cache entry: planned coords for one mission sequence number.
 * Populated when navigator_mission_item fires (heading TO waypoint N).
 * Consumed when mission_result.seq_reached = N (waypoint N actually reached).
 */
struct WpCoordCache {
	float    lat{0.f};
	float    lon{0.f};
	float    alt{0.f};
	uint16_t nav_cmd{0};
	bool     valid{false};
};

static constexpr int CACHE_SIZE = 32;   /* enough for any sane mission */

class WpHashChain : public ModuleBase, public px4::WorkItem
{
public:
	WpHashChain();
	~WpHashChain() override;

	static ModuleBase::Descriptor desc;

	static int task_spawn(int argc, char *argv[]);
	static int custom_command(int argc, char *argv[]);
	static int print_usage(const char *reason = nullptr);

	int  print_status() override;
	bool start();

private:
	void Run() override;

	void hash_waypoint(uint16_t seq);
	void on_landing();

	/*
	 * Two callbacks so Run() fires on both waypoint transitions AND
	 * land-state changes (landing happens after the last nav item fires).
	 */
	uORB::SubscriptionCallbackWorkItem _nav_item_sub{this, ORB_ID(navigator_mission_item)};
	uORB::SubscriptionCallbackWorkItem _land_sub{this, ORB_ID(vehicle_land_detected)};

	uORB::Subscription _mission_result_sub{ORB_ID(mission_result)};

	uORB::Publication<wp_hash_chain_s> _chain_pub{ORB_ID(wp_hash_chain)};

	perf_counter_t _optee_call_perf;
	perf_counter_t _loop_perf;

	bool     _optee_ok{false};
	bool     _in_flight{false};
	uint32_t _chain_seq{0};
	int32_t  _last_seq_reached{-1};
	uint8_t  _chain_hash[32]{};

	/* coord cache: index = mission sequence number */
	WpCoordCache _cache[CACHE_SIZE]{};

	/* frozen[N]=true once mission_result.seq_reached=N fires.
	 * After that, navigator re-publishes (loiter adjustments) are ignored. */
	bool _seq_frozen[CACHE_SIZE]{};
};
