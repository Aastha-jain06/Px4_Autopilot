#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <tee_api_defines.h>
#include <tee_api.h>
#include "ta_mission_defines.h"

#define TA_UUID TA_MISSION_UUID

#define TA_FLAGS (TA_FLAG_EXEC_DDR | TA_FLAG_MULTI_SESSION)

#define TA_STACK_SIZE (2 * 1024)
#define TA_DATA_SIZE (32 * 1024)

#define TA_CURRENT_TA_EXT_PROPERTIES \
    { "gp.ta.description", USER_TA_PROP_TYPE_STRING, \
        "Mission Verification TA" }, \
    { "gp.ta.version", USER_TA_PROP_TYPE_U32, 0x00010000 }
