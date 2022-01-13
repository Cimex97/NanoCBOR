if(CONFIG_NANOCBOR)
    zephyr_interface_library_named(NANOCBOR)
    zephyr_library_compile_definitions(NANOCBOR_BYTEORDER_HEADER="sys/byteorder.h")

    zephyr_library_compile_options("-DNANOCBOR_BE64TOH_FUNC(be)=sys_be64_to_cpu(be)")
    zephyr_library_compile_options("-DNANOCBOR_HTOBE64_FUNC(he)=sys_cpu_to_be64(he)")
    zephyr_library_compile_options("-DNANOCBOR_HTOBE32_FUNC(he)=sys_cpu_to_be32(he)")

    zephyr_include_directories(NANOCBOR INTERFACE ../include)

    zephyr_library_sources(../src/decoder.c)
    zephyr_library_sources(../src/encoder.c)

    zephyr_library_link_libraries(NANOCBOR)
endif()
