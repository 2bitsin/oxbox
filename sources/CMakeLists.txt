Require(GTest VERSION ">=1.17.0" TEST CONAN gtest COMPONENTS gtest gtest_main gmock gmock_main)
Require(nlohmann_json VERSION "3.11.3" CONAN nlohmann_json PUBLIC)
Require(yaml-cpp VERSION "0.9.0" CONAN yaml-cpp PUBLIC)
Require(pugixml VERSION "1.16" CONAN pugixml PUBLIC)
# header_only is one settings-independent package for every profile
Require(Boost VERSION "1.91.0" CONAN boost OPTIONS header_only=True PUBLIC
        PLATFORM Linux Macos Windows)
Require(OpenSSL VERSION "3.6.3" CONAN openssl PUBLIC
        PLATFORM Linux Macos Windows)

Scan_subdirectories()
