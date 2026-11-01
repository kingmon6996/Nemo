#include "nemo/core/version.hpp"

namespace nemo {

std::string_view version() noexcept {
    // Note: To make this constexpr and string_view returning, we could use
    // some template metaprogramming to convert ints to chars at compile time,
    // or just hardcode the string here and ensure they match.
    // Given the constraints, a static string literal is the cleanest approach.
    return "0.2.2";
}

} // namespace nemo
