px4_add_module(
	MODULE modules__wp_hash_chain
	MAIN wp_hash_chain
	SRCS
		WpHashChain.cpp
		WpHashChain.hpp
		wp_hash_chain_main.cpp
	DEPENDS
		px4_work_queue
)

if("${PX4_PLATFORM}" STREQUAL "posix")
	if(NOT TARGET optee_mission_client)
		add_subdirectory(${PX4_SOURCE_DIR}/src/lib/optee_mission_client
		                 ${CMAKE_CURRENT_BINARY_DIR}/optee_mission_client)
	endif()
	target_link_libraries(modules__wp_hash_chain PRIVATE optee_mission_client)
endif()
