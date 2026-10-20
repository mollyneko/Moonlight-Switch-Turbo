set(_mbedtls_search_roots)
if (DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND _mbedtls_search_roots "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()
foreach(_mbedtls_root_var MbedTLS_ROOT mbedTLS_ROOT MBEDTLS_ROOT)
    if (DEFINED ${_mbedtls_root_var} AND NOT "${${_mbedtls_root_var}}" STREQUAL "")
        list(APPEND _mbedtls_search_roots "${${_mbedtls_root_var}}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _mbedtls_search_roots)

if (_mbedtls_search_roots)
    find_path(MBEDTLS_INCLUDE_DIRS mbedtls/version.h
            HINTS ${_mbedtls_search_roots}
            PATH_SUFFIXES include
            NO_DEFAULT_PATH)
endif()
if (NOT MBEDTLS_INCLUDE_DIRS)
    find_path(MBEDTLS_INCLUDE_DIRS mbedtls/version.h PATH_SUFFIXES include)
endif()

if (MBEDTLS_INCLUDE_DIRS AND EXISTS ${MBEDTLS_INCLUDE_DIRS}/mbedtls/version.h)
    file(STRINGS "${MBEDTLS_INCLUDE_DIRS}/mbedtls/version.h" MBEDTLS_VERSION_STRING_LINE REGEX "^#define[ \t]+MBEDTLS_VERSION_STRING[ \t]+\"[0-9.]+\"$")
    string(REGEX REPLACE "^#define[ \t]+MBEDTLS_VERSION_STRING[ \t]+\"([0-9.]+)\"$" "\\1" MBEDTLS_VERSION_STRING "${MBEDTLS_VERSION_STRING_LINE}")
    unset(MBEDTLS_VERSION_STRING_LINE)
endif()

if (_mbedtls_search_roots)
    find_library(MBEDTLS_LIBRARY mbedtls
            HINTS ${_mbedtls_search_roots}
            PATH_SUFFIXES lib
            NO_DEFAULT_PATH)
    find_library(MBEDX509_LIBRARY mbedx509
            HINTS ${_mbedtls_search_roots}
            PATH_SUFFIXES lib
            NO_DEFAULT_PATH)
    find_library(MBEDCRYPTO_LIBRARY mbedcrypto
            HINTS ${_mbedtls_search_roots}
            PATH_SUFFIXES lib
            NO_DEFAULT_PATH)
endif()

if (NOT MBEDTLS_LIBRARY)
    find_library(MBEDTLS_LIBRARY mbedtls PATH_SUFFIXES lib)
endif()
if (NOT MBEDX509_LIBRARY)
    find_library(MBEDX509_LIBRARY mbedx509 PATH_SUFFIXES lib)
endif()
if (NOT MBEDCRYPTO_LIBRARY)
    find_library(MBEDCRYPTO_LIBRARY mbedcrypto PATH_SUFFIXES lib)
endif()

set(MBEDTLS_LIBRARIES "${MBEDTLS_LIBRARY}" "${MBEDX509_LIBRARY}" "${MBEDCRYPTO_LIBRARY}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MbedTLS
    REQUIRED_VARS MBEDTLS_LIBRARIES MBEDTLS_INCLUDE_DIRS
    VERSION_VAR MBEDTLS_VERSION_STRING
)

mark_as_advanced(MBEDTLS_INCLUDE_DIRS MBEDTLS_LIBRARY MBEDX509_LIBRARY MBEDCRYPTO_LIBRARY)
