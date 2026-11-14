# LugaruEX build id: a hash of every engine and shared source file (line endings ignored),
# so the same source gives the same id on Windows, Linux and in the public source export,
# and any change gives another. Online, a guest whose build id differs from the host's
# can't join. Run as: cmake -DROOT=<repo> -DOUT=<header> -P BuildId.cmake
file(GLOB_RECURSE files
    "${ROOT}/lugaru/Source/*.cpp" "${ROOT}/lugaru/Source/*.hpp" "${ROOT}/lugaru/Source/*.h"
    "${ROOT}/shared/*.cpp" "${ROOT}/shared/*.h")
# (Version.hpp is made by the build from git: it differs from machine to machine)
list(FILTER files EXCLUDE REGEX "/Version\\.hpp$")
list(SORT files)
set(all "")
foreach(f IN LISTS files)
    file(READ "${f}" text)
    string(REPLACE "\r" "" text "${text}")
    string(SHA256 h "${text}")
    file(RELATIVE_PATH rel "${ROOT}" "${f}")
    string(APPEND all "${rel}:${h}\n")
endforeach()
string(SHA256 id "${all}")
string(SUBSTRING "${id}" 0 16 id)
set(content "#define EX_BUILD_ID \"${id}\"\n")
set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL content)
    file(WRITE "${OUT}" "${content}")
endif()
