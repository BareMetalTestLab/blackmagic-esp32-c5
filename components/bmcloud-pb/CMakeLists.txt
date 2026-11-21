# nanopb sources generated from the bmcloud-proto submodule at build time;
# neither the .pb.c nor the .pb.h are committed (see bmcloud-proto/README.md).
set(PROTO_DIR ${CMAKE_SOURCE_DIR}/components/bmcloud-proto)
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)

# The requirements-discovery pass includes this file in scriptable mode where
# only idf_component_register may execute: keep everything else in the real pass
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c
            "import google.protobuf, grpc_tools.protoc"
        RESULT_VARIABLE NANOPB_PYTHON_DEPS_RESULT
        OUTPUT_QUIET
        ERROR_QUIET)

    if(NOT NANOPB_PYTHON_DEPS_RESULT EQUAL 0)
        message(STATUS "Installing Nanopb Python dependencies with ESP-IDF Python")
        execute_process(
            COMMAND ${Python3_EXECUTABLE} -m pip install --disable-pip-version-check protobuf grpcio-tools
            RESULT_VARIABLE NANOPB_PIP_RESULT
            ERROR_VARIABLE NANOPB_PIP_ERROR)

        if(NOT NANOPB_PIP_RESULT EQUAL 0)
            message(FATAL_ERROR
                "Failed to install Nanopb Python dependencies: ${NANOPB_PIP_ERROR}")
        endif()

        execute_process(
            COMMAND ${Python3_EXECUTABLE} -c
                "import google.protobuf, grpc_tools.protoc"
            RESULT_VARIABLE NANOPB_PYTHON_DEPS_RESULT
            OUTPUT_QUIET
            ERROR_QUIET)

        if(NOT NANOPB_PYTHON_DEPS_RESULT EQUAL 0)
            message(FATAL_ERROR
                "Nanopb Python dependencies were installed but could not be imported")
        endif()
    endif()

    set(GENERATED
        ${GEN_DIR}/bmcloud/device.pb.c
        ${GEN_DIR}/bmcloud/device.pb.h
        ${GEN_DIR}/bmcloud/streams.pb.c
        ${GEN_DIR}/bmcloud/streams.pb.h)

    add_custom_command(
        OUTPUT ${GENERATED}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/third_party/nanopb/generator/nanopb_generator.py
                -I ${PROTO_DIR}/proto
                -D ${GEN_DIR}
                --options-file ${PROTO_DIR}/nanopb.options
                ${PROTO_DIR}/proto/bmcloud/device.proto
                ${PROTO_DIR}/proto/bmcloud/streams.proto
        DEPENDS
            ${PROTO_DIR}/proto/bmcloud/device.proto
            ${PROTO_DIR}/proto/bmcloud/streams.proto
            ${PROTO_DIR}/nanopb.options
        COMMENT "nanopb: generating bmcloud protocol sources")
endif()

idf_component_register(
    SRCS
        ${GEN_DIR}/bmcloud/device.pb.c
        ${GEN_DIR}/bmcloud/streams.pb.c
    REQUIRES nanopb)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    target_include_directories(${COMPONENT_LIB} PUBLIC ${GEN_DIR})
endif()
