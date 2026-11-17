cmake_minimum_required(VERSION 3.24)
project(WSDeckImporter LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(WIN32)
    set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime-directml-1.24.4" CACHE PATH "ONNXRuntime prebuilt dir")
elseif(APPLE)
    set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime-osx-arm64-1.23.2" CACHE PATH "ONNXRuntime prebuilt dir")
endif()
if (NOT EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "ONNXRuntime not found at ${ONNXRUNTIME_ROOT}. "
                        "Download the prebuilt zip and set -DONNXRUNTIME_ROOT=<path>.")
endif()
add_library(onnxruntime SHARED IMPORTED)
if(WIN32)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_IMPLIB   "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
        IMPORTED_LOCATION "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_ROOT}/include")
elseif(APPLE)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_ROOT}/include")
endif()

find_package(faiss CONFIG REQUIRED)
find_package(OpenCV REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets Quick QuickWidgets Network Concurrent Sql Svg QuickControls2)

set(WS_MINIZ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz")

add_library(ws_miniz STATIC
    "${WS_MINIZ_DIR}/miniz.c"
)

target_include_directories(ws_miniz PUBLIC
    "${WS_MINIZ_DIR}"
)

target_compile_definitions(ws_miniz PUBLIC
    MINIZ_NO_STDIO
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES
)

set_target_properties(ws_miniz PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)


add_executable(WSDeckImporter WIN32
    main.cpp
    src/app/MainWindow.cpp   src/app/MainWindow.h
    src/detection/ImageCanvas.cpp  src/detection/ImageCanvas.h
    src/viewmodels/Models.cpp       src/viewmodels/Models.h
    src/index/IndexCatalog.cpp src/index/IndexCatalog.h
    src/viewmodels/UiBridge.h

    qml.qrc
    src/retrieval/CardDetector.cpp  src/retrieval/CardDetector.h
    src/detection/CompareDialog.cpp  src/detection/CompareDialog.h
    src/core/Config.h src/core/Config.cpp
    src/index/IndexSearchProxy.h



    src/database/DatabaseUtil.h src/database/DatabaseUtil.cpp
    src/gallery/GalleryPage.h src/gallery/GalleryPage.cpp
    src/index/FaissPage.h src/index/FaissPage.cpp
    src/settings/SettingsPage.h src/settings/SettingsPage.cpp
    src/detection/DetectionPage.h src/detection/DetectionPage.cpp
    src/services/ModelService.h src/services/ModelService.cpp
    src/cardsIndex/CardsPage.h src/cardsIndex/CardsPage.cpp
    src/cardsIndex/SeriesCatalog.h src/cardsIndex/SeriesCatalog.cpp
    src/cardsIndex/SeriesSearchProxy.h
    src/images/CardImageProvider.h src/images/CardImageProvider.cpp
    src/database/CardResolver.cpp src/database/CardResolver.h
    src/cardsIndex/CardsPage.cpp src/cardsIndex/CardsPage.h src/cardsIndex/SeriesCatalog.cpp src/cardsIndex/SeriesCatalog.h src/cardsIndex/SeriesRepository.cpp src/cardsIndex/SeriesRepository.h src/cardsIndex/SeriesSearchProxy.h
    src/models/index_builder.cpp src/models/index_builder.h src/models/tcg_core.cpp src/models/tcg_core.h src/models/tcg_infer.cpp src/models/tcg_infer.h
    src/models/tcg_util.h
    src/index/IndexGenerationPage.h src/index/IndexGenerationPage.cpp
    src/network/NetGuard.cpp src/network/NetGuard.h
    src/core/AppUpdateController.cpp src/core/AppUpdateController.h
    src/database/LocalCardStore.cpp src/database/LocalCardStore.h
    src/database/LocalCardStore.cpp src/database/LocalCardStore.h
    src/deckbuilder/DeckBuilderImages.cpp src/deckbuilder/DeckBuilderImages.h src/deckbuilder/DeckBuilderPage.cpp src/deckbuilder/DeckBuilderPage.h
    qml/deckbuilder/DeckBuilderPage.qml
    src/database/TriggerAliases.h
    src/database/ReferenceSearch.h
    src/deckbuilder/DeckCode.h src/deckbuilder/DeckCode.cpp
    src/deckbuilder/DeckBuilderController.h src/deckbuilder/DeckBuilderController.cpp
    src/gallery/GalleryController.h src/gallery/GalleryController.cpp
    src/database/LocalDatabaseUpdater.h src/database/LocalDatabaseUpdater.cpp
)

set(APP_VERSION "")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(GIT_TAG)
        set(APP_VERSION "${GIT_TAG}")
    endif()
endif()
message(STATUS "App version: ${APP_VERSION}")
target_compile_definitions(WSDeckImporter PRIVATE APP_VERSION="${APP_VERSION}")

target_include_directories(WSDeckImporter PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(WSDeckImporter PRIVATE
    ws_miniz
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::Network
    Qt6::Concurrent
    Qt6::Sql
    Qt6::Svg
    Qt6::QuickControls2
    onnxruntime
    faiss
    ${OpenCV_LIBS}
)

if (WIN32)
    foreach(tgt WSDeckImporter)
        add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
                $<TARGET_FILE_DIR:${tgt}>)
    endforeach()

    find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if (WINDEPLOYQT)
        add_custom_command(TARGET WSDeckImporter POST_BUILD
            COMMAND "${WINDEPLOYQT}" --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                    "$<TARGET_FILE:WSDeckImporter>")
    endif()
endif()

# if (APPLE)
#     set_target_properties(tcg_app PROPERTIES MACOSX_BUNDLE TRUE)
# endif()
