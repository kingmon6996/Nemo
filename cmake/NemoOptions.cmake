# CMake Options for Nemo

option(NEMO_BUILD_TESTS "Build Nemo tests" ON)
option(NEMO_BUILD_EXAMPLES "Build Nemo examples" ON)
option(NEMO_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(NEMO_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

# Function to add strict compiler flags to a target
function(nemo_enable_strict_warnings target_name)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wnull-dereference
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${target_name} PRIVATE
            /W4
            /permissive-
        )
    endif()
endfunction()

# Function to add sanitizers
function(nemo_enable_sanitizers target_name)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        if(NEMO_ENABLE_ASAN)
            target_compile_options(${target_name} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
            target_link_options(${target_name} PRIVATE -fsanitize=address)
        endif()
        if(NEMO_ENABLE_UBSAN)
            target_compile_options(${target_name} PRIVATE -fsanitize=undefined)
            target_link_options(${target_name} PRIVATE -fsanitize=undefined)
        endif()
    endif()
endfunction()
