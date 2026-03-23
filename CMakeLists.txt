cmake_minimum_required(VERSION 3.24)
project(LiquidGlassEngine LANGUAGES CXX CUDA)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LG_BUILD_WITH_IMGUI "Build with ImGui UI" ON)
option(LG_BUILD_WITH_FFMPEG "Build with FFmpeg video IO" ON)
option(LG_ENABLE_CUDA_GRAPH "Enable CUDA Graph path" ON)
option(LG_ENABLE_OPENGL_INTEROP "Enable CUDA-OpenGL interop" ON)

find_package(OpenGL REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(glfw3 REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(IMG_JPEG REQUIRED IMPORTED_TARGET libjpeg)
pkg_check_modules(IMG_PNG REQUIRED IMPORTED_TARGET libpng)

if(LG_BUILD_WITH_FFMPEG)
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec libavformat libavutil libswscale)
endif()

add_library(lg_core
  src/core/Config.cpp
  src/core/RealtimePlayback.cpp
  src/core/Timer.cpp
  src/utils/Logger.cpp
  src/utils/Color.cpp
  src/io/ImageIO.cpp
  src/io/VideoDecoder.cpp
  src/io/VideoEncoder.cpp
  src/ui/Interaction.cpp
  src/ui/TabBar.cpp
  src/renderer/GLDisplay.cpp
  src/renderer/CUDAInterop.cpp
  src/renderer/Renderer.cpp
  src/core/Engine.cpp
  src/app/App.cpp
  src/liquid_glass/ShapeSDF.cpp
  src/liquid_glass/ExecutionPlan.cpp
  src/liquid_glass/GlassPipeline.cpp
  src/experiments/Benchmark.cpp
  src/experiments/Ablation.cpp
  src/experiments/DatasetRunner.cpp
  src/experiments/AblationBatchRunner.cpp
  src/experiments/ImageBatchRunner.cpp
  src/experiments/Metrics.cpp
  src/liquid_glass/ThicknessField.cu
  src/liquid_glass/NormalField.cu
  src/liquid_glass/Refraction.cu
  src/liquid_glass/ReflectionSpecular.cu
  src/liquid_glass/Legibility.cu
  src/liquid_glass/Temporal.cu
  src/liquid_glass/Compose.cu
)

target_include_directories(lg_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(lg_core PUBLIC
  OpenGL::GL
  glfw
  CUDA::cudart
  CUDA::cuda_driver
  PkgConfig::IMG_JPEG
  PkgConfig::IMG_PNG
)

if(LG_BUILD_WITH_FFMPEG)
  target_link_libraries(lg_core PUBLIC PkgConfig::FFMPEG)
  target_compile_definitions(lg_core PUBLIC LG_WITH_FFMPEG=1)
endif()

if(LG_ENABLE_CUDA_GRAPH)
  target_compile_definitions(lg_core PUBLIC LG_ENABLE_CUDA_GRAPH=1)
endif()
if(LG_ENABLE_OPENGL_INTEROP)
  target_compile_definitions(lg_core PUBLIC LG_ENABLE_OPENGL_INTEROP=1)
endif()

target_compile_options(lg_core PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>
  $<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math -lineinfo>
)

set_target_properties(lg_core PROPERTIES
  CUDA_SEPARABLE_COMPILATION ON
  CUDA_ARCHITECTURES 89
)

add_executable(liquid_glass_engine src/app/main.cpp)
target_link_libraries(liquid_glass_engine PRIVATE lg_core)
add_executable(liquid_glass_analysis src/experiments/main.cpp)
target_link_libraries(liquid_glass_analysis PRIVATE lg_core)

if(BUILD_TESTING)
  add_executable(lg_phase1_tests
    tests/test_phase1.cpp
    src/core/Config.cpp
    src/utils/Logger.cpp
  )
  target_include_directories(lg_phase1_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_compile_options(lg_phase1_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME lg_phase1_tests COMMAND lg_phase1_tests)
  set_tests_properties(lg_phase1_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  add_executable(lg_phase2_tests
    tests/test_phase2.cpp
    src/core/Config.cpp
    src/utils/Logger.cpp
    src/io/ImageIO.cpp
  )
  target_include_directories(lg_phase2_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_compile_options(lg_phase2_tests PRIVATE -Wall -Wextra -Wpedantic)
  target_link_libraries(lg_phase2_tests PRIVATE PkgConfig::IMG_JPEG PkgConfig::IMG_PNG)
  add_test(NAME lg_phase2_tests COMMAND lg_phase2_tests)
  set_tests_properties(lg_phase2_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  add_executable(lg_phase3_tests
    tests/test_phase3.cpp
    src/ui/TabBar.cpp
    src/ui/Interaction.cpp
    src/liquid_glass/ShapeSDF.cpp
  )
  target_include_directories(lg_phase3_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_link_libraries(lg_phase3_tests PRIVATE glfw)
  target_compile_options(lg_phase3_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME lg_phase3_tests COMMAND lg_phase3_tests)
  set_tests_properties(lg_phase3_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  add_executable(lg_phase4_tests
    tests/test_phase4.cpp
    src/core/Config.cpp
    src/liquid_glass/ShapeSDF.cpp
    src/ui/Interaction.cpp
  )
  target_include_directories(lg_phase4_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_link_libraries(lg_phase4_tests PRIVATE glfw)
  target_compile_options(lg_phase4_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME lg_phase4_tests COMMAND lg_phase4_tests)
  set_tests_properties(lg_phase4_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  if(LG_BUILD_WITH_FFMPEG)
    add_executable(lg_phase5_tests
      tests/test_phase5.cpp
      src/core/Config.cpp
      src/io/VideoDecoder.cpp
      src/io/VideoEncoder.cpp
    )
    target_include_directories(lg_phase5_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(lg_phase5_tests PRIVATE PkgConfig::FFMPEG)
    target_compile_definitions(lg_phase5_tests PRIVATE LG_WITH_FFMPEG=1)
    target_compile_options(lg_phase5_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME lg_phase5_tests COMMAND lg_phase5_tests)
    set_tests_properties(lg_phase5_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endif()

  add_executable(lg_phase6_tests
    tests/test_phase6.cpp
    src/core/Config.cpp
    src/core/RealtimePlayback.cpp
    src/utils/Logger.cpp
    src/ui/Interaction.cpp
    src/renderer/GLDisplay.cpp
    src/renderer/CUDAInterop.cpp
    src/liquid_glass/ShapeSDF.cpp
  )
  target_include_directories(lg_phase6_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_link_libraries(lg_phase6_tests PRIVATE glfw OpenGL::GL CUDA::cudart)
  target_compile_options(lg_phase6_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME lg_phase6_tests COMMAND lg_phase6_tests)
  set_tests_properties(lg_phase6_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  add_executable(lg_phase7_tests
    tests/test_phase7.cpp
    src/core/Config.cpp
    src/core/Timer.cpp
    src/io/ImageIO.cpp
    src/experiments/Benchmark.cpp
    src/experiments/Metrics.cpp
    src/liquid_glass/ExecutionPlan.cpp
  )
  target_include_directories(lg_phase7_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_link_libraries(lg_phase7_tests PRIVATE PkgConfig::IMG_JPEG PkgConfig::IMG_PNG)
  target_compile_options(lg_phase7_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME lg_phase7_tests COMMAND lg_phase7_tests)
  set_tests_properties(lg_phase7_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  add_executable(lg_phase8_tests
    tests/test_phase8.cpp
    src/core/Timer.cpp
    src/io/ImageIO.cpp
    src/experiments/Benchmark.cpp
    src/experiments/Metrics.cpp
    src/experiments/DatasetRunner.cpp
    src/experiments/Ablation.cpp
  )
  target_include_directories(lg_phase8_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_link_libraries(lg_phase8_tests PRIVATE PkgConfig::IMG_JPEG PkgConfig::IMG_PNG)
  target_compile_options(lg_phase8_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME lg_phase8_tests COMMAND lg_phase8_tests)
  set_tests_properties(lg_phase8_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  add_executable(lg_phase9_tests
    tests/test_phase9.cpp
  )
  target_link_libraries(lg_phase9_tests PRIVATE lg_core)
  target_include_directories(lg_phase9_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_compile_options(lg_phase9_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME lg_phase9_tests COMMAND lg_phase9_tests)
  set_tests_properties(lg_phase9_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

install(TARGETS liquid_glass_engine RUNTIME DESTINATION bin)
