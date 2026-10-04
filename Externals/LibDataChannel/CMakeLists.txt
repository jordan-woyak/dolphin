set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(USE_OPENSSL OFF CACHE BOOL "" FORCE)

set(PREFER_SYSTEM_LIB OFF CACHE BOOL "" FORCE)
set(USE_SYSTEM_SRTP OFF CACHE BOOL "" FORCE)
set(USE_SYSTEM_JUICE OFF CACHE BOOL "" FORCE)
set(USE_SYSTEM_USRSCTP OFF CACHE BOOL "" FORCE)
set(USE_SYSTEM_PLOG OFF CACHE BOOL "" FORCE)
set(USE_SYSTEM_JSON OFF CACHE BOOL "" FORCE)

set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
set(NO_MEDIA OFF CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)

# FetchContent_Declare(
#   openssl
#   GIT_REPOSITORY https://github.com/openssl/openssl.git
#   # openssl-4.0.3
#   GIT_TAG af1775b60dfa141a4ad762585052cabeb9f37e9e
#   GIT_SHALLOW TRUE
# )
# FetchContent_MakeAvailable(openssl)

FetchContent_Declare(
  libdatachannel
  GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
  # v0.24.6
  GIT_TAG 6b1e2e620f1e37f0eafeee702eaea0043cb305fd
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libdatachannel)

if(NOT MSVC)
  target_compile_options(datachannel PRIVATE -fexceptions)
endif()
