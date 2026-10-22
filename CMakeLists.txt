cmake_minimum_required(VERSION 3.22)
project(linopus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(LINOPUS_APP_ID "com.monroeworld.Linopus")

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

option(LINOPUS_BUILD_TESTS "Build the unit tests" ON)

add_compile_definitions(
    LINOPUS_VERSION="${PROJECT_VERSION}"
    LINOPUS_APP_ID="${LINOPUS_APP_ID}"
    QT_NO_KEYWORDS
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# Core: no widgets. Everything that runs without a window lives here.
add_library(linopus_core STATIC
    src/core/Paths.cpp
    src/core/AliasManager.cpp
    src/core/FileItem.cpp
    src/core/DirLister.cpp
    src/core/FileOps.cpp
    src/core/Trash.cpp
    src/core/Job.cpp
    src/core/JobManager.cpp
    src/core/CommandParser.cpp
    src/core/CommandEngine.cpp
)
target_include_directories(linopus_core PUBLIC src)
target_link_libraries(linopus_core PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent)

option(LINOPUS_BUILD_UI "Build the application (off: core library and its tests only)" ON)

if(LINOPUS_BUILD_UI)
# UI: listers, displays, dialogs.
add_library(linopus_ui STATIC
    src/ui/FileListModel.cpp
    src/ui/FileSortProxy.cpp
    src/ui/RenameDelegate.cpp
    src/ui/FolderTab.cpp
    src/ui/BreadcrumbBar.cpp
    src/ui/FileDisplay.cpp
    src/ui/ButtonBar.cpp
    src/ui/CollisionDialog.cpp
    src/ui/ProgressWindow.cpp
    src/ui/ListerWindow.cpp
    src/ui/AppContext.cpp
)
target_link_libraries(linopus_ui PUBLIC linopus_core Qt6::Widgets)

add_executable(linopus src/main.cpp)
target_link_libraries(linopus PRIVATE linopus_ui)

include(GNUInstallDirs)
install(TARGETS linopus RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/${LINOPUS_APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/${LINOPUS_APP_ID}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/${LINOPUS_APP_ID}.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

if(LINOPUS_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    foreach(t aliases commandparser fileops trash jobmanager commandengine)
        add_executable(tst_${t} tests/tst_${t}.cpp)
        target_link_libraries(tst_${t} PRIVATE linopus_core Qt6::Test)
        add_test(NAME ${t} COMMAND tst_${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()

    if(LINOPUS_BUILD_UI)
        add_executable(tst_ui tests/tst_ui.cpp)
        target_link_libraries(tst_ui PRIVATE linopus_ui Qt6::Test)
        add_test(NAME ui COMMAND tst_ui)
        set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        # Renders a lister offscreen and saves a PNG. Used to eyeball the UI without a desktop.
        add_executable(linopus_snapshot tests/snapshot.cpp)
        target_link_libraries(linopus_snapshot PRIVATE linopus_ui Qt6::Test)
    endif()
endif()
