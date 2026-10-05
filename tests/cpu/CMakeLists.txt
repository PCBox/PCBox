if(ARCH STREQUAL "x86_64" AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # Available to CLion without enabling the full test suite; normal builds
    # do not build or run benchmarks. Match the emulator's optimization flags.
    add_executable(cpu_microbench EXCLUDE_FROM_ALL cpu_microbench.c
        ../../src/codegen_new/codegen_ops_mov.c
        ../../src/codegen_new/codegen_backend_x86-64_ops.c
        ../../src/codegen_new/codegen_backend_x86-64_ops_sse.c)
    set_target_properties(cpu_microbench PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
    target_compile_definitions(cpu_microbench PRIVATE USE_DYNAREC USE_NEW_DYNAREC
        CPU_BENCH_BUILD="$<CONFIG>")
    target_include_directories(cpu_microbench PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include)
    target_compile_options(cpu_microbench PRIVATE -ffunction-sections -fdata-sections
        $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
    target_link_options(cpu_microbench PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>)
endif()

if(BUILD_TESTING AND ARCH STREQUAL "x86_64")
    add_executable(codegen_profile_report_test codegen_profile_report_test.c
        ../../src/codegen_new/codegen_profile.c)
    target_include_directories(codegen_profile_report_test PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include ${PROJECT_BINARY_DIR}/src/include)
    add_test(NAME codegen_profile_report_test COMMAND ${CMAKE_COMMAND} -E env
        PCBOX_DYNAREC_PROFILE=1 $<TARGET_FILE:codegen_profile_report_test>)
    add_test(NAME codegen_profile_disabled_test COMMAND ${CMAKE_COMMAND} -E env
        PCBOX_DYNAREC_PROFILE=0 $<TARGET_FILE:codegen_profile_report_test> disabled)
    add_executable(sse_entry_test sse_entry_test.c)
    target_compile_definitions(sse_entry_test PRIVATE USE_NEW_DYNAREC)
    target_include_directories(sse_entry_test PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    add_test(NAME sse_entry_test COMMAND sse_entry_test)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_executable(hint_nop_test hint_nop_test.c
            ../../src/codegen_new/codegen_ops_misc.c
            ../../src/codegen_new/codegen_ops_helpers.c
            ../../src/codegen_new/codegen_block.c)
        target_compile_definitions(hint_nop_test PRIVATE USE_DYNAREC USE_NEW_DYNAREC)
        target_include_directories(hint_nop_test PRIVATE
            ${PROJECT_SOURCE_DIR}/src/include ${PROJECT_SOURCE_DIR}/src/cpu
            ${PROJECT_SOURCE_DIR}/src/codegen_new ${PROJECT_BINARY_DIR}/src/include)
        target_compile_options(hint_nop_test PRIVATE -ffunction-sections -fdata-sections
            $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
        target_link_options(hint_nop_test PRIVATE
            $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
            $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>)
        add_test(NAME hint_nop_test COMMAND hint_nop_test)

        add_executable(ram_register_test ram_register_test.c
            ../../src/codegen_new/codegen_ops_mov.c
            ../../src/codegen_new/codegen_backend_x86-64_ops.c
            ../../src/codegen_new/codegen_backend_x86-64_ops_sse.c)
        target_compile_definitions(ram_register_test PRIVATE USE_DYNAREC USE_NEW_DYNAREC)
        target_include_directories(ram_register_test PRIVATE
            ${PROJECT_SOURCE_DIR}/src/include ${PROJECT_SOURCE_DIR}/src/cpu
            ${PROJECT_BINARY_DIR}/src/include)
        target_compile_options(ram_register_test PRIVATE -ffunction-sections -fdata-sections
            $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
        target_link_options(ram_register_test PRIVATE
            $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
            $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>)
        add_test(NAME ram_register_test COMMAND ram_register_test)

        add_executable(codegen_profile_jit_test codegen_profile_jit_test.c
            codegen_profile_test_stubs.c
            ../../src/codegen_new/codegen_backend_x86-64_ops.c
            ../../src/codegen_new/codegen_backend_x86-64_ops_sse.c)
        target_compile_definitions(codegen_profile_jit_test PRIVATE USE_NEW_DYNAREC)
        target_include_directories(codegen_profile_jit_test PRIVATE
            ${PROJECT_SOURCE_DIR}/src/include ${PROJECT_SOURCE_DIR}/src/cpu
            ${PROJECT_BINARY_DIR}/src/include)
        target_compile_options(codegen_profile_jit_test PRIVATE -ffunction-sections -fdata-sections
            $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
        target_link_options(codegen_profile_jit_test PRIVATE
            $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
            $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>)
        add_test(NAME codegen_profile_jit_test COMMAND codegen_profile_jit_test)
        add_executable(sse_load_test
            sse_load_test.c
            ../../src/codegen_new/codegen_backend_x86-64.c
            ../../src/codegen_new/codegen_backend_x86-64_ops.c
            ../../src/codegen_new/codegen_backend_x86-64_ops_sse.c
        )
        target_compile_definitions(sse_load_test PRIVATE USE_NEW_DYNAREC)
        target_include_directories(sse_load_test PRIVATE
            ${PROJECT_SOURCE_DIR}/src/include
            ${PROJECT_SOURCE_DIR}/src/cpu
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
            ${PROJECT_SOURCE_DIR}/src/include
            ${PROJECT_SOURCE_DIR}/src/cpu
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
            ${PROJECT_SOURCE_DIR}/src/include
            ${PROJECT_SOURCE_DIR}/src/cpu
            ${PROJECT_BINARY_DIR}/src/include
        )
        target_compile_options(scalar_memory_test PRIVATE -ffunction-sections -fdata-sections)
        target_compile_options(scalar_memory_test PRIVATE $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
        target_link_options(scalar_memory_test PRIVATE
            $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
            $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>
        )
        add_test(NAME scalar_memory_test COMMAND scalar_memory_test)

        foreach(emitter_test sse_load_test sse_store_test scalar_memory_test)
            target_sources(${emitter_test} PRIVATE codegen_profile_test_stubs.c)
            add_test(NAME ${emitter_test}_profile COMMAND ${CMAKE_COMMAND} -E env
                PCBOX_TEST_PROFILE=1 $<TARGET_FILE:${emitter_test}>)
        endforeach()
    endif()
endif()
