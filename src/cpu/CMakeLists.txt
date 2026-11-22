#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script.
#
# Authors: David Hrdlička, <hrdlickadavid@outlook.com>
#          Jasmine Iwanek, <jriwanek@gmail.com>
#
#          Copyright 2020-2021 David Hrdlička.
#          Copyright 2024      Jasmine Iwanek.
#

add_library(cpu OBJECT
    cpu.c
    cpu_lapic.c
    cpu_table.c
    fpu.c x86.c
    808x.c
    808x_common.c
    808x_marty_86box.c
    vx0.c
    vx0_biu.c
    386.c
    386_common.c
    386_dynarec.c
    cache_as_ram.c
    x86_ops_mmx.c
    x86_rep_host.c
    x86seg_common.c
    x86seg.c
    x86seg_2386.c
    x87.c
    x87_ferr.c
    weitek3167.c
    x87_timings.c
    i8080.c
)

# The REP string kernels' generic versions are the host baseline whatever
# the build targets; the Zen 5 versions ask for AVX-512 themselves.
if((ARCH STREQUAL "x86_64") AND (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
    set_source_files_properties(x86_rep_host.c PROPERTIES COMPILE_OPTIONS "-march=x86-64;-mtune=generic")
endif()

if(AMD_K5)
    target_compile_definitions(cpu PRIVATE USE_AMD_K5)

    if(DYNAREC)
        add_library(ctk5 OBJECT codegen_timing_k5.c)
        target_link_libraries(PCBox ctk5)
    endif()

endif()

if(DYNAREC)
    target_sources(cpu PRIVATE
        386_dynarec_ops.c
    )

    add_library(cgt OBJECT
        codegen_timing_486.c
        codegen_timing_686.c
        codegen_timing_common.c
        codegen_timing_k6.c
        codegen_timing_pentium.c
        codegen_timing_p6.c
        codegen_timing_p4.c
        codegen_timing_winchip.c
        codegen_timing_winchip2.c
    )

    if(FAST_DYNAREC)
        target_sources(cgt PRIVATE
            codegen_timing_fast.c
        )
    endif()
endif()

add_subdirectory(softfloat3e)
target_link_libraries(PCBox softfloat3e)
