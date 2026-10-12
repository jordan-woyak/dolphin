set(NO_SERVER ON)
set(NO_TESTS ON)

FetchContent_Declare(
    libjuice
    GIT_REPOSITORY https://github.com/paullouisageneau/libjuice.git
    GIT_TAG b89c792e3612faf2f12cf35bcc56857313a06be3 # v1.7.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libjuice)
