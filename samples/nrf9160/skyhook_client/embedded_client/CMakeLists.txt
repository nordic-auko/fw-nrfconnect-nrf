

# Check system for protoc and nanopb binaries presence, needed to build the gadget profile files.
# Prefer local variable to make version matching easier
find_program(PROTOC_BIN
    protoc
    HINTS
    "${CMAKE_SOURCE_DIR}/*/generator-bin/"
    "${ZEPHYR_BASE}/../*/generator-bin/"
    "${ZEPHYR_BASE}/../tools/*/generator-bin/"
)
find_program(NANOPB_BIN
    protoc-gen-nanopb
    HINTS
    "${CMAKE_SOURCE_DIR}/*/generator-bin/"
    "${ZEPHYR_BASE}/../*/generator-bin/"
    "${ZEPHYR_BASE}/../tools/*/generator-bin/"
)

if ((NOT NANOPB_BIN) OR (NOT PROTOC_BIN))
    message(STATUS "nanopb not found.")
    message(STATUS "Please download platform-specific nanopb from here https://jpa.kapsi.fi/nanopb/download/")
    message(STATUS "Extract the files to `${CMAKE_SOURCE_DIR}`, or add `<path-to>/nanopb-x.y.z-<platform>/generator-bin` to system PATH.")
    message(STATUS "Alternatively, specify install location using: `-DCMAKE_PROGRAM_PATH=<path-to>/nanopb-x.y.z-<platform>/generator-bin` when running CMake.")
    message(FATAL_ERROR "nanopb required by this project.")
endif()

if(EMBEDDED_CLIENT_PATH)
  # Do nothing, just use the provided path
elseif(WEST)
  ## Use Python and west to find the skyhook path
  execute_process(
    COMMAND ${PYTHON_EXECUTABLE} -c
     "from west.manifest import Manifest; manifest = Manifest.from_file(); print(manifest.get_projects(['embedded-client'])[0].posixpath)"
    OUTPUT_VARIABLE
    west_project_output
    RESULT_VARIABLE
    result
  )
  string(REGEX REPLACE "[\r\n]+" "" EMBEDDED_CLIENT_PATH "${west_project_output}")
  if(${result})
    message(FATAL_ERROR "Failed to find skyhook source files, cannot build sample")
  endif()
else()
  message(FATAL_ERROR "west not installed, please provide EMBEDDED_CLIENT_PATH to CMake to build this sample")
endif()

if(EMBEDDED_PROTOCOL_PATH)
  # Do nothing, just use the provided path
elseif(WEST)
  ## Use Python and west to find the skyhook path
  execute_process(
    COMMAND ${PYTHON_EXECUTABLE} -c
     "from west.manifest import Manifest; manifest = Manifest.from_file(); print(manifest.get_projects(['embedded-protocol'])[0].posixpath)"
    OUTPUT_VARIABLE
    west_project_output
    RESULT_VARIABLE
    result
  )
  string(REGEX REPLACE "[\r\n]+" "" EMBEDDED_PROTOCOL_PATH "${west_project_output}")
  if(${result})
    message(FATAL_ERROR "Failed to find skyhook source files, cannot build sample")
  endif()
else()
  message(FATAL_ERROR "west not installed, please provide EMBEDDED_PROTOCOL_PATH to CMake to build this sample")
endif()

if(TINY_AES_C_PATH)
  # Do nothing, just use the provided path
elseif(WEST)
  ## Use Python and west to find the skyhook path
  execute_process(
    COMMAND ${PYTHON_EXECUTABLE} -c
     "from west.manifest import Manifest; manifest = Manifest.from_file(); print(manifest.get_projects(['tiny-AES-c'])[0].posixpath)"
    OUTPUT_VARIABLE
    west_project_output
    RESULT_VARIABLE
    result
  )
  string(REGEX REPLACE "[\r\n]+" "" TINY_AES_C_PATH "${west_project_output}")
  if(${result})
    message(FATAL_ERROR "Failed to find skyhook source files, cannot build sample")
  endif()
else()
  message(FATAL_ERROR "west not installed, please provide TINY_AES_C_PATH to CMake to build this sample")
endif()

if(NANOPB_PATH)
  # Do nothing, just use the provided path
