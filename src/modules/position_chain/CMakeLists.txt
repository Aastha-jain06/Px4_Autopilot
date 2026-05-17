px4_add_module(
	MODULE modules__position_chain
	MAIN position_chain
	SRCS
		PositionChain.cpp
		PositionChain.hpp
		position_chain_main.cpp
	DEPENDS
		px4_work_queue
)

if("${PX4_PLATFORM}" STREQUAL "posix")
	if(NOT TARGET optee_mission_client)
		add_subdirectory(${PX4_SOURCE_DIR}/src/lib/optee_mission_client
		                 ${CMAKE_CURRENT_BINARY_DIR}/optee_mission_client)
	endif()
	target_link_libraries(modules__position_chain PRIVATE optee_mission_client)
endif()
