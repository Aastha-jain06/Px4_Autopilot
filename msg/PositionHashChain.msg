# Tamper-evident hash chain record for vehicle_global_position.
# Each entry's chain_hash = SHA256(prev_chain_hash || lat || lon || alt || timestamp_us)
# computed inside OP-TEE TrustZone. compute_time_us measures the round-trip OP-TEE call.

uint64 timestamp           # PX4 system time (us)

float64 lat                # Latitude (degrees)
float64 lon                # Longitude (degrees)
float32 alt                # Altitude AMSL (metres)

uint32 chain_seq           # Monotonically increasing chain index (0 = genesis)
uint8[32] chain_hash       # SHA-256 chain hash from secure world
uint32 compute_time_us     # Time for OP-TEE round-trip call (microseconds)
bool optee_used            # true = TrustZone, false = OP-TEE unavailable

# TOPICS position_hash_chain