elseif(WEST)
  ## Use Python and west to find the nanopb path
  execute_process(
    COMMAND ${PYTHON_EXECUTABLE} -c
     "from west.manifest import Manifest; manifest = Manifest.from_file(); print(manifest.get_projects(['nanopb'])[0].posixpath)"
    OUTPUT_VARIABLE
    west_project_output
    RESULT_VARIABLE
    result
  )
  string(REGEX REPLACE "[\r\n]+" "" NANOPB_PATH "${west_project_output}")
  if(${result})
    message(FATAL_ERROR "Failed to find nanopb source files, cannot build sample")
  endif()
else()
  message(FATAL_ERROR "west not installed, please provide NANOPB_PATH to CMake to build this sample")
endif()

message(STATUS "Skyhook lib path: ${EMBEDDED_CLIENT_PATH}")

set(PROTO_COMPILATION_DIR ${CMAKE_BINARY_DIR}/generated/skyhook/protobuf)

set(PROTO_FILES
    ${EMBEDDED_PROTOCOL_PATH}/el.proto
)

set(PROTO_OPTION_FILES
    ${EMBEDDED_CLIENT_PATH}/libel/protocol/el.options
)

foreach(file ${PROTO_FILES})
    get_filename_component(FILE_DIR ${file} DIRECTORY)
    get_filename_component(FILE_BASE_NAME ${file} NAME_WE)
    list(APPEND PROTOC_INC_DIRS "-I.")
    list(APPEND PROTOC_INC_DIRS "-I${FILE_DIR}")
    list(APPEND PROTO_OUTPUTS "${PROTO_COMPILATION_DIR}/${FILE_BASE_NAME}.pb.c")
    list(APPEND PROTO_DEPENDENCIES "${file}")
endforeach()

foreach(file ${PROTO_OPTION_FILES})
    get_filename_component(FILE_DIR ${file} DIRECTORY)
    list(APPEND PROTOC_OPT_DIRS "-I${FILE_DIR}")
endforeach()

list(APPEND PROTOC_OPTS "--nanopb_opt=${PROTOC_OPT_DIRS}")

# Each .pb.c file to be compiled by protoc gets its own CMake custom command,
# with dependencies to all other .proto files in its group and the base files.
foreach(file ${PROTO_FILES})
    get_filename_component(FILE_DIR ${file} DIRECTORY)
    get_filename_component(FILE_NAME ${file} NAME)
    get_filename_component(FILE_BASE_NAME ${file} NAME_WE)

    set(PROTOC_ARGS "${PROTOC_INC_DIRS}"
                    "${PROTOC_OPTS}"
                    "--nanopb_out=${PROTO_COMPILATION_DIR}"
                    "${FILE_NAME}"
    )

    message(STATUS "PROTOC_ARGS: ${PROTOC_ARGS}")

    add_custom_command(
      OUTPUT "${PROTO_COMPILATION_DIR}/${FILE_BASE_NAME}.pb.c"
      COMMAND ${PROTOC_BIN} ${PROTOC_ARGS}
      WORKING_DIRECTORY ${FILE_DIR}
      MAIN_DEPENDENCY ${file}
    )
endforeach()

zephyr_library_named(embedded_client_lib)

zephyr_include_directories(
    ${NANOPB_PATH}
    ${TINY_AES_C_PATH}
    ${EMBEDDED_CLIENT_PATH}/libel
    ${EMBEDDED_CLIENT_PATH}/libel/protocol
)

zephyr_library_sources_ifdef(CONFIG_SKYHOOK_EMBEDDED_CLIENT
    ${NANOPB_PATH}/pb_common.c
    ${NANOPB_PATH}/pb_encode.c
    ${NANOPB_PATH}/pb_decode.c
    ${TINY_AES_C_PATH}/aes.c
    ${EMBEDDED_CLIENT_PATH}/libel/libel.c
    ${EMBEDDED_CLIENT_PATH}/libel/utilities.c
    ${EMBEDDED_CLIENT_PATH}/libel/beacons.c
    ${EMBEDDED_CLIENT_PATH}/libel/crc32.c
    ${EMBEDDED_CLIENT_PATH}/libel/protocol/proto.c
    ${PROTO_OUTPUTS}
)

zephyr_library_compile_definitions(VERSION="TODO") 

#
# Give the main application access to the generated proto headers
#
# NOTE: This library corresponds to APP_LINK_WITH_EMBEDDED_CLIENT_PROTO in Kconfig
#       which is default set to "true".
#
zephyr_interface_library_named(skyhook_embedded_client)
target_link_libraries(skyhook_embedded_client INTERFACE embedded_client_lib)
