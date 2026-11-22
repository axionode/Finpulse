cmake_minimum_required(VERSION 3.20)
project(Finpulse VERSION 0.1.0 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------- toolchain flags
if(MSVC)
  # ADR-0001: static CRT, hardening, UTF-8 sources
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/utf-8 $<$<COMPILE_LANGUAGE:CXX>:/permissive-> $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus> $<$<COMPILE_LANGUAGE:CXX>:/GR-> /D_HAS_EXCEPTIONS=0)
  add_compile_options($<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/GL> /sdl /guard:cf)
  add_link_options(/DYNAMICBASE /NXCOMPAT /guard:cf /MANIFEST:NO /OPT:REF /OPT:ICF)
  add_link_options($<$<CONFIG:Release>:/LTCG>)
  set(FP_WARN /W4)
else()
  # MinGW-w64 (cross build from Linux or MSYS2)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions> $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti> -municode)
  add_compile_options($<$<CONFIG:Release>:-O2>)
  add_link_options(-static -static-libgcc -static-libstdc++ -municode -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va)
  add_link_options($<$<CONFIG:Release>:-s>)
  set(FP_WARN -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-cast-function-type)
endif()

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)

# ---------------------------------------------------------------- third party
# SQLite3 Multiple Ciphers (ADR-0005). TEMP_STORE=3: temp tables never touch disk (they are not encrypted).
add_library(sqlite3mc STATIC third_party/sqlite3mc/sqlite3mc_amalgamation.c)
target_include_directories(sqlite3mc PUBLIC third_party/sqlite3mc)
target_compile_definitions(sqlite3mc PUBLIC
  SQLITE_THREADSAFE=2 SQLITE_DQS=0 SQLITE_DEFAULT_FOREIGN_KEYS=1 SQLITE_OMIT_LOAD_EXTENSION
  SQLITE_OMIT_DEPRECATED SQLITE_OMIT_SHARED_CACHE SQLITE_ENABLE_FTS5 SQLITE_SECURE_DELETE
  SQLITE_TEMP_STORE=3 SQLITE_DEFAULT_MEMSTATUS=0 SQLITE_LIKE_DOESNT_MATCH_BLOBS
  SQLITE_DEFAULT_WAL_SYNCHRONOUS=2 CODEC_TYPE=CODEC_TYPE_CHACHA20)
if(MSVC)
  target_compile_options(sqlite3mc PRIVATE /W0)
else()
  target_compile_options(sqlite3mc PRIVATE -w)
endif()

# Argon2 reference implementation (ADR-0007)
add_library(argon2 STATIC
  third_party/argon2/src/argon2.c third_party/argon2/src/core.c third_party/argon2/src/encoding.c
  third_party/argon2/src/ref.c third_party/argon2/src/thread.c third_party/argon2/src/blake2/blake2b.c)
target_include_directories(argon2 PUBLIC third_party/argon2/include PRIVATE third_party/argon2/src)
if(MSVC)
  target_compile_options(argon2 PRIVATE /W0)
else()
  target_compile_options(argon2 PRIVATE -w)
endif()

# ---------------------------------------------------------------- core library (no UI)
file(GLOB FP_CORE_SOURCES CONFIGURE_DEPENDS
  src/core/*.cpp src/crypto/*.cpp src/storage/*.cpp src/services/*.cpp)
add_library(fpcore STATIC ${FP_CORE_SOURCES})
target_include_directories(fpcore PUBLIC src)
target_compile_options(fpcore PRIVATE ${FP_WARN})
target_link_libraries(fpcore PUBLIC sqlite3mc argon2 bcrypt crypt32 advapi32 ole32 shell32)

# ---------------------------------------------------------------- application
file(GLOB FP_APP_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp src/app/*.cpp)
add_executable(Finpulse WIN32 ${FP_APP_SOURCES} src/resources/finpulse.rc)
target_compile_options(Finpulse PRIVATE ${FP_WARN})
target_link_libraries(Finpulse PRIVATE fpcore
  d2d1 dwrite d3d11 dxgi dwmapi uxtheme shcore imm32 ole32 oleaut32 shell32 shlwapi comdlg32
  windowscodecs wtsapi32 user32 gdi32 uuid)

# ---------------------------------------------------------------- tests (console, run under Windows or Wine)
file(GLOB FP_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(finpulse_tests ${FP_TEST_SOURCES})
target_link_libraries(finpulse_tests PRIVATE fpcore)

# ---------------------------------------------------------------- offline guarantee (ADR-0008)
if(NOT CMAKE_CROSSCOMPILING)
  add_executable(check_imports tools/check_imports.cpp)
  add_custom_command(TARGET Finpulse POST_BUILD
    COMMAND check_imports $<TARGET_FILE:Finpulse>
    COMMENT "Checking import table for forbidden network DLLs")
endif()
