# Automatically generated by scripts/boost/generate-ports.ps1

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO boostorg/ratio
    REF boost-1.77.0
    SHA512 aa575893c00eac177cef6a5e8817747a457bae1ef83cd32b81d950b9f13cdc1b744e0f8336773361727f4cc374d4731e973aa3f33f19ef53830f2b0818bc8b0d
    HEAD_REF master
)

include(${CURRENT_INSTALLED_DIR}/share/boost-vcpkg-helpers/boost-modular-headers.cmake)
boost_modular_headers(SOURCE_PATH ${SOURCE_PATH})