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

#pragma once

#include "../Common.hpp"

#include <lib/dataman_client/DatamanClient.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/topics/mission.h>

class MissionHashCheck : public HealthAndArmingCheckBase
{
public:
	MissionHashCheck() = default;
	~MissionHashCheck();

	void checkAndReport(const Context &context, Report &reporter) override;

private:
	bool loadExpectedHash();
	bool loadExpectedSig();
	bool computeHash(dm_item_t dataman_id, uint16_t count, uint8_t out[32]);
	bool verifySignature(const uint8_t hash[32]);

	uORB::Subscription _mission_sub{ORB_ID(mission)};
	DatamanClient      _dataman_client;

	uint32_t _cached_mission_id{0};
	bool     _hash_file_present{false};
	bool     _hash_matches{false};
	bool     _sig_file_present{false};
	bool     _sig_verified{false};
	bool     _optee_initialized{false};
	bool     _evaluated{false};
	uint8_t  _expected_hash[32]{};
	uint8_t  _expected_sig[64]{};
};
