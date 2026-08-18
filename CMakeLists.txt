cmake_minimum_required(VERSION 3.24)
project(AdayoCorpusTool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

option(ADAYO_BUILD_DESKTOP "Build wxWidgets desktop application" OFF)
option(ADAYO_BUILD_TESTS "Build core regression tests" ON)
option(ADAYO_WITH_RAPIDFUZZ "Use rapidfuzz-cpp when available" OFF)
option(ADAYO_WITH_UTF8PROC "Use utf8proc Unicode normalization when available" OFF)
option(ADAYO_WITH_SHERPA_ONNX "Enable native sherpa-onnx TTS adapter" OFF)
option(ADAYO_WITH_MINIAUDIO "Enable miniaudio playback adapter" OFF)
option(ADAYO_WITH_OPENXLSX "Enable OpenXLSX workbook reader" OFF)
option(ADAYO_WITH_LIBXLSXWRITER "Enable libxlsxwriter exporter" OFF)
option(ADAYO_WITH_JSON_CONFIG "Enable nlohmann-json config store" OFF)

add_library(adayo_core STATIC
    src/core/unicode/Utf8.cpp
    src/core/compare/TextNormalizer.cpp
    src/core/compare/TextSimilarity.cpp
    src/core/compare/CharacterDiff.cpp
    src/core/compare/SequenceAligner.cpp
    src/core/workbook/ColumnAnalyzer.cpp
    src/core/workbook/ViewBuilder.cpp
    src/core/worker/WorkerQueue.cpp
    src/services/CompareService.cpp
    src/services/CorpusViewService.cpp
    src/services/TtsService.cpp
    src/services/WorkbookService.cpp
)
target_include_directories(adayo_core PUBLIC src)

if(ADAYO_WITH_RAPIDFUZZ)
    find_package(rapidfuzz CONFIG REQUIRED)
    target_link_libraries(adayo_core PUBLIC rapidfuzz::rapidfuzz)
    target_compile_definitions(adayo_core PUBLIC ADAYO_HAS_RAPIDFUZZ=1)
endif()

if(ADAYO_WITH_UTF8PROC)
    find_package(utf8proc CONFIG REQUIRED)
    if(TARGET utf8proc::utf8proc)
        target_link_libraries(adayo_core PUBLIC utf8proc::utf8proc)
    elseif(TARGET utf8proc)
        target_link_libraries(adayo_core PUBLIC utf8proc)
    else()
        message(FATAL_ERROR "utf8proc package found but no known CMake target exposed")
    endif()
    target_compile_definitions(adayo_core PUBLIC ADAYO_HAS_UTF8PROC=1)
endif()

if(ADAYO_WITH_SHERPA_ONNX)
    add_library(adayo_sherpa_adapter STATIC src/adapters/tts/SherpaOnnxTtsEngine.cpp)
    target_include_directories(adayo_sherpa_adapter PUBLIC src)
    target_link_libraries(adayo_sherpa_adapter PUBLIC adayo_core)
    target_compile_definitions(adayo_sherpa_adapter PUBLIC ADAYO_HAS_SHERPA_ONNX=1)
    # Intentionally not guessing sherpa-onnx target names here.
    # CODEX_EXECUTION_PLAN.md requires pinning the tested Windows package/binary and linking it explicitly.
endif()

if(ADAYO_WITH_MINIAUDIO)
    add_library(adayo_audio_adapter STATIC src/adapters/audio/MiniaudioPlayer.cpp)
    target_include_directories(adayo_audio_adapter PUBLIC src)
    target_link_libraries(adayo_audio_adapter PUBLIC adayo_core)
    target_compile_definitions(adayo_audio_adapter PUBLIC ADAYO_HAS_MINIAUDIO=1)
endif()

if(ADAYO_WITH_OPENXLSX)
    find_package(OpenXLSX CONFIG REQUIRED)
    add_library(adayo_excel_reader STATIC src/adapters/excel/OpenXlsxWorkbookReader.cpp)
    target_include_directories(adayo_excel_reader PUBLIC src)
    target_link_libraries(adayo_excel_reader PUBLIC adayo_core OpenXLSX::OpenXLSX)
    target_compile_definitions(adayo_excel_reader PUBLIC ADAYO_HAS_OPENXLSX=1)
endif()

if(ADAYO_WITH_JSON_CONFIG)
    find_package(nlohmann_json CONFIG REQUIRED)
    add_library(adayo_persistence STATIC src/persistence/JsonConfigStore.cpp)
    target_include_directories(adayo_persistence PUBLIC src)
    target_link_libraries(adayo_persistence PUBLIC adayo_core nlohmann_json::nlohmann_json)
    target_compile_definitions(adayo_persistence PUBLIC ADAYO_HAS_JSON_CONFIG=1)
endif()

if(ADAYO_WITH_LIBXLSXWRITER)
    add_library(adayo_excel_writer STATIC src/adapters/excel/LibXlsxWriterExporter.cpp)
    target_include_directories(adayo_excel_writer PUBLIC src)
    target_link_libraries(adayo_excel_writer PUBLIC adayo_core)
    target_compile_definitions(adayo_excel_writer PUBLIC ADAYO_HAS_LIBXLSXWRITER=1)
    # Target/library name is deliberately resolved in P1 after the Windows dependency baseline is pinned.
endif()

if(ADAYO_BUILD_DESKTOP)
    find_package(wxWidgets REQUIRED COMPONENTS core base adv)
    include(${wxWidgets_USE_FILE})

    add_executable(AdayoCorpusTool WIN32
        src/app/App.cpp
        src/ui/MainFrame.cpp
        src/ui/TtsPanel.cpp
        src/ui/CorpusMappingPanel.cpp
        src/ui/CorpusRunPanel.cpp
        src/ui/ComparePanel.cpp
    )
    target_include_directories(AdayoCorpusTool PRIVATE src)
    target_link_libraries(AdayoCorpusTool PRIVATE adayo_core ${wxWidgets_LIBRARIES})
    if(ADAYO_WITH_OPENXLSX)
        target_link_libraries(AdayoCorpusTool PRIVATE adayo_excel_reader)
    endif()
    if(ADAYO_WITH_JSON_CONFIG)
        target_link_libraries(AdayoCorpusTool PRIVATE adayo_persistence)
    endif()
endif()

if(ADAYO_BUILD_TESTS)
    enable_testing()
    add_executable(adayo_core_tests tests/core_tests.cpp)
    target_link_libraries(adayo_core_tests PRIVATE adayo_core)
    add_test(NAME adayo_core_tests COMMAND adayo_core_tests)

    if(ADAYO_WITH_OPENXLSX AND ADAYO_WITH_JSON_CONFIG)
        add_executable(adayo_p2_tests tests/p2_workbook_tests.cpp)
        target_link_libraries(adayo_p2_tests PRIVATE adayo_core adayo_excel_reader adayo_persistence)
        target_compile_definitions(adayo_p2_tests PRIVATE
            ADAYO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
        add_test(NAME adayo_p2_tests COMMAND adayo_p2_tests)
    endif()
endif()
