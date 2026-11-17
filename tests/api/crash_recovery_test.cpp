#include "nemo/api/connection.hpp"
#include "nemo/api/database.hpp"
#include "nemo/api/result.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#endif

namespace {

nemo::DatabaseConfig test_config() {
    nemo::DatabaseConfig config;
    config.buffer_pool_size = 32 * 1024 * 1024;
    return config;
}

int run_crash_child(const std::string& mode, const std::filesystem::path& path) {
    auto opened = nemo::Database::open(path.string(), test_config());
    if (!opened) return 10;
    auto database = std::move(opened.value());
    auto connected = database.connect({});
    if (!connected) return 11;
    auto connection = std::move(connected.value());

    if (mode == "committed") {
        auto result = connection.execute(
            "INSERT INTO crash_rows VALUES (1, 'committed')");
        if (!result) return 12;
    } else if (mode == "uncommitted") {
        auto transaction = connection.begin();
        if (!transaction) return 13;
        auto result = connection.execute(
            "INSERT INTO crash_rows VALUES (2, 'uncommitted')");
        if (!result) return 14;
    } else {
        return 15;
    }

    std::_Exit(0);
}

std::string shell_quote(const std::string& value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (const char character : value) {
        if (character == '"') quoted.push_back('\\');
        quoted.push_back(character);
    }
    quoted.push_back('"');
    return quoted;
#else
    std::string quoted = "'";
    for (const char character : value) {
        if (character == '\'') quoted += "'\\''";
        else quoted.push_back(character);
    }
    quoted.push_back('\'');
    return quoted;
#endif
}

bool run_child(const std::filesystem::path& executable, const std::string& mode,
               const std::filesystem::path& path) {
#ifdef _WIN32
    const auto quote_argument = [](const std::wstring& argument) {
        std::wstring quoted = L"\"";
        std::size_t backslashes = 0;
        for (const wchar_t character : argument) {
            if (character == L'\\') {
                ++backslashes;
                continue;
            }
            if (character == L'"') {
                quoted.append(backslashes * 2 + 1, L'\\');
                quoted.push_back(L'"');
                backslashes = 0;
                continue;
            }
            quoted.append(backslashes, L'\\');
            quoted.push_back(character);
            backslashes = 0;
        }
        quoted.append(backslashes * 2, L'\\');
        quoted.push_back(L'"');
        return quoted;
    };
    std::wstring command = quote_argument(executable.wstring()) + L" --child ";
    command += std::wstring(mode.begin(), mode.end());
    command += L" " + quote_argument(path.wstring());

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
            nullptr, nullptr, &startup, &process)) {
        return false;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code == 0;
#else
    const auto command = shell_quote(executable.string()) + " --child " + mode + " " +
        shell_quote(path.string());
    const int status = std::system(command.c_str());
    return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

bool verify_row(nemo::Database& database, std::int64_t id, bool expected) {
    auto connected = database.connect({});
    if (!connected) return false;
    auto connection = std::move(connected.value());
    const auto sql = "SELECT id FROM crash_rows WHERE id = " +
        std::to_string(id);
    auto result = connection.execute(sql);
    if (!result) return false;
    return (result.value().next() != nullptr) == expected;
}

struct DatabaseFilesCleanup {
    std::filesystem::path path;
    ~DatabaseFilesCleanup() {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove_all(path.string() + ".wal", error);
        std::filesystem::remove(path.string() + ".master.nm", error);
    }
};

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--child") {
        return run_crash_child(argv[2], argv[3]);
    }
    if (argc != 1) return 1;

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("nemo_process_crash_" + std::to_string(nonce) + ".nm");
    [[maybe_unused]] const DatabaseFilesCleanup cleanup{path};
    const auto executable = std::filesystem::absolute(argv[0]);

    {
        auto opened = nemo::Database::open(path.string(), test_config());
        if (!opened) return 2;
        auto database = std::move(opened.value());
        auto connected = database.connect({});
        if (!connected) return 3;
        auto connection = std::move(connected.value());
        if (!connection.execute(
                "CREATE TABLE crash_rows (id INTEGER, value TEXT)")) {
            return 4;
        }
        if (!database.close()) return 5;
    }
    if (!std::filesystem::exists(
            std::filesystem::path(path.string() + ".master.nm"))) {
        return 6;
    }

    if (!run_child(executable, "committed", path)) return 7;
    {
        auto opened = nemo::Database::open(path.string(), test_config());
        if (!opened) return 8;
        auto database = std::move(opened.value());
        if (!verify_row(database, 1, true)) return 9;
        if (!database.close()) return 10;
    }

    if (!run_child(executable, "uncommitted", path)) return 11;
    {
        auto opened = nemo::Database::open(path.string(), test_config());
        if (!opened) return 12;
        auto database = std::move(opened.value());
        if (!verify_row(database, 1, true) ||
            !verify_row(database, 2, false)) {
            return 13;
        }
        if (!database.close()) return 14;
    }

    return 0;
}
