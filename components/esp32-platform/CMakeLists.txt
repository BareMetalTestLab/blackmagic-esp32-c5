set(BM_DIR "${CMAKE_SOURCE_DIR}/components/blackmagic")
# set(PLATFORM_DIR "esp32-platform")

set(BM_SOURCES

    # ${PLATFORM_DIR}/custom/swd-spi-tap.c
    ${BM_DIR}/src/platforms/common/swdptap.c
    ${BM_DIR}/src/platforms/common/jtagtap.c
    platform.c
    gdb-glue.c
)

set(BM_TARGETS
    ${BM_DIR}/src/target/adi.c
    ${BM_DIR}/src/target/adiv5.c
    ${BM_DIR}/src/target/adiv6.c
    ${BM_DIR}/src/target/adiv5_jtag.c
    ${BM_DIR}/src/target/adiv5_swd.c
    ${BM_DIR}/src/command.c
    ${BM_DIR}/src/target/spi.c
    ${BM_DIR}/src/target/cortex.c
    ${BM_DIR}/src/target/cortexar.c
    ${BM_DIR}/src/target/cortexm.c
    ${BM_DIR}/src/target/semihosting.c
    ${BM_DIR}/src/crc32.c
    ${BM_DIR}/src/target/efm32.c
    ${BM_DIR}/src/exception.c

    # ${BM_DIR}/src/platforms/hosted/gdb_if.c
    # ${BM_DIR}/src/gdb_hostio.c
    ${BM_DIR}/src/maths_utils.c
    ${BM_DIR}/src/gdb_main.c
    ${BM_DIR}/src/gdb_packet.c
    ${BM_DIR}/src/target/gdb_reg.c
    ${BM_DIR}/src/hex_utils.c
    ${BM_DIR}/src/target/jtag_devs.c
    ${BM_DIR}/src/target/jtag_scan.c
    ${BM_DIR}/src/target/lmi.c
    ${BM_DIR}/src/target/lpc_common.c
    ${BM_DIR}/src/target/lpc11xx.c
    ${BM_DIR}/src/target/lpc17xx.c
    ${BM_DIR}/src/target/lpc15xx.c
    ${BM_DIR}/src/target/lpc43xx.c
    ${BM_DIR}/src/target/lpc546xx.c
    ${BM_DIR}/src/target/kinetis.c
    ${BM_DIR}/src/main.c
    ${BM_DIR}/src/morse.c
    # ${BM_DIR}/src/target/msp432.c
    ${BM_DIR}/src/target/nrf51.c
    ${BM_DIR}/src/target/nxpke04.c

    # ${BM_DIR}/src/target/platform.c
    ${BM_DIR}/src/remote.c
    # ${BM_DIR}/src/target/rp.c
    ${BM_DIR}/src/target/sam3x.c
    ${BM_DIR}/src/target/sam4l.c
    ${BM_DIR}/src/target/samd.c
    ${BM_DIR}/src/target/samx5x.c
    ${BM_DIR}/src/target/sfdp.c
    ${BM_DIR}/src/target/stm32f1.c
    ${BM_DIR}/src/target/ch32f1.c
    ${BM_DIR}/src/target/stm32f4.c
    ${BM_DIR}/src/target/stm32h7.c
    ${BM_DIR}/src/target/stm32l0.c
    ${BM_DIR}/src/target/stm32l4.c
    ${BM_DIR}/src/target/stm32g0.c
    # ${BM_DIR}/src/target/renesas.c
    ${BM_DIR}/src/target/target.c
    ${BM_DIR}/src/target/target_flash.c
    ${BM_DIR}/src/target/target_probe.c
)

set(BM_INCLUDE
    ${BM_DIR}/src/include
    ${BM_DIR}/src/platforms/common
    ${BM_DIR}/src/target
    # ${BM_DIR}/libopencm3/include
    # ${PLATFORM_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/
)

# Get the current working branch
execute_process(
    COMMAND git describe --always --dirty --tags
    WORKING_DIRECTORY ${COMPONENT_DIR}/blackmagic-fw
    OUTPUT_VARIABLE BM_GIT_DESC
    OUTPUT_STRIP_TRAILING_WHITESPACE)

message(STATUS "BM version: ${BM_GIT_DESC}")

idf_component_register(SRCS ${BM_SOURCES} ${BM_TARGETS}
    INCLUDE_DIRS ${BM_INCLUDE} PRIV_REQUIRES esp_driver_gpio esp_timer)

target_compile_options(${COMPONENT_LIB} PRIVATE -DPC_HOSTED=0 -DFIRMWARE_VERSION="${BM_GIT_DESC}" -Wno-char-subscripts -Wno-attributes -std=gnu11)
