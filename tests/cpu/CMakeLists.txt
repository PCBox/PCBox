# CPU fixtures can be built independently of the GUI and Google Test.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(pcbox_cpu_tests LANGUAGES C)
    include(CTest)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|amd64|x86_64")
        message(FATAL_ERROR "The CPU execution fixtures require an x86-64 host")
    endif()
    set(ARCH x86_64)
    add_compile_options($<$<C_COMPILER_ID:GNU,Clang>:-fno-strict-aliasing>)
endif()
get_filename_component(CPU_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(ARCH STREQUAL "x86_64" AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # Available to CLion without enabling the full test suite; normal builds
    # do not build or run benchmarks. Match the emulator's optimization flags.
    add_executable(cpu_microbench EXCLUDE_FROM_ALL cpu_microbench.c
        ../../src/codegen_new/codegen_ops_mov.c
        ../../src/codegen_new/codegen_ops_jump.c
        ../../src/codegen_new/codegen_ops_helpers.c
        ../../src/codegen_new/codegen_block.c
        ../../src/codegen_new/codegen_backend_x86-64_ops.c
        ../../src/codegen_new/codegen_backend_x86-64_ops_sse.c)
    set_target_properties(cpu_microbench PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
    target_compile_definitions(cpu_microbench PRIVATE USE_DYNAREC USE_NEW_DYNAREC
        CPU_BENCH_BUILD="$<CONFIG>")
    target_include_directories(cpu_microbench PRIVATE
        ${CPU_SOURCE_ROOT}/src/include ${CPU_SOURCE_ROOT}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include)
    target_compile_options(cpu_microbench PRIVATE -ffunction-sections -fdata-sections
        $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
    target_link_options(cpu_microbench PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>)
    if(NOT WIN32)
        target_link_libraries(cpu_microbench PRIVATE m)
    endif()
endif()

if(BUILD_TESTING AND ARCH STREQUAL "x86_64")
    add_executable(sse_entry_test sse_entry_test.c)
    target_compile_definitions(sse_entry_test PRIVATE USE_NEW_DYNAREC)
    target_include_directories(sse_entry_test PRIVATE
        ${CPU_SOURCE_ROOT}/src/include
        ${CPU_SOURCE_ROOT}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    add_test(NAME sse_entry_test COMMAND sse_entry_test)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_executable(cpu_fastpath_test cpu_fastpath_test.c
            ../../src/codegen_new/codegen_ops_mov.c
            ../../src/codegen_new/codegen_ops_jump.c
            ../../src/codegen_new/codegen_ops_helpers.c
            ../../src/codegen_new/codegen_block.c
            ../../src/codegen_new/codegen_backend_x86-64_ops.c
            ../../src/codegen_new/codegen_backend_x86-64_ops_sse.c)
        target_compile_definitions(cpu_fastpath_test PRIVATE USE_DYNAREC USE_NEW_DYNAREC)
        target_include_directories(cpu_fastpath_test PRIVATE
            ${CPU_SOURCE_ROOT}/src/include ${CPU_SOURCE_ROOT}/src/cpu
            ${PROJECT_BINARY_DIR}/src/include)
        target_compile_options(cpu_fastpath_test PRIVATE -ffunction-sections -fdata-sections
            $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
        target_link_options(cpu_fastpath_test PRIVATE
            $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
            $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>)
        if(NOT WIN32)
            target_link_libraries(cpu_fastpath_test PRIVATE m)
        endif()
        add_test(NAME cpu_fastpath_test COMMAND cpu_fastpath_test)

        add_executable(hint_nop_test hint_nop_test.c
            ../../src/codegen_new/codegen_ops_misc.c
            ../../src/codegen_new/codegen_ops_helpers.c
            ../../src/codegen_new/codegen_block.c)
        target_compile_definitions(hint_nop_test PRIVATE USE_DYNAREC USE_NEW_DYNAREC)
        target_include_directories(hint_nop_test PRIVATE
            ${CPU_SOURCE_ROOT}/src/include ${CPU_SOURCE_ROOT}/src/cpu
            ${CPU_SOURCE_ROOT}/src/codegen_new ${PROJECT_BINARY_DIR}/src/include)
        target_compile_options(hint_nop_test PRIVATE -ffunction-sections -fdata-sections
            $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
        target_link_options(hint_nop_test PRIVATE
            $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
            $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>)
        add_test(NAME hint_nop_test COMMAND hint_nop_test)

        add_executable(jump_unroll_test jump_unroll_test.c
            ../../src/codegen_new/codegen_ops_jump.c
            ../../src/codegen_new/codegen_ops_helpers.c
            ../../src/codegen_new/codegen_block.c
            ../../src/codegen_new/codegen_backend_x86-64_ops.c
            ../../src/codegen_new/codegen_backend_x86-64_ops_sse.c)
        target_compile_definitions(jump_unroll_test PRIVATE USE_DYNAREC USE_NEW_DYNAREC)
        target_include_directories(jump_unroll_test PRIVATE
            ${CPU_SOURCE_ROOT}/src/include ${CPU_SOURCE_ROOT}/src/cpu
            ${CPU_SOURCE_ROOT}/src/codegen_new ${PROJECT_BINARY_DIR}/src/include)
        target_compile_options(jump_unroll_test PRIVATE -ffunction-sections -fdata-sections
            $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
        target_link_options(jump_unroll_test PRIVATE
            $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
            $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>)
        add_test(NAME jump_unroll_test COMMAND jump_unroll_test)

        add_executable(ram_register_test ram_register_test.c
            ../../src/codegen_new/codegen_ops_mov.c
            ../../src/codegen_new/codegen_backend_x86-64_ops.c
            ../../src/codegen_new/codegen_backend_x86-64_ops_sse.c)
        target_compile_definitions(ram_register_test PRIVATE USE_DYNAREC USE_NEW_DYNAREC)
        target_include_directories(ram_register_test PRIVATE
            ${CPU_SOURCE_ROOT}/src/include ${CPU_SOURCE_ROOT}/src/cpu
            ${PROJECT_BINARY_DIR}/src/include)
        target_compile_options(ram_register_test PRIVATE -ffunction-sections -fdata-sections
            $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
        target_link_options(ram_register_test PRIVATE
            $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
            $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>)
        add_test(NAME ram_register_test COMMAND ram_register_test)

        add_executable(sse_load_test
            sse_load_test.c
            ../../src/codegen_new/codegen_backend_x86-64.c
            ../../src/codegen_new/codegen_backend_x86-64_ops.c
            ../../src/codegen_new/codegen_backend_x86-64_ops_sse.c
        )
        target_compile_definitions(sse_load_test PRIVATE USE_NEW_DYNAREC)
        target_include_directories(sse_load_test PRIVATE
            ${CPU_SOURCE_ROOT}/src/include
            ${CPU_SOURCE_ROOT}/src/cpu
            ${PROJECT_BINARY_DIR}/src/include
        )
        target_compile_options(sse_load_test PRIVATE -ffunction-sections -fdata-sections)
        # PE unwind tables otherwise keep unused backend functions alive.
        target_compile_options(sse_load_test PRIVATE $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
        target_link_options(sse_load_test PRIVATE
            $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
            $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>
        )
        add_test(NAME sse_load_test COMMAND sse_load_test)
        add_executable(sse_store_test
            sse_store_test.c
            ../../src/codegen_new/codegen_backend_x86-64.c
            ../../src/codegen_new/codegen_backend_x86-64_ops.c
            ../../src/codegen_new/codegen_backend_x86-64_ops_sse.c
        )
        target_compile_definitions(sse_store_test PRIVATE USE_NEW_DYNAREC)
        target_include_directories(sse_store_test PRIVATE
            ${CPU_SOURCE_ROOT}/src/include
            ${CPU_SOURCE_ROOT}/src/cpu
            ${PROJECT_BINARY_DIR}/src/include
        )
        target_compile_options(sse_store_test PRIVATE -ffunction-sections -fdata-sections)
        target_compile_options(sse_store_test PRIVATE $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
        target_link_options(sse_store_test PRIVATE
            $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
            $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>
        )
        add_test(NAME sse_store_test COMMAND sse_store_test)

        add_executable(scalar_memory_test
            scalar_memory_test.c
            ../../src/codegen_new/codegen_backend_x86-64.c
            ../../src/codegen_new/codegen_backend_x86-64_ops.c
            ../../src/codegen_new/codegen_backend_x86-64_ops_sse.c
        )
        target_compile_definitions(scalar_memory_test PRIVATE USE_NEW_DYNAREC)
        target_include_directories(scalar_memory_test PRIVATE
            ${CPU_SOURCE_ROOT}/src/include
            ${CPU_SOURCE_ROOT}/src/cpu
            ${PROJECT_BINARY_DIR}/src/include
        )
        target_compile_options(scalar_memory_test PRIVATE -ffunction-sections -fdata-sections)
        target_compile_options(scalar_memory_test PRIVATE $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
        target_link_options(scalar_memory_test PRIVATE
            $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
            $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>
        )
        add_test(NAME scalar_memory_test COMMAND scalar_memory_test)

    endif()
endif()
