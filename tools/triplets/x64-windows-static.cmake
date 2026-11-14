# LugaruEX: vcpkg's x64-windows-static triplet, plus /d1trimfile so the libraries' source
# file names (asserts, GL error messages in SDL) don't carry the folder vcpkg was built in.
# Used with: vcpkg install --overlay-triplets=tools\triplets --triplet x64-windows-static ...
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PROVIDED_FORTRAN ON)

# (forward slashes: a trailing backslash would escape what follows on vcpkg's command line;
# the compiler matches the prefix either way)
if(DEFINED VCPKG_ROOT_DIR)
    file(TO_CMAKE_PATH "${VCPKG_ROOT_DIR}/" _lugaruex_vcpkg_root)
    set(VCPKG_C_FLAGS "/d1trimfile:${_lugaruex_vcpkg_root}")
    set(VCPKG_CXX_FLAGS "/d1trimfile:${_lugaruex_vcpkg_root}")
endif()
