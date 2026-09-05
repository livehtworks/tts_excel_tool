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
set(ADAYO_SHERPA_ONNX_ROOT "" CACHE PATH "Root directory of a pinned sherpa-onnx native package")
set(ADAYO_TEST_MODELS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/dist/AdayoCorpusTool/model" CACHE PATH "Canonical accepted model root used by TTS integration tests")
if(NOT ADAYO_TEST_MODELS_ROOT)
    set(ADAYO_TEST_MODELS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/dist/AdayoCorpusTool/model" CACHE PATH "Canonical accepted model root used by TTS integration tests" FORCE)
endif()

if(ADAYO_BUILD_DESKTOP)
    set(ADAYO_DESKTOP_REQUIRED_OPTIONS
        ADAYO_WITH_RAPIDFUZZ
        ADAYO_WITH_UTF8PROC
        ADAYO_WITH_SHERPA_ONNX
        ADAYO_WITH_MINIAUDIO
        ADAYO_WITH_OPENXLSX
        ADAYO_WITH_LIBXLSXWRITER
        ADAYO_WITH_JSON_CONFIG
    )
    foreach(required_option IN LISTS ADAYO_DESKTOP_REQUIRED_OPTIONS)
        if(NOT ${required_option})
            message(FATAL_ERROR "ADAYO_BUILD_DESKTOP=ON requires ${required_option}=ON for the supported desktop application")
        endif()
    endforeach()
    if(NOT ADAYO_SHERPA_ONNX_ROOT)
        message(FATAL_ERROR "ADAYO_BUILD_DESKTOP=ON requires ADAYO_SHERPA_ONNX_ROOT. Set the ADAYO_SHERPA_ONNX_ROOT environment/cache variable to the pinned sherpa-onnx native package root.")
    endif()
endif()

add_library(adayo_core STATIC
    src/platform/UnicodePath.cpp
    src/platform/FileIo.cpp
    src/core/unicode/Utf8.cpp
    src/core/compare/TextNormalizer.cpp
    src/core/compare/TextSimilarity.cpp
    src/core/compare/CharacterDiff.cpp
    src/core/compare/SequenceAligner.cpp
    src/core/workbook/ColumnAnalyzer.cpp
    src/core/workbook/ViewBuilder.cpp
    src/core/worker/WorkerQueue.cpp
    src/adapters/text/TextFileImporter.cpp
    src/services/CompareService.cpp
    src/services/CorpusViewService.cpp
    src/services/FileLogger.cpp
    src/services/PlaybackService.cpp
    src/services/TtsService.cpp
    src/services/AudioCache.cpp
    src/services/WorkbookService.cpp
)
target_include_directories(adayo_core PUBLIC src)
if(WIN32)
    target_link_libraries(adayo_core PUBLIC bcrypt)
endif()

add_library(adayo_moss_adapter STATIC src/adapters/tts/MossNanoTtsEngine.cpp)
target_include_directories(adayo_moss_adapter PUBLIC src)
target_link_libraries(adayo_moss_adapter PUBLIC adayo_core)

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
    if(NOT ADAYO_SHERPA_ONNX_ROOT)
        message(FATAL_ERROR "ADAYO_WITH_SHERPA_ONNX requires ADAYO_SHERPA_ONNX_ROOT")
    endif()
    set(ADAYO_SHERPA_ONNX_INCLUDE_DIR "${ADAYO_SHERPA_ONNX_ROOT}/include")
    set(ADAYO_SHERPA_ONNX_LIB_DIR "${ADAYO_SHERPA_ONNX_ROOT}/lib")
    set(ADAYO_SHERPA_ONNX_IMPORT_LIB "${ADAYO_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-c-api.lib")
    if(NOT EXISTS "${ADAYO_SHERPA_ONNX_INCLUDE_DIR}/sherpa-onnx/c-api/c-api.h")
        message(FATAL_ERROR "Missing sherpa-onnx c-api.h under ${ADAYO_SHERPA_ONNX_INCLUDE_DIR}")
    endif()
    if(NOT EXISTS "${ADAYO_SHERPA_ONNX_IMPORT_LIB}")
        message(FATAL_ERROR "Missing sherpa-onnx import lib: ${ADAYO_SHERPA_ONNX_IMPORT_LIB}")
    endif()
    add_library(adayo_sherpa_adapter STATIC src/adapters/tts/SherpaOnnxTtsEngine.cpp)
    target_include_directories(adayo_sherpa_adapter PUBLIC src)
    target_include_directories(adayo_sherpa_adapter PRIVATE "${ADAYO_SHERPA_ONNX_INCLUDE_DIR}")
    target_link_libraries(adayo_sherpa_adapter PUBLIC adayo_core "${ADAYO_SHERPA_ONNX_IMPORT_LIB}")
    target_compile_definitions(adayo_sherpa_adapter PUBLIC ADAYO_HAS_SHERPA_ONNX=1)
endif()

if(ADAYO_WITH_MINIAUDIO)
    find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h" REQUIRED)
    add_library(adayo_audio_adapter STATIC src/adapters/audio/MiniaudioPlayer.cpp)
    target_include_directories(adayo_audio_adapter PUBLIC src)
    target_include_directories(adayo_audio_adapter PRIVATE ${MINIAUDIO_INCLUDE_DIRS})
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
    target_link_libraries(adayo_core PUBLIC nlohmann_json::nlohmann_json)
    target_compile_definitions(adayo_core PUBLIC ADAYO_HAS_JSON_CONFIG=1)
    add_library(adayo_persistence STATIC
        src/persistence/JsonConfigStore.cpp
        src/services/ModelRegistry.cpp
    )
    target_include_directories(adayo_persistence PUBLIC src)
    target_link_libraries(adayo_persistence PUBLIC adayo_core nlohmann_json::nlohmann_json)
    target_compile_definitions(adayo_persistence PUBLIC ADAYO_HAS_JSON_CONFIG=1)
endif()

if(ADAYO_WITH_LIBXLSXWRITER)
    find_path(LIBXLSXWRITER_INCLUDE_DIRS "xlsxwriter.h" REQUIRED)
    find_library(LIBXLSXWRITER_LIBRARY xlsxwriter REQUIRED)
    add_library(adayo_excel_writer STATIC src/adapters/excel/LibXlsxWriterExporter.cpp)
    target_include_directories(adayo_excel_writer PUBLIC src)
    target_include_directories(adayo_excel_writer PRIVATE ${LIBXLSXWRITER_INCLUDE_DIRS})
    target_link_libraries(adayo_excel_writer PUBLIC adayo_core ${LIBXLSXWRITER_LIBRARY})
    target_compile_definitions(adayo_excel_writer PUBLIC ADAYO_HAS_LIBXLSXWRITER=1)
endif()

if(ADAYO_BUILD_DESKTOP)
    find_package(wxWidgets REQUIRED COMPONENTS core base adv)
    include(${wxWidgets_USE_FILE})

    add_executable(AdayoCorpusTool WIN32
        src/app/App.cpp
        src/app/ApplicationRuntime.cpp
        src/ui/MainFrame.cpp
        src/ui/TtsPanel.cpp
        src/ui/CorpusMappingPanel.cpp
        src/ui/CorpusRunPanel.cpp
        src/ui/CompareGridTable.cpp
        src/ui/ComparePanel.cpp
    )
    target_include_directories(AdayoCorpusTool PRIVATE src)
    target_link_libraries(AdayoCorpusTool PRIVATE adayo_core ${wxWidgets_LIBRARIES})
    if(MSVC)
        target_link_options(AdayoCorpusTool PRIVATE
            "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/app/AdayoCorpusTool.exe.manifest")
    endif()
    if(ADAYO_WITH_OPENXLSX)
        target_link_libraries(AdayoCorpusTool PRIVATE adayo_excel_reader)
    endif()
    if(ADAYO_WITH_JSON_CONFIG)
        target_link_libraries(AdayoCorpusTool PRIVATE adayo_persistence)
    endif()
    if(ADAYO_WITH_SHERPA_ONNX)
        target_link_libraries(AdayoCorpusTool PRIVATE adayo_sherpa_adapter)
        add_custom_command(TARGET AdayoCorpusTool POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ADAYO_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-c-api.dll"
                "${ADAYO_SHERPA_ONNX_LIB_DIR}/onnxruntime.dll"
                "${ADAYO_SHERPA_ONNX_LIB_DIR}/onnxruntime_providers_shared.dll"
                "$<TARGET_FILE_DIR:AdayoCorpusTool>")
    endif()
    if(ADAYO_WITH_MINIAUDIO)
        target_link_libraries(AdayoCorpusTool PRIVATE adayo_audio_adapter)
    endif()
    if(ADAYO_WITH_LIBXLSXWRITER)
        target_link_libraries(AdayoCorpusTool PRIVATE adayo_excel_writer)
        add_custom_command(TARGET AdayoCorpusTool POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin/xlsxwriter.dll"
                "$<TARGET_FILE_DIR:AdayoCorpusTool>")
    endif()
endif()

if(ADAYO_BUILD_TESTS)
    enable_testing()
    add_executable(adayo_core_tests tests/core_tests.cpp)
    target_link_libraries(adayo_core_tests PRIVATE adayo_core)
    add_test(NAME adayo_core_tests COMMAND adayo_core_tests)

    add_executable(adayo_p5_playback_tests tests/p5_playback_tests.cpp)
    target_link_libraries(adayo_p5_playback_tests PRIVATE adayo_core)
    add_test(NAME adayo_p5_playback_tests COMMAND adayo_p5_playback_tests)

    add_executable(adayo_p8_moss_blocked_tests tests/p8_moss_blocked_tests.cpp)
    target_link_libraries(adayo_p8_moss_blocked_tests PRIVATE adayo_core adayo_moss_adapter)
    add_test(NAME adayo_p8_moss_blocked_tests COMMAND adayo_p8_moss_blocked_tests)

    add_executable(adayo_p6_compare_tests tests/p6_compare_tests.cpp)
    target_link_libraries(adayo_p6_compare_tests PRIVATE adayo_core)
    add_test(NAME adayo_p6_compare_tests COMMAND adayo_p6_compare_tests)

    if(ADAYO_WITH_LIBXLSXWRITER)
        add_executable(adayo_p7_export_tests tests/p7_export_tests.cpp)
        target_link_libraries(adayo_p7_export_tests PRIVATE adayo_core adayo_excel_writer)
        if(ADAYO_WITH_OPENXLSX)
            target_link_libraries(adayo_p7_export_tests PRIVATE adayo_excel_reader)
            target_compile_definitions(adayo_p7_export_tests PRIVATE ADAYO_CAN_VERIFY_XLSX_READ=1)
        endif()
        add_custom_command(TARGET adayo_p7_export_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin/xlsxwriter.dll"
                "$<TARGET_FILE_DIR:adayo_p7_export_tests>")
        add_test(NAME adayo_p7_export_tests COMMAND adayo_p7_export_tests)
    endif()

    if(ADAYO_WITH_OPENXLSX AND ADAYO_WITH_JSON_CONFIG)
        add_executable(adayo_p2_tests tests/p2_workbook_tests.cpp)
        target_link_libraries(adayo_p2_tests PRIVATE adayo_core adayo_excel_reader adayo_persistence)
        target_compile_definitions(adayo_p2_tests PRIVATE
            ADAYO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
        add_test(NAME adayo_p2_tests COMMAND adayo_p2_tests)
    endif()

    if(ADAYO_WITH_SHERPA_ONNX AND ADAYO_WITH_JSON_CONFIG)
        add_executable(adayo_p4_tts_tests tests/p4_tts_tests.cpp)
        target_link_libraries(adayo_p4_tts_tests PRIVATE adayo_core adayo_persistence adayo_sherpa_adapter)
        if(ADAYO_WITH_MINIAUDIO)
            target_link_libraries(adayo_p4_tts_tests PRIVATE adayo_audio_adapter)
            target_include_directories(adayo_p4_tts_tests PRIVATE ${MINIAUDIO_INCLUDE_DIRS})
        endif()
        target_compile_definitions(adayo_p4_tts_tests PRIVATE
            ADAYO_MODELS_DIR="${ADAYO_TEST_MODELS_ROOT}")
        add_custom_command(TARGET adayo_p4_tts_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ADAYO_SHERPA_ONNX_LIB_DIR}/sherpa-onnx-c-api.dll"
                "${ADAYO_SHERPA_ONNX_LIB_DIR}/onnxruntime.dll"
                "${ADAYO_SHERPA_ONNX_LIB_DIR}/onnxruntime_providers_shared.dll"
                "$<TARGET_FILE_DIR:adayo_p4_tts_tests>")
        add_test(NAME adayo_p4_tts_tests COMMAND adayo_p4_tts_tests)
    endif()
endif()
