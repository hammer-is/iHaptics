cmake_minimum_required(VERSION 3.24)
project(iHaptics LANGUAGES CXX)

add_executable(iHaptics
    iHaptics.rc
    main.cpp
    HPR.cpp
    irsdk/irsdk_client.cpp
    irsdk/irsdk_utils.cpp
    irsdk/yaml_parser.cpp
)

target_compile_features(iHaptics
    PUBLIC
        cxx_std_20
)

target_include_directories(iHaptics
    PUBLIC
)

target_compile_definitions(iHaptics
    PUBLIC
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
        PICOJSON_USE_RVALUE_REFERENCE=0
)


target_link_libraries(iHaptics
    PUBLIC
        version.lib
        hid.lib
)

target_compile_options(iHaptics
    PUBLIC
)