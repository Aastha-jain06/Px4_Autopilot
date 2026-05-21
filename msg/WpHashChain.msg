uint64 timestamp          # time since system start (microseconds)

uint16 wp_seq             # mission sequence number of the waypoint reached
uint16 nav_cmd            # MAVLink command of the waypoint (16=WP, 22=TAKEOFF, 21=LAND)

float32 planned_lat       # planned latitude from navigator_mission_item
float32 planned_lon       # planned longitude from navigator_mission_item
float32 planned_alt       # planned altitude from navigator_mission_item (m AMSL)

uint32 chain_seq          # position in the hash chain (increments per waypoint)
uint8[32] chain_hash      # SHA-256 hash at this step from OP-TEE TrustZone
uint32 compute_time_us    # OP-TEE call duration in microseconds
bool optee_used           # true if hash was computed in TrustZone

# TOPICS wp_hash_chain
