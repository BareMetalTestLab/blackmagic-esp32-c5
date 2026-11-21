# ESP-IDF wrapper around the third_party/nanopb submodule: upstream ships no
# IDF component of its own, so its runtime sources are registered here.
set(NANOPB_DIR ${CMAKE_SOURCE_DIR}/third_party/nanopb)

idf_component_register(
    SRCS
        ${NANOPB_DIR}/pb_common.c
        ${NANOPB_DIR}/pb_decode.c
        ${NANOPB_DIR}/pb_encode.c
)

target_include_directories(${COMPONENT_LIB} PUBLIC ${NANOPB_DIR})
