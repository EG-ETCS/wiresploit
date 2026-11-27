cmake_minimum_required(VERSION 3.16)
project(pi5la C)

# piolib headers use GNU statement expressions, so compile as gnu11.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# Host-only mode builds just the hardware-independent unit tests
# (unpack, srzip, analyze, program builder, parsing) on any Linux machine.
option(PI5LA_TESTS_ONLY "Build only the host unit tests (no piolib / pioasm)" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)

add_library(pi5la_core STATIC
    src/analyze.c
    src/burst.c
    src/idle.c
    src/srwriter.c
    src/progbuild.c
    src/srzip.c
    src/unpack.c
    src/util.c
)
target_include_directories(pi5la_core PUBLIC src)
target_link_libraries(pi5la_core PUBLIC PkgConfig::LIBZIP m)

if(NOT PI5LA_TESTS_ONLY)
    # piolib: installed from raspberrypi/utils (see README).
    find_path(PIOLIB_INCLUDE_DIR piolib.h PATH_SUFFIXES piolib)
    find_library(PIOLIB_LIBRARY NAMES pio)
    if(NOT PIOLIB_INCLUDE_DIR OR NOT PIOLIB_LIBRARY)
        message(FATAL_ERROR "piolib not found: install it from raspberrypi/utils (see README), "
                            "or configure with -DPI5LA_TESTS_ONLY=ON")
    endif()
    find_package(Threads REQUIRED)

    # pioasm: built from pico-sdk/tools/pioasm (see README).
    find_program(PIOASM_EXECUTABLE pioasm)
    if(NOT PIOASM_EXECUTABLE)
        message(FATAL_ERROR "pioasm not found: build it from pico-sdk/tools/pioasm (see README) "
                            "or pass -DPIOASM_EXECUTABLE=/path/to/pioasm")
    endif()

    set(PI5LA_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${PI5LA_GEN_DIR})
    set(PI5LA_PIO_HEADERS)
    foreach(prog la8 la8_trig sqwave sqwave_slow)
        set(in ${CMAKE_CURRENT_SOURCE_DIR}/pio/${prog}.pio)
        set(out ${PI5LA_GEN_DIR}/${prog}.pio.h)
        add_custom_command(
            OUTPUT ${out}
            COMMAND ${PIOASM_EXECUTABLE} -o c-sdk ${in} ${out}
            DEPENDS ${in}
            COMMENT "pioasm ${prog}.pio"
            VERBATIM)
        list(APPEND PI5LA_PIO_HEADERS ${out})
    endforeach()
    add_custom_target(pi5la_pio_headers DEPENDS ${PI5LA_PIO_HEADERS})

    add_library(pi5la_hw STATIC
        src/capture.c
        src/dmacyclic.c
        src/record.c
        src/shipper.c
        src/fdebug.c
        src/generator.c
    )
    add_dependencies(pi5la_hw pi5la_pio_headers)
    target_include_directories(pi5la_hw PUBLIC src ${PIOLIB_INCLUDE_DIR} ${PI5LA_GEN_DIR})
    target_link_libraries(pi5la_hw PUBLIC pi5la_core ${PIOLIB_LIBRARY} Threads::Threads)

    add_executable(pi5la src/main.c)
    target_link_libraries(pi5la PRIVATE pi5la_hw)

    add_executable(ratesweep tools/ratesweep.c)
    target_link_libraries(ratesweep PRIVATE pi5la_hw)

    add_executable(bitprobe tools/bitprobe.c)
    target_link_libraries(bitprobe PRIVATE pi5la_hw)

    add_executable(fdebugtest tools/fdebugtest.c)
    target_link_libraries(fdebugtest PRIVATE pi5la_hw)

    # Helper scripts, placed next to pi5la in the build directory. file(COPY)
    # sets the executable bit; the configure_file() calls exist only to make
    # CMake re-run (and so re-copy) when a script changes, since otherwise a
    # plain `cmake --build` would leave a stale copy behind.
    foreach(script tools/sr2es.py tools/uart-listen.sh)
        get_filename_component(script_name ${script} NAME)
        configure_file(${script} ${CMAKE_CURRENT_BINARY_DIR}/.stamp-${script_name} COPYONLY)
        file(COPY ${script} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
            FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
                             WORLD_READ WORLD_EXECUTE)
    endforeach()

    # record.c drives the streaming/monitor logic; the test stubs capture_stream,
    # so it compiles record.c directly instead of linking pi5la_hw.
    add_executable(test_record tests/test_record.c src/record.c src/shipper.c)
    target_include_directories(test_record PRIVATE ${PIOLIB_INCLUDE_DIR})
    target_link_libraries(test_record PRIVATE pi5la_core)
    add_test(NAME test_record COMMAND test_record)

    install(TARGETS pi5la ratesweep bitprobe fdebugtest RUNTIME DESTINATION bin)
    install(PROGRAMS tools/uart-listen.sh DESTINATION bin RENAME uart-listen)
    install(PROGRAMS tools/sr2es.py DESTINATION bin RENAME sr2es)
endif()

enable_testing()
foreach(t test_unpack test_srzip test_srwriter test_analyze test_progbuild test_util
          test_idle test_burst)
    add_executable(${t} tests/${t}.c)
    target_link_libraries(${t} PRIVATE pi5la_core)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# Python-side tests: web UI argument validation and the Elasticsearch exporter.
# Skipped when python3 is missing.
find_program(PI5LA_PYTHON3 python3)
if(PI5LA_PYTHON3)
    foreach(pt test_web_args test_sr2es)
        add_test(NAME ${pt}
            COMMAND ${PI5LA_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${pt}.py)
    endforeach()
endif()
