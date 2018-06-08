# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

include_directories(../../../../)

add_definitions(-DDAEMONLIB_WITH_LOGGING)

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

add_library( brickd-android

             SHARED

             ../../../../daemonlib/array.c
             ../../../../daemonlib/base58.c
             ../../../../daemonlib/config.c
             ../../../../daemonlib/conf_file.c
             ../../../../daemonlib/enum.c
             ../../../../daemonlib/event.c
             ../../../../daemonlib/event_posix.c
             ../../../../daemonlib/io.c
             ../../../../daemonlib/log.c
             ../../../../daemonlib/node.c
             ../../../../daemonlib/packet.c
             ../../../../daemonlib/pipe_posix.c
             ../../../../daemonlib/queue.c
             ../../../../daemonlib/socket.c
             ../../../../daemonlib/socket_posix.c
             ../../../../daemonlib/threads_posix.c
             ../../../../daemonlib/timer_posix.c
             ../../../../daemonlib/utils.c
             ../../../../daemonlib/writer.c

             ../../../../brickd/base64.c
             ../../../../brickd/client.c
             ../../../../brickd/config_options.c
             ../../../../brickd/hardware.c
             ../../../../brickd/hmac.c
             ../../../../brickd/log_android.c
             ../../../../brickd/mesh.c
             ../../../../brickd/mesh_stack.c
             ../../../../brickd/network.c
             ../../../../brickd/sha1.c
             ../../../../brickd/stack.c
             ../../../../brickd/websocket.c
             ../../../../brickd/zombie.c

             ../../../../brickd/main_android.cpp )

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.

find_library( # Sets the name of the path variable.
              log-lib

              # Specifies the name of the NDK library that
              # you want CMake to locate.
              log )

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
                       brickd-android

                       # Links the target library to the log library
                       # included in the NDK.
                       ${log-lib} )