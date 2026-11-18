# Shared device-free production CPU/MMU/JIT build for execution fixtures.
function(pcbox_full_cpu_target target)
    set(core "${CPU_SOURCE_ROOT}/src")
    set(cpu_sources cpu.c cpu_lapic.c cache_as_ram.c cpu_table.c fpu.c x86.c 386.c 386_common.c
        386_dynarec.c 386_dynarec_ops.c x86_ops_mmx.c x86_rep_host.c x87_ferr.c x86seg_common.c
        x86seg.c x86seg_2386.c x87.c x87_timings.c
        codegen_timing_486.c codegen_timing_686.c codegen_timing_common.c
        codegen_timing_k6.c codegen_timing_pentium.c codegen_timing_p6.c
        codegen_timing_winchip.c codegen_timing_winchip2.c)
    list(TRANSFORM cpu_sources PREPEND "${core}/cpu/")
    if(EXISTS "${core}/cpu/codegen_timing_p4.c")
        list(APPEND cpu_sources "${core}/cpu/codegen_timing_p4.c")
    endif()
    file(GLOB jit_sources CONFIGURE_DEPENDS "${core}/codegen/*.c")
    list(FILTER jit_sources EXCLUDE REGEX "codegen_backend_(arm64|loongarch64)|_x87h[.]c$")
    # The main project defines softfloat3e later in src/cpu. Only a standalone
    # fixture build owns this target; the generator expression below also
    # accepts the main project's target once configuration has finished.
    if(CMAKE_SOURCE_DIR STREQUAL "${CPU_SOURCE_ROOT}/tests/cpu" AND NOT TARGET softfloat3e)
        add_subdirectory("${core}/cpu/softfloat3e" "${CMAKE_CURRENT_BINARY_DIR}/softfloat3e" EXCLUDE_FROM_ALL)
        target_include_directories(softfloat3e PRIVATE "${core}/include")
    endif()
    target_sources(${target} PRIVATE ${cpu_sources} ${jit_sources}
        "${CPU_SOURCE_ROOT}/tests/cpu/full_core_platform.c"
        $<TARGET_OBJECTS:softfloat3e> "${core}/mem/mem.c" "${core}/mem/mmu_2386.c"
        "${core}/mem/smram.c" "${core}/lapic.c" "${core}/lapic_policy.c"
        "${core}/apic_cpu_policy.c" "${core}/lapic_mmio.c" "${core}/lapic_delivery.c")
    set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
    target_compile_definitions(${target} PRIVATE USE_DYNAREC USE_NEW_DYNAREC)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/86box")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/86box/version.h" CONTENT
        "#define EMU_NAME \"PCBox CPU fixture\"\n#define EMU_VERSION \"test\"\n")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/include"
        "${core}/include" "${core}/cpu" "${core}/codegen")
    target_compile_options(${target} PRIVATE -fno-strict-aliasing -ffunction-sections -fdata-sections
        $<$<PLATFORM_ID:Windows>:-fno-asynchronous-unwind-tables>)
    target_link_options(${target} PRIVATE
        $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
        $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>)
    if(NOT WIN32)
        target_link_libraries(${target} PRIVATE m)
    endif()
endfunction()
