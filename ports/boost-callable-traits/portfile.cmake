# Automatically generated by scripts/boost/generate-ports.ps1

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO boostorg/callable_traits
    REF boost-1.77.0
    SHA512 ad0e266c219dba57048b4f3010a5740c7b6f914564123ba73c64b89952a2edf5b7e275d2bc4c8c456a7bc54abb736dc05ab4bc95b644f5ea724160671ebd95cf
    HEAD_REF master
)

include(${CURRENT_INSTALLED_DIR}/share/boost-vcpkg-helpers/boost-modular-headers.cmake)
boost_modular_headers(SOURCE_PATH ${SOURCE_PATH})