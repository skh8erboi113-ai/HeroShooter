# app/src/main/cpp/CMakeLists.txt
cmake_minimum_required(VERSION 3.28)

project(HeroShooterEngine
    VERSION     0.1.0
    LANGUAGES   CXX C
    DESCRIPTION "High-performance Android hero shooter game engine"
)

# ── Language standard ─────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)   # Disable GNU extensions for portability

# ── Toolchain sanity check ────────────────────────────────────────────────────
if(NOT ANDROID)
    message(FATAL_ERROR
        "This CMakeLists.txt is Android-only. "
        "Use the Android Gradle Plugin to build."
    )
endif()

message(STATUS "Android ABI       : ${ANDROID_ABI}")
message(STATUS "Android API Level : ${ANDROID_PLATFORM}")
message(STATUS "NDK Path          : ${ANDROID_NDK}")
message(STATUS "Compiler          : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# ── Build options (overridable from Gradle) ───────────────────────────────────
option(ENABLE_VULKAN_VALIDATION  "Enable Vulkan validation layers"   OFF)
option(ENABLE_PROFILING          "Enable CPU/GPU profiling markers"  OFF)
option(ENABLE_PHYSICS            "Enable Jolt physics integration"   ON)
option(ENABLE_NETWORKING         "Enable Yojimbo networking"         ON)

# ── Compiler & linker flags ───────────────────────────────────────────────────
# These flags apply to ALL targets in this CMake project.
add_compile_options(
    # Warnings — treat all warnings as errors in production
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wno-unused-parameter          # Too noisy with ECS/callback patterns
    -Wno-missing-field-initializers

    # Optimisation
    -O3
    -ffast-math                    # Allow float reassociation (game-safe)
    -funroll-loops

    # ABI / code-size
    -fno-rtti                      # No runtime type information
    -fno-exceptions                # No C++ exceptions (use error codes)
    -fvisibility=hidden            # Hide all symbols by default
    -ffunction-sections            # Place each function in own section
    -fdata-sections                # Place each data item in own section

    # ARM SIMD
    $<$<STREQUAL:${ANDROID_ABI},arm64-v8a>:-march=armv8.2-a+fp16+dotprod>
    $<$<STREQUAL:${ANDROID_ABI},armeabi-v7a>:-mfpu=neon-vfpv4 -mfloat-abi=hard>
)

add_link_options(
    -Wl,--gc-sections              # Remove unused sections (dead code elimination)
    -Wl,--strip-all                # Strip debug symbols in .so
    -Wl,-z,nocopyreloc             # Reduce relocations
)

if(ENABLE_PROFILING)
    add_compile_definitions(ENGINE_PROFILING_ENABLED=1)
endif()

# ── Android NDK system libraries ──────────────────────────────────────────────
find_library(lib_android    android  REQUIRED)
find_library(lib_log        log      REQUIRED)
find_library(lib_vulkan     vulkan   REQUIRED)
find_library(lib_EGL        EGL      REQUIRED)    # Fallback / surface query
find_library(lib_GLESv3     GLESv3   REQUIRED)    # For UI overlays if needed

# ── AGDK Prefab packages (provided by Gradle AAR dependencies) ────────────────
# Gradle's 'prefab = true' in buildFeatures extracts these from AARs
find_package(game-activity      REQUIRED CONFIG)
find_package(games-frame-pacing REQUIRED CONFIG)
find_package(oboe               REQUIRED CONFIG)

# ── EnTT (header-only ECS library) ───────────────────────────────────────────
# Fetched at configure time via FetchContent — no runtime overhead
include(FetchContent)

FetchContent_Declare(
    entt
    GIT_REPOSITORY  https://github.com/skypjack/entt.git
    GIT_TAG         v3.13.2
    GIT_SHALLOW     TRUE            # Shallow clone — faster CI
)
FetchContent_MakeAvailable(entt)

# EnTT is header-only; linking gives us the include path
# EnTT is configured to be FREESTANDING (no RTTI, no exceptions)
target_compile_definitions(EnTT::EnTT INTERFACE
    ENTT_NO_ETO                    # Disable empty type optimisation (debug clarity)
)

# ── Jolt Physics ──────────────────────────────────────────────────────────────
if(ENABLE_PHYSICS)
    FetchContent_Declare(
        JoltPhysics
        GIT_REPOSITORY  https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG         v5.1.0
        GIT_SHALLOW     TRUE
        SOURCE_SUBDIR   Build       # Jolt's CMakeLists is in Build/
    )

    # Configure Jolt build options BEFORE MakeAvailable
    set(TARGET_HELLO_WORLD          OFF CACHE BOOL "" FORCE)
    set(TARGET_UNIT_TESTS           OFF CACHE BOOL "" FORCE)
    set(TARGET_PERFORMANCE_TEST     OFF CACHE BOOL "" FORCE)
    set(TARGET_SAMPLES              OFF CACHE BOOL "" FORCE)
    set(TARGET_VIEWER               OFF CACHE BOOL "" FORCE)
    set(CROSS_PLATFORM_DETERMINISTIC ON  CACHE BOOL "" FORCE)  # Multiplayer determinism
    set(USE_STD_VECTOR              ON   CACHE BOOL "" FORCE)
    # Jolt does use exceptions internally — we isolate behind our wrapper
    set(OVERRIDE_CXX_FLAGS          OFF  CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(JoltPhysics)
    add_compile_definitions(ENGINE_PHYSICS_ENABLED=1)
endif()

# ── Yojimbo Networking ────────────────────────────────────────────────────────
# Yojimbo depends on: libsodium (crypto), mbedTLS or similar
# For Android cross-compilation we vendor pre-built libsodium and use
# Yojimbo's CMake directly.
if(ENABLE_NETWORKING)
    FetchContent_Declare(
        yojimbo
        GIT_REPOSITORY  https://github.com/networkprotocol/yojimbo.git
        GIT_TAG         1.2.1
        GIT_SHALLOW     TRUE
    )
    # NOTE: Yojimbo requires libsodium. Pre-built libs should be placed at:
    # third_party/libsodium/lib/${ANDROID_ABI}/libsodium.a
    # and headers at: third_party/libsodium/include/
    set(SODIUM_INCLUDE_DIR
        "${CMAKE_SOURCE_DIR}/../../../third_party/libsodium/include"
        CACHE STRING "" FORCE
    )
    set(SODIUM_LIBRARY
        "${CMAKE_SOURCE_DIR}/../../../third_party/libsodium/lib/${ANDROID_ABI}/libsodium.a"
        CACHE STRING "" FORCE
    )

    # Only enable Yojimbo if libsodium is present; otherwise stub networking
    if(EXISTS "${SODIUM_LIBRARY}")
        FetchContent_MakeAvailable(yojimbo)
        add_compile_definitions(ENGINE_NETWORKING_ENABLED=1)
        message(STATUS "Yojimbo networking: ENABLED")
    else()
        message(WARNING
            "libsodium not found at ${SODIUM_LIBRARY}. "
            "Networking will use stub implementation. "
            "See third_party/libsodium/README.md."
        )
        add_compile_definitions(ENGINE_NETWORKING_STUB=1)
    endif()
endif()

# ── Shader compilation (GLSL → SPIR-V) ───────────────────────────────────────
# We compile shaders at build time using glslangValidator from the NDK
find_program(GLSLANG
    glslangValidator
    HINTS   "${ANDROID_NDK}/shader-tools/${ANDROID_HOST_TAG}"
            "${ANDROID_NDK}/tools/glslang/bin"
    REQUIRED
)

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../assets/shaders")
set(SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_BINARY_DIR}")

# Collect all GLSL shaders
file(GLOB SHADER_SOURCES
    "${SHADER_SOURCE_DIR}/*.vert"
    "${SHADER_SOURCE_DIR}/*.frag"
    "${SHADER_SOURCE_DIR}/*.comp"
)

set(SPIRV_BINARY_FILES "")
foreach(SHADER_SOURCE ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
    set(SPIRV_OUTPUT "${SHADER_BINARY_DIR}/${SHADER_NAME}.spv")

    add_custom_command(
        OUTPUT  ${SPIRV_OUTPUT}
        COMMAND ${GLSLANG}
                --target-env vulkan1.1
                -V
                ${SHADER_SOURCE}
                -o ${SPIRV_OUTPUT}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling shader: ${SHADER_NAME} → ${SHADER_NAME}.spv"
        VERBATIM
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV_OUTPUT})
endforeach()

add_custom_target(compile_shaders ALL DEPENDS ${SPIRV_BINARY_FILES})

# ── Engine source files ───────────────────────────────────────────────────────
set(ENGINE_SOURCES
    # Entry point & lifecycle
    main/android_main.cpp
    main/GameEngine.cpp

    # ECS systems
    ecs/EntityManager.cpp
    ecs/systems/MovementSystem.cpp
    ecs/systems/PhysicsSystem.cpp
    ecs/systems/RenderSystem.cpp
    ecs/systems/NetworkSystem.cpp

    # Vulkan rendering
    rendering/VulkanRenderer.cpp
    rendering/VulkanDevice.cpp
    rendering/VulkanSwapchain.cpp
    rendering/VulkanPipeline.cpp
    rendering/VulkanBuffer.cpp
    rendering/ShaderManager.cpp

    # Physics
    physics/PhysicsWorld.cpp

    # Audio
    audio/AudioEngine.cpp

    # Networking
    network/NetworkManager.cpp
    network/ClientAdapter.cpp

    # Memory management
    memory/PoolAllocator.cpp
    memory/LinearAllocator.cpp

    # Threading
    threading/ThreadPool.cpp
    threading/JobSystem.cpp

    # Utilities
    utils/Timer.cpp
)

# ── Main shared library target ────────────────────────────────────────────────
add_library(hero_shooter_engine SHARED ${ENGINE_SOURCES})

# Ensure shaders are compiled before the .so
add_dependencies(hero_shooter_engine compile_shaders)

# ── Include directories ───────────────────────────────────────────────────────
target_include_directories(hero_shooter_engine
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}          # Own headers
        ${CMAKE_CURRENT_SOURCE_DIR}/ecs
        ${CMAKE_CURRENT_SOURCE_DIR}/rendering
        ${CMAKE_CURRENT_SOURCE_DIR}/physics
        ${CMAKE_CURRENT_SOURCE_DIR}/audio
        ${CMAKE_CURRENT_SOURCE_DIR}/network
        ${CMAKE_CURRENT_SOURCE_DIR}/memory
        ${CMAKE_CURRENT_SOURCE_DIR}/threading
        ${CMAKE_CURRENT_SOURCE_DIR}/utils
        ${SHADER_BINARY_DIR}                 # Generated SPIR-V includes (optional)
)

# ── Compile definitions ───────────────────────────────────────────────────────
target_compile_definitions(hero_shooter_engine
    PRIVATE
        ANDROID
        VK_USE_PLATFORM_ANDROID_KHR          # Enable Vulkan Android extensions
        ENGINE_VERSION_MAJOR=${ENGINE_VERSION_MAJOR}
        ENGINE_VERSION_MINOR=${ENGINE_VERSION_MINOR}
        $<$<BOOL:${ENABLE_VULKAN_VALIDATION}>:ENABLE_VALIDATION_LAYERS>
        $<$<CONFIG:Debug>:ENGINE_DEBUG_BUILD>
        $<$<CONFIG:Release>:ENGINE_RELEASE_BUILD NDEBUG>
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(hero_shooter_engine
    PRIVATE
        # Android platform
        ${lib_android}
        ${lib_log}
        ${lib_vulkan}
        ${lib_EGL}
        ${lib_GLESv3}

        # AGDK
        game-activity::game-activity        # GameActivity C API
        games-frame-pacing::swappy_static   # Swappy (static for .so isolation)

        # Oboe audio (static link preferred to avoid version mismatch)
        oboe::oboe

        # EnTT (header-only — includes resolved through target)
        EnTT::EnTT

        # Jolt Physics
        $<$<BOOL:${ENABLE_PHYSICS}>:Jolt>

        # Yojimbo Networking (conditional)
        $<$<AND:$<BOOL:${ENABLE_NETWORKING}>,$<TARGET_EXISTS:yojimbo>>:yojimbo>
        $<$<AND:$<BOOL:${ENABLE_NETWORKING}>,$<EXISTS:${SODIUM_LIBRARY}>>:${SODIUM_LIBRARY}>

        # C++ atomic (required for std::atomic on armeabi-v7a)
        atomic
)

# ── Export visibility ─────────────────────────────────────────────────────────
# Only the GameActivity entry point needs to be exported
set_target_properties(hero_shooter_engine PROPERTIES
    CXX_VISIBILITY_PRESET           hidden
    VISIBILITY_INLINES_HIDDEN       ON
    LINK_FLAGS                      "-Wl,--export-dynamic-symbol=android_main"
)
