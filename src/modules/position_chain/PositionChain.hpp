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
#include <uORB/topics/vehicle_global_position.h>
#include <uORB/topics/vehicle_land_detected.h>
#include <uORB/topics/position_hash_chain.h>

class PositionChain : public ModuleBase, public px4::WorkItem
{
public:
	PositionChain();
	~PositionChain() override;

	static ModuleBase::Descriptor desc;

	static int task_spawn(int argc, char *argv[]);
	static int custom_command(int argc, char *argv[]);
	static int print_usage(const char *reason = nullptr);

	int  print_status() override;
	bool start();

private:
	void Run() override;

	static void run_perf_test(int iterations);

	/*
	 * Trigger: fires whenever the navigator activates a new mission item
	 * (i.e. each waypoint transition during a mission).
	 */
	uORB::SubscriptionCallbackWorkItem _nav_item_sub{this, ORB_ID(navigator_mission_item)};

	/* Read current EKF2 position at the moment of waypoint transition */
	uORB::Subscription _pos_sub{ORB_ID(vehicle_global_position)};
	uORB::Subscription _land_sub{ORB_ID(vehicle_land_detected)};

	uORB::Publication<position_hash_chain_s> _chain_pub{ORB_ID(position_hash_chain)};

	perf_counter_t _optee_call_perf;
	perf_counter_t _loop_perf;

	bool     _optee_ok{false};
	bool     _in_flight{false};
	uint32_t _chain_seq{0};
	uint16_t _last_wp_seq{UINT16_MAX};   /* last waypoint sequence number hashed */
	uint8_t  _chain_hash[32]{};
};
