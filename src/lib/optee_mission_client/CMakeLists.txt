px4_add_library(optee_mission_client
	mission_client.c
)

target_include_directories(optee_mission_client
	PUBLIC  .
	PRIVATE ${PX4_SOURCE_DIR}
)

# Locate libteec and its header manually (no pkg-config entry on Jetson)
find_library(TEEC_LIBRARY teec
	PATHS /usr/lib /usr/lib/aarch64-linux-gnu /lib /lib/aarch64-linux-gnu
	NO_DEFAULT_PATH
)
find_path(TEEC_INCLUDE_DIR tee_client_api.h
	PATHS /usr/include /usr/local/include
	NO_DEFAULT_PATH
)

if(TEEC_LIBRARY AND TEEC_INCLUDE_DIR)
	message(STATUS "OP-TEE TEE Client: ${TEEC_LIBRARY}")
	target_link_libraries(optee_mission_client PUBLIC ${TEEC_LIBRARY})
	target_include_directories(optee_mission_client PUBLIC ${TEEC_INCLUDE_DIR})
	target_compile_definitions(optee_mission_client PUBLIC OPTEE_AVAILABLE=1)
else()
	message(WARNING "libteec not found — mission_client will use stubs (no secure world)")
	target_compile_definitions(optee_mission_client PUBLIC OPTEE_NOT_AVAILABLE=1)
endif()
