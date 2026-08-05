SET(EXAMPLE_NAME "Vulkan-pbr")

set(MAIN_CPP main/main.cpp)

file(GLOB MAIN_SOURCE main/*.cpp)
file(GLOB UI_HEADER 
		"ui/*.h"
		"ui/*.hpp"
)
file(GLOB UI_SOURCE
		"ui/*.cpp"
)

file(GLOB APPLICATION_HEADER
		"application/*.h"
		"application/*.hpp"
)

file(GLOB APPLICATION_SOURCE
		"application/*.cpp"
)


file(GLOB SHADERS "../data/shaders/*.vert" "../data/shaders/*.frag" "../data/shaders/*.comp" "../data/shaders/*.geom" "../data/shaders/*.tesc" "../data/shaders/*.tese")
file(GLOB SHADER_INCLUDES "../data/shaders/includes/*.glsl")
source_group("Shaders" FILES ${SHADERS})
source_group("Shaders\\Includes" FILES ${SHADER_INCLUDES})
if(WIN32)
	add_executable(${EXAMPLE_NAME} WIN32 ${MAIN_CPP} ${MAIN_SOURCE} ${UI_HEADER} ${UI_SOURCE} ${APPLICATION_HEADER} ${APPLICATION_SOURCE} ${SHADERS} ${SHADER_INCLUDES})
	target_link_libraries(${EXAMPLE_NAME} engine ${Vulkan_LIBRARY} ${WINLIBS})
else(WIN32)
	add_executable(${EXAMPLE_NAME} ${MAIN_CPP} ${MAIN_SOURCE} ${UI_HEADER} ${UI_SOURCE} ${APPLICATION_HEADER} ${APPLICATION_SOURCE} ${SHADERS} ${SHADER_INCLUDES})
	target_link_libraries(${EXAMPLE_NAME} engine )
endif(WIN32)

add_custom_target(compile_shaders ALL 
	COMMAND E:/Python312/python.exe "${CMAKE_SOURCE_DIR}/scripts/compile_shaders.py"
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	COMMENT "Compiling GLSL shaders..."
)

add_dependencies(${EXAMPLE_NAME} compile_shaders)

if(RESOURCE_INSTALL_DIR)
	install(TARGETS ${EXAMPLE_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()