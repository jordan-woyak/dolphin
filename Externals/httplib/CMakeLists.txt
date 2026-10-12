FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG cf3693cb5cc0d39b0e6f4122ba89bda9edb5ac6b # v0.59.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(httplib)
