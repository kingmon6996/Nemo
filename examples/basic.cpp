#include <nemo/nemo.hpp>
#include <iostream>

int main() {
    std::cout << "Nemo Version: " << nemo::version() << "\n";

    auto result = nemo::Database::open("example.nm");

    if (!result) {
        std::cerr << "Expected failure (Module 1): " << nemo::to_string(result.error()) << "\n";
        return 0; // Returning 0 since this failure is expected for Module 1
    }

    auto db = std::move(result.value());
    db.close();

    return 0;
}
