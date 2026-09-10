################################
# libretro renderer example
################################

if(BUILD_LIBRETRO)
    set(LIBRETRO_DIR ${TIC80CORE_DIR}/system/libretro)
    set(LIBRETRO_SRC
        ${LIBRETRO_DIR}/tic80_libretro.c
    )

    if(LIBRETRO_STATIC AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
        if(NOT BUILD_STATIC)
            message(FATAL_ERROR "An iOS static libretro core requires BUILD_STATIC=ON")
        endif()

        add_library(tic80_libretro STATIC ${LIBRETRO_SRC})
        target_link_libraries(tic80_libretro PRIVATE tic80core)

        # Merge compiled archives, preserving each dependency's language,
        # compile options and generated sources. Recompiling a flattened source
        # list loses these properties (and misses runtime adapter targets).
        set(ios_core_pending tic80core)
        set(ios_core_visited "")
        set(ios_core_archives "")
        while(ios_core_pending)
            list(GET ios_core_pending 0 dep)
            list(REMOVE_AT ios_core_pending 0)
            if(dep IN_LIST ios_core_visited)
                continue()
            endif()
            list(APPEND ios_core_visited "${dep}")
            if(TARGET "${dep}")
                get_target_property(dep_type "${dep}" TYPE)
                if(dep_type STREQUAL "STATIC_LIBRARY" OR dep_type STREQUAL "UNKNOWN_LIBRARY")
                    list(APPEND ios_core_archives "$<TARGET_FILE:${dep}>")
                elseif(dep_type STREQUAL "SHARED_LIBRARY")
                    message(FATAL_ERROR "iOS static libretro dependency ${dep} must be static")
                endif()
                get_target_property(dep_links "${dep}" LINK_LIBRARIES)
                if(dep_links)
                    list(APPEND ios_core_pending ${dep_links})
                endif()
            elseif(IS_ABSOLUTE "${dep}" AND dep MATCHES "\\.a$")
                list(APPEND ios_core_archives "${dep}")
            endif()
        endwhile()

        # Static target link dependencies are only ordering edges. A generated
        # empty translation unit makes archive changes trigger a new merge too.
        set(ios_core_stamp "${CMAKE_CURRENT_BINARY_DIR}/libretro_ios_dependencies.c")
        add_custom_command(OUTPUT "${ios_core_stamp}"
            COMMAND ${CMAKE_COMMAND} -E touch "${ios_core_stamp}"
            DEPENDS ${ios_core_archives}
            VERBATIM
        )
        target_sources(tic80_libretro PRIVATE "${ios_core_stamp}")

        find_program(TIC80_APPLE_LIBTOOL libtool REQUIRED)
        add_custom_command(TARGET tic80_libretro POST_BUILD
            COMMAND "${TIC80_APPLE_LIBTOOL}" -static -o "$<TARGET_FILE:tic80_libretro>.merged"
                "$<TARGET_FILE:tic80_libretro>" ${ios_core_archives}
            COMMAND ${CMAKE_COMMAND} -E rename "$<TARGET_FILE:tic80_libretro>.merged" "$<TARGET_FILE:tic80_libretro>"
            VERBATIM
        )
        set_target_properties(tic80_libretro PROPERTIES SUFFIX "${LIBRETRO_SUFFIX}.a")
    elseif (LIBRETRO_STATIC)
        # Collect sources from all dependencies to build a monolithic library.
        # This ensures that consoles and Emscripten get a single, complete archive.
        set(TIC80_MONOLITHIC_SRCS ${LIBRETRO_SRC})
        
        # List of all potential core component targets to collect sources from
        set(TIC80_CORE_OBJECT_DEPS 
            tic80core luaapi lua lpeg moon yue fennel wren squirrel pocketpy 
            quickjs mruby wasm scheme janet zip zlib png blipbuf giflib 
            wave argparse naett
        )

        set(TIC80_MONOLITHIC_INCLUDES "")
        set(TIC80_MONOLITHIC_DEFINES "")

        foreach(target ${TIC80_CORE_OBJECT_DEPS})
            if(TARGET ${target})
                get_target_property(type ${target} TYPE)
                get_target_property(imported ${target} IMPORTED)

                if(NOT type STREQUAL "INTERFACE_LIBRARY" AND NOT imported)
                    # Collect sources
                    get_target_property(target_sources ${target} SOURCES)
                    get_target_property(target_dir ${target} SOURCE_DIR)
                    if(target_sources)
                        foreach(src ${target_sources})
                            if(NOT IS_ABSOLUTE "${src}")
                                set(src "${target_dir}/${src}")
                            endif()
                            if(EXISTS "${src}")
                                list(APPEND TIC80_MONOLITHIC_SRCS "${src}")
                            endif()
                        endforeach()
                    endif()

                    # Collect Private include directories
                    get_target_property(includes ${target} INCLUDE_DIRECTORIES)
                    if(includes)
                        list(APPEND TIC80_MONOLITHIC_INCLUDES ${includes})
                    endif()

                    # Collect Private compile definitions
                    get_target_property(defines ${target} COMPILE_DEFINITIONS)
                    if(defines)
                        list(APPEND TIC80_MONOLITHIC_DEFINES ${defines})
                    endif()

                    # Collect Directory compile definitions if possible
                    # Only if target_dir is within project source tree to avoid errors
                    if(target_dir AND "${target_dir}" MATCHES "^${CMAKE_SOURCE_DIR}")
                        get_directory_property(dir_defines DIRECTORY ${target_dir} COMPILE_DEFINITIONS)
                        if(dir_defines)
                            list(APPEND TIC80_MONOLITHIC_DEFINES ${dir_defines})
                        endif()
                    endif()
                endif()

                # Collect Interface properties (available for all types, but we must use the INTERFACE_ variant)
                get_target_property(iface_includes ${target} INTERFACE_INCLUDE_DIRECTORIES)
                if(iface_includes)
                    list(APPEND TIC80_MONOLITHIC_INCLUDES ${iface_includes})
                endif()

                get_target_property(iface_defines ${target} INTERFACE_COMPILE_DEFINITIONS)
                if(iface_defines)
                    list(APPEND TIC80_MONOLITHIC_DEFINES ${iface_defines})
                endif()
            endif()
        endforeach()

        # Remove duplicates from includes/defines to keep command lines manageable
        if(TIC80_MONOLITHIC_INCLUDES)
            list(REMOVE_DUPLICATES TIC80_MONOLITHIC_INCLUDES)
        endif()
        if(TIC80_MONOLITHIC_DEFINES)
            list(REMOVE_DUPLICATES TIC80_MONOLITHIC_DEFINES)
        endif()

        add_library(tic80_libretro STATIC
            ${TIC80_MONOLITHIC_SRCS}
        )

        target_include_directories(tic80_libretro PRIVATE ${TIC80_MONOLITHIC_INCLUDES})
        target_compile_definitions(tic80_libretro PRIVATE ${TIC80_MONOLITHIC_DEFINES})

        if(EMSCRIPTEN)
            set(LIBRETRO_EXTENSION "bc")
        else()
            set(LIBRETRO_EXTENSION "a")
        endif()

        set_target_properties(tic80_libretro PROPERTIES SUFFIX "${LIBRETRO_SUFFIX}.${LIBRETRO_EXTENSION}")
    else()
        add_library(tic80_libretro SHARED
            ${LIBRETRO_SRC}
        )
    endif()

    target_include_directories(tic80_libretro PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${TIC80CORE_DIR}
    )

    if(MINGW)
        target_link_libraries(tic80_libretro mingw32)
    endif()

    if(ANDROID)
        set_target_properties(tic80_libretro PROPERTIES SUFFIX "_android.so")
    endif()

    # MSYS2 builds libretro to ./bin, despite it being a DLL. This forces it to ./lib.
    set_target_properties(tic80_libretro PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )

    target_compile_definitions(tic80_libretro PRIVATE
        __LIBRETRO__=TRUE
    )
    if(NOT LIBRETRO_STATIC)
        target_link_libraries(tic80_libretro tic80core)
    endif()
    set_target_properties(tic80_libretro PROPERTIES PREFIX "")
endif()
