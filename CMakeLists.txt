cmake_minimum_required(VERSION 3.16)
project(frame_mask LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
option(FRAME_MASK_BUILD_TRACE "Build the opt-in passive compositor diagnostic layer" OFF)
option(FRAME_MASK_BUILD_RECEIVER_OBSERVER
    "Build the opt-in, version-pinned receiver scalar diagnostic (not installed)" OFF)

if(FRAME_MASK_BUILD_RECEIVER_OBSERVER)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The receiver observer supports Linux/glibc only")
    endif()
    find_package(Threads REQUIRED)
    add_library(frame_receiver_observer SHARED
        receiver/receiver_observer.cpp receiver/receiver_observer_core.cpp)
    target_include_directories(frame_receiver_observer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(frame_receiver_observer PRIVATE
        -Wall -Wextra -Wpedantic -fvisibility=hidden -ftls-model=initial-exec
        -fno-exceptions -fno-rtti)
    target_link_libraries(frame_receiver_observer PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    if(BUILD_TESTING)
        add_executable(test_receiver_observer
            tests/test_receiver_observer.cpp receiver/receiver_observer_core.cpp)
        target_include_directories(test_receiver_observer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_options(test_receiver_observer PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(test_receiver_observer PRIVATE Threads::Threads)
        if(NOT CMAKE_CROSSCOMPILING)
            add_test(NAME receiver_observer COMMAND test_receiver_observer)
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
                add_executable(test_receiver_observer_plt tests/test_receiver_observer_plt.cpp)
                target_compile_options(test_receiver_observer_plt PRIVATE -Wall -Wextra -Wpedantic)
                target_link_options(test_receiver_observer_plt PRIVATE -rdynamic)
                target_link_libraries(test_receiver_observer_plt PRIVATE Threads::Threads)
                find_package(Python3 REQUIRED COMPONENTS Interpreter)
                add_test(NAME receiver_observer_preload COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_receiver_observer_preload.py
                    $<TARGET_FILE:frame_receiver_observer> $<TARGET_FILE:test_receiver_observer_plt>)
            endif()
        endif()
    endif()
endif()

if(UNIX AND (BUILD_TESTING OR FRAME_MASK_BUILD_TRACE))
    find_package(Vulkan REQUIRED)
    find_package(Threads REQUIRED)
    add_library(VkLayer_frame_mask_trace SHARED layer/passive_pipeline_trace.cpp)
    target_include_directories(VkLayer_frame_mask_trace PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_compile_options(VkLayer_frame_mask_trace PRIVATE
        -Wall -Wextra -Wpedantic -fvisibility=hidden)
    target_link_options(VkLayer_frame_mask_trace PRIVATE -Wl,-Bsymbolic-functions)
    target_link_libraries(VkLayer_frame_mask_trace PRIVATE Threads::Threads)
    set(FRAME_MASK_TRACE_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}VkLayer_frame_mask_trace${CMAKE_SHARED_LIBRARY_SUFFIX}")
    configure_file(layer/VkLayer_frame_mask_trace.json.in
        ${CMAKE_CURRENT_BINARY_DIR}/VkLayer_frame_mask_trace.json @ONLY)
elseif(FRAME_MASK_BUILD_TRACE)
    message(FATAL_ERROR "The passive compositor trace targets Linux Vulkan")
endif()

add_library(frame_mask STATIC mask/mask_protocol.cpp mask/frame_association.cpp)
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
    add_executable(test_frame_association tests/test_frame_association.cpp)
    target_link_libraries(test_frame_association PRIVATE frame_mask)
    target_compile_options(test_frame_association PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME frame_association COMMAND test_frame_association)
    add_executable(test_mask_module_guard tests/test_mask_module_guard.cpp)
    target_include_directories(test_mask_module_guard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME mask_module_guard COMMAND test_mask_module_guard)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(UNIX)
        add_executable(test_import_observer tests/test_import_observer.cpp
            import/import_observer_core.cpp)
        target_include_directories(test_import_observer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_options(test_import_observer PRIVATE -Wall -Wextra -Wpedantic)
        find_package(Threads REQUIRED)
        target_link_libraries(test_import_observer PRIVATE Threads::Threads)
        add_test(NAME compositor_import_core COMMAND test_import_observer)
        add_executable(test_native_import_route tests/test_native_import_route.cpp
            import/native_import_route.cpp)
        target_include_directories(test_native_import_route PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_options(test_native_import_route PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME compositor_import_native_route COMMAND test_native_import_route)
        add_test(NAME compositor_import_entry_abi COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_import_entry_abi.py
            --source ${CMAKE_CURRENT_SOURCE_DIR}/import/native_import_entry.S)
        set_tests_properties(compositor_import_entry_abi PROPERTIES SKIP_RETURN_CODE 77)
        add_test(NAME unix_connection_snapshot COMMAND ${Python3_EXECUTABLE}
            -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests
            -p test_inspect_unix_connection.py)
        add_test(NAME receiver_observer_preparation COMMAND ${Python3_EXECUTABLE}
            -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests
            -p test_prepare_receiver_observer.py)
        add_test(NAME receiver_observer_session_recovery COMMAND ${Python3_EXECUTABLE}
            -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests
            -p test_receiver_observer_session.py)
        add_test(NAME frame_trace_session_recovery COMMAND ${Python3_EXECUTABLE}
            -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -p test_frame_trace_session.py)
    endif()
    if(TARGET VkLayer_frame_mask_trace)
        add_executable(test_passive_pipeline_trace tests/test_passive_pipeline_trace.cpp)
        target_include_directories(test_passive_pipeline_trace PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_options(test_passive_pipeline_trace PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(test_passive_pipeline_trace PRIVATE Threads::Threads)
        add_test(NAME passive_pipeline_trace COMMAND test_passive_pipeline_trace)
        add_executable(test_passive_binding_trace tests/test_passive_binding_trace.cpp)
        target_include_directories(test_passive_binding_trace PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${Vulkan_INCLUDE_DIRS})
        target_compile_options(test_passive_binding_trace PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(test_passive_binding_trace PRIVATE Threads::Threads)
        add_test(NAME passive_binding_trace COMMAND test_passive_binding_trace)
        foreach(suite passive_binding_journal passive_binding_extensions passive_binding_lookup)
            add_executable(test_${suite} tests/test_${suite}.cpp)
            target_include_directories(test_${suite} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${Vulkan_INCLUDE_DIRS})
            target_compile_options(test_${suite} PRIVATE -Wall -Wextra -Wpedantic)
            target_link_libraries(test_${suite} PRIVATE Threads::Threads)
            add_test(NAME ${suite} COMMAND test_${suite})
        endforeach()
        add_executable(test_import_resource_receipt tests/test_import_resource_receipt.cpp)
        target_include_directories(test_import_resource_receipt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${Vulkan_INCLUDE_DIRS})
        target_compile_options(test_import_resource_receipt PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(test_import_resource_receipt PRIVATE Threads::Threads)
        add_test(NAME compositor_import_resource_receipt COMMAND test_import_resource_receipt)
        add_executable(binding_throughput_fixture tests/binding_throughput_fixture.cpp)
        target_include_directories(binding_throughput_fixture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${Vulkan_INCLUDE_DIRS})
        target_compile_options(binding_throughput_fixture PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(binding_throughput_fixture PRIVATE Threads::Threads)
        add_test(NAME passive_binding_throughput COMMAND binding_throughput_fixture --check)
    endif()
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
        target_link_libraries(test_mask_gpu PRIVATE Vulkan::Vulkan Threads::Threads)
        target_compile_options(test_mask_gpu PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME mask_gpu COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_mask_gpu.py $<TARGET_FILE:test_mask_gpu>
            $<TARGET_FILE:test_mask_module_guard>)
        if(TARGET VkLayer_frame_mask_trace)
            add_test(NAME passive_pipeline_trace_gpu COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_passive_pipeline_trace.py
                $<TARGET_FILE:test_mask_gpu> ${CMAKE_CURRENT_BINARY_DIR})
        endif()
    else()
        message(FATAL_ERROR "Mask validation needs Vulkan headers, glslc and spirv-tools. For transport-only builds use -DBUILD_TESTING=OFF.")
    endif()
endif()
