cmake_minimum_required(VERSION 3.24)
project(tcg_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)        # compiles qml.qrc
set(CMAKE_AUTOUIC ON)

# ── ONNXRuntime: PREBUILT zip from Microsoft (NOT a vcpkg source build) ─────
# https://github.com/microsoft/onnxruntime/releases -> onnxruntime-win-x64-<ver>.zip
if(WIN32)
    set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime-win-x64-1.23.2" CACHE PATH "ONNXRuntime prebuilt dir")
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

# ── vcpkg deps ──────────────────────────────────────────────────────────────
#   -DCMAKE_TOOLCHAIN_FILE=D:/Coding/vcpkg/scripts/buildsystems/vcpkg.cmake
find_package(faiss CONFIG REQUIRED)
find_package(OpenCV REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets Quick QuickWidgets Network)

# ── headless retrieval test (Piece 1 acceptance test) ──────────────────────
# add_executable(tcg_infer_test main_infer_test.cpp tcg_infer.cpp)
# target_include_directories(tcg_infer_test PRIVATE ${OpenCV_INCLUDE_DIRS})
# target_link_libraries(tcg_infer_test PRIVATE onnxruntime faiss ${OpenCV_LIBS})

# ── the GUI app (Widgets shell + QML panel) ────────────────────────────────
add_executable(tcg_app WIN32
    main.cpp
    MainWindow.cpp   MainWindow.h
    ImageCanvas.cpp  ImageCanvas.h
    Models.cpp       Models.h
    IndexCatalog.cpp IndexCatalog.h
    UiBridge.h
    tcg_infer.cpp    tcg_infer.h
    qml.qrc
    Config.h Config.cpp
    IndexSearchProxy.h
    CardDatabase.h
)
target_include_directories(tcg_app PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(tcg_app PRIVATE
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::Network
    onnxruntime
    faiss
    ${OpenCV_LIBS}
)

if (WIN32)
    foreach(tgt tcg_app)
        add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
                $<TARGET_FILE_DIR:${tgt}>)
    endforeach()
    # deploy Qt runtime (incl. QML modules) so the app runs outside Qt Creator
    find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if (WINDEPLOYQT)
        add_custom_command(TARGET tcg_app POST_BUILD
            COMMAND "${WINDEPLOYQT}" --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                    "$<TARGET_FILE:tcg_app>")
    endif()
endif()
