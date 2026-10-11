cmake_minimum_required(VERSION 3.16)
project(frame_mask LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

add_library(frame_mask STATIC mask/mask_protocol.cpp)
target_include_directories(frame_mask PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(frame_mask PRIVATE -Wall -Wextra -Wpedantic)
add_executable(mask_endpoint tools/mask_endpoint.cpp)
target_link_libraries(mask_endpoint PRIVATE frame_mask)
target_compile_options(mask_endpoint PRIVATE -Wall -Wextra -Wpedantic)
if(WIN32)
    target_link_libraries(mask_endpoint PRIVATE ws2_32)
endif()
if(MINGW)
    target_link_options(mask_endpoint PRIVATE -static-libgcc -static-libstdc++)
endif()

if(BUILD_TESTING)
    add_executable(test_mask_protocol tests/test_mask_protocol.cpp)
    target_link_libraries(test_mask_protocol PRIVATE frame_mask)
    add_test(NAME mask_protocol COMMAND test_mask_protocol)
    add_executable(test_mask_module_guard tests/test_mask_module_guard.cpp)
    target_include_directories(test_mask_module_guard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME mask_module_guard COMMAND test_mask_module_guard)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME mask_endpoint COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mask_endpoint.py $<TARGET_FILE:mask_endpoint>)
    add_test(NAME mask_shader COMMAND ${Python3_EXECUTABLE}
        -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -p test_mask_shader.py)
    add_test(NAME mask_bridge_collector COMMAND ${Python3_EXECUTABLE}
        -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -p test_collect_mask_bridge.py)
    add_test(NAME mask_pc_bridge_collector COMMAND ${Python3_EXECUTABLE}
        -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -p test_collect_pc_bridge.py)
    add_test(NAME mask_live_bridge_inspection COMMAND ${Python3_EXECUTABLE}
        -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -p test_inspect_live_bridge.py)
    find_package(Vulkan QUIET)
    find_program(GLSLC glslc)
    find_program(SPIRV_AS spirv-as)
    find_program(SPIRV_VAL spirv-val)
    if(Vulkan_FOUND AND GLSLC AND SPIRV_AS AND SPIRV_VAL)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mrchroma_shaders.h
            COMMAND ${GLSLC} -O --target-env=vulkan1.1 ${CMAKE_CURRENT_SOURCE_DIR}/layer/key.vert
                -o ${CMAKE_CURRENT_BINARY_DIR}/key.vert.spv
            COMMAND ${GLSLC} -O --target-env=vulkan1.1 ${CMAKE_CURRENT_SOURCE_DIR}/layer/key.frag
                -o ${CMAKE_CURRENT_BINARY_DIR}/key.frag.spv
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_spv.py
                ${CMAKE_CURRENT_BINARY_DIR}/mrchroma_shaders.h
                ${CMAKE_CURRENT_BINARY_DIR}/key.vert.spv ${CMAKE_CURRENT_BINARY_DIR}/key.frag.spv
            DEPENDS layer/key.vert layer/key.frag tools/embed_spv.py)
        add_library(VkLayer_mrchroma SHARED layer/mrchroma_layer.cpp
            ${CMAKE_CURRENT_BINARY_DIR}/mrchroma_shaders.h)
        target_include_directories(VkLayer_mrchroma PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${Vulkan_INCLUDE_DIRS})
        target_compile_options(VkLayer_mrchroma PRIVATE -Wall -Wno-missing-field-initializers -fvisibility=hidden)
        target_link_libraries(VkLayer_mrchroma PRIVATE pthread)
        add_executable(test_mask_gpu tests/test_mask_gpu.cpp)
        target_link_libraries(test_mask_gpu PRIVATE Vulkan::Vulkan)
        target_compile_options(test_mask_gpu PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME mask_gpu COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_mask_gpu.py $<TARGET_FILE:test_mask_gpu>
            $<TARGET_FILE:test_mask_module_guard>)
    else()
        message(FATAL_ERROR "Mask validation needs Vulkan headers, glslc and spirv-tools. For transport-only builds use -DBUILD_TESTING=OFF.")
    endif()
endif()
