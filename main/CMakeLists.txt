# Create frontend dist directory if it doesn't exist
set(FRONTEND_DIR "${CMAKE_SOURCE_DIR}/frontend")
file(MAKE_DIRECTORY "${FRONTEND_DIR}/dist")

idf_component_register(SRCS
    "network-http-system.c"
    "network-http-settings.c"
    "network-http-target.c"
    "nvs-config.c"
    "network.c"
    "main.c"
    "network-gdb.c"
    "network-http.c"
    "network-rtt.c"
    "network-serial.c"
    "uart-gdb.c"
    "uart-rtt.c"
    "uart-serial.c"
    "nvs.c"
    "nvs-config.c"
    PRIV_REQUIRES esp_wifi esp_https_server nvs_flash esp_driver_uart esp_driver_gpio esp_driver_usb_serial_jtag
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/frontend/dist")

# Generate frontend header file
set(GENERATED_HEADER "${FRONTEND_DIR}/dist/network-http-page.h")
set(FRONTEND_SOURCES
    "${FRONTEND_DIR}/src/index.html"
    "${FRONTEND_DIR}/src/styles.css"
    "${FRONTEND_DIR}/src/app.js"
    "${FRONTEND_DIR}/build.js"
)

# Add custom command to build frontend
add_custom_command(
    OUTPUT ${GENERATED_HEADER}
    COMMAND ${CMAKE_COMMAND} -E chdir ${FRONTEND_DIR} npm install
    COMMAND ${CMAKE_COMMAND} -E chdir ${FRONTEND_DIR} npm run build
    DEPENDS ${FRONTEND_SOURCES}
    COMMENT "Installing npm dependencies and building frontend"
    VERBATIM
)

# Add custom target to ensure header is generated
add_custom_target(generate_frontend_header DEPENDS ${GENERATED_HEADER})

# Ensure frontend header is generated before building this component
add_dependencies(${COMPONENT_LIB} generate_frontend_header)
