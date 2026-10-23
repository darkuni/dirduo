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

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network)

# KIO adds servers (SFTP, FTP, SMB, WebDAV...) and archives as folders. Without
# it Linopus still builds and handles local files.
option(LINOPUS_WITH_KIO "Use KIO for remote locations and archives" ON)
if(LINOPUS_WITH_KIO)
    find_package(KF6KIO 6.0 QUIET)
    find_package(KF6CoreAddons 6.0 QUIET)
    if(NOT KF6KIO_FOUND OR NOT KF6CoreAddons_FOUND)
        message(WARNING "KF6 KIO not found: building without remote locations and archives")
        set(LINOPUS_WITH_KIO OFF)
    endif()
endif()
message(STATUS "Linopus with KIO: ${LINOPUS_WITH_KIO}")

option(LINOPUS_BUILD_TESTS "Build the unit tests" ON)

# Which build this is: build-id.txt (written by CI), else git, else "local build".
set(LINOPUS_BUILD_ID "local build")
if(EXISTS "${CMAKE_SOURCE_DIR}/build-id.txt")
    file(STRINGS "${CMAKE_SOURCE_DIR}/build-id.txt" LINOPUS_BUILD_ID LIMIT_COUNT 1)
else()
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} log -1 --format=%h%x20%cd --date=format:%Y-%m-%d
                        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE _git_id
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _git_ok)
        if(_git_ok EQUAL 0 AND _git_id)
            set(LINOPUS_BUILD_ID "${_git_id}")
        endif()
    endif()
endif()
message(STATUS "Linopus build: ${LINOPUS_BUILD_ID}")

add_compile_definitions(
    LINOPUS_VERSION="${PROJECT_VERSION}"
    LINOPUS_BUILD_ID="${LINOPUS_BUILD_ID}"
    LINOPUS_APP_ID="${LINOPUS_APP_ID}"
    QT_NO_KEYWORDS
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
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
    src/core/Zip.cpp
    src/core/RenameEngine.cpp
    src/core/FolderSync.cpp
    src/core/Collections.cpp
    src/core/FindEngine.cpp
    src/core/Favorites.cpp
    src/core/CommandCatalog.cpp
    src/core/ImageConvert.cpp
    src/core/Attributes.cpp
    src/core/HostRun.cpp
    src/core/Servers.cpp
    src/core/ScriptRunner.cpp
    src/core/Metadata.cpp
    src/core/Thumbnails.cpp
    src/core/History.cpp
    src/core/SelectRules.cpp
    src/core/Archive.cpp
    src/core/Condition.cpp
    src/core/DriveList.cpp
    src/core/FileKind.cpp
    src/core/LabelRules.cpp
    src/core/FolderSizes.cpp
    src/core/Location.cpp
    src/core/RemoteBackend.cpp
    src/core/RemoteBackend.h
)
target_include_directories(linopus_core PUBLIC src)
target_link_libraries(linopus_core PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent)
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(linopus_core PRIVATE ZLIB::ZLIB)
    target_compile_definitions(linopus_core PRIVATE LINOPUS_HAVE_ZLIB)
endif()

# Creating and extracting archives. Optional, but the Flatpak requires it.
option(LINOPUS_REQUIRE_LIBARCHIVE "Fail when libarchive is missing" OFF)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBARCHIVE QUIET IMPORTED_TARGET libarchive)
endif()
if(LIBARCHIVE_FOUND)
    target_link_libraries(linopus_core PRIVATE PkgConfig::LIBARCHIVE)
    target_compile_definitions(linopus_core PRIVATE LINOPUS_HAVE_LIBARCHIVE)
    message(STATUS "libarchive ${LIBARCHIVE_VERSION}: archives on")
elseif(LINOPUS_REQUIRE_LIBARCHIVE)
    message(FATAL_ERROR "libarchive not found (LINOPUS_REQUIRE_LIBARCHIVE is on)")
else()
    message(WARNING "libarchive not found: no Add to Archive or Extract")
endif()

# Music tags and photo details. Both optional: without them the columns stay
# empty (photos still get their size in pixels).
option(LINOPUS_WITH_METADATA "Read music tags (TagLib) and photo details (Exiv2) when found" ON)
if(LINOPUS_WITH_METADATA)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(TAGLIB QUIET IMPORTED_TARGET taglib)
        pkg_check_modules(EXIV2 QUIET IMPORTED_TARGET exiv2)
    endif()
    if(TAGLIB_FOUND)
        target_link_libraries(linopus_core PRIVATE PkgConfig::TAGLIB)
        target_compile_definitions(linopus_core PRIVATE LINOPUS_HAVE_TAGLIB)
        message(STATUS "TagLib ${TAGLIB_VERSION}: music tags on")
    else()
        message(WARNING "TagLib not found: no music tags")
    endif()
    if(EXIV2_FOUND)
        target_link_libraries(linopus_core PRIVATE PkgConfig::EXIV2)
        target_compile_definitions(linopus_core PRIVATE LINOPUS_HAVE_EXIV2)
        message(STATUS "Exiv2 ${EXIV2_VERSION}: photo details on")
    else()
        message(WARNING "Exiv2 not found: no camera details")
    endif()
endif()

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
    src/ui/DrivesBar.cpp
    src/ui/ToolbarStrip.cpp
    src/ui/Style.cpp
    src/ui/GroupTreeView.cpp
    src/ui/FolderFormats.cpp
    src/ui/FolderTree.cpp
    src/ui/ButtonEditor.cpp
    src/ui/CustomizeDialog.cpp
    src/ui/CommandConsole.cpp
    src/ui/AdvancedRenameDialog.cpp
    src/ui/SyncDialog.cpp
    src/ui/DuplicatesDialog.cpp
    src/ui/FindDialog.cpp
    src/ui/FavoritesDialog.cpp
    src/ui/ImageConvertDialog.cpp
    src/ui/ArchiveDialog.cpp
    src/ui/PropertiesDialog.cpp
    src/ui/Glyphs.cpp
    src/ui/ServerDialog.cpp
    src/ui/HistoryDialog.cpp
    src/ui/SelectDialog.cpp
    src/ui/Launch.cpp
    src/ui/MetadataPane.cpp
    src/ui/PreferencesDialog.cpp
    src/ui/ViewerPane.cpp
    src/ui/ListerWindow.cpp
    src/ui/AppContext.cpp
)
target_link_libraries(linopus_ui PUBLIC linopus_core Qt6::Widgets Qt6::Network)

if(LINOPUS_WITH_KIO)
    add_library(linopus_kio STATIC
        src/kio/KioBackend.cpp
        src/kio/RemoteJob.cpp
    )
    target_link_libraries(linopus_kio PUBLIC linopus_core Qt6::Widgets KF6::KIOCore KF6::KIOGui KF6::KIOWidgets KF6::CoreAddons)
    target_compile_definitions(linopus_kio PUBLIC LINOPUS_HAVE_KIO)
    target_link_libraries(linopus_ui PUBLIC linopus_kio)
endif()

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
    foreach(t aliases commandparser fileops trash jobmanager commandengine drives location listing condition rename foldersync find imageconvert metadata thumbnails history selectrules archive attributes)
        add_executable(tst_${t} tests/tst_${t}.cpp)
        target_link_libraries(tst_${t} PRIVATE linopus_core Qt6::Test)
        add_test(NAME ${t} COMMAND tst_${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()

    # The archive test makes a hostile archive with libarchive itself.
    if(LIBARCHIVE_FOUND)
        target_link_libraries(tst_archive PRIVATE PkgConfig::LIBARCHIVE)
        target_compile_definitions(tst_archive PRIVATE LINOPUS_HAVE_LIBARCHIVE)
    endif()

    # The metadata test writes tags and EXIF with the libraries themselves.
    if(TAGLIB_FOUND)
        target_link_libraries(tst_metadata PRIVATE PkgConfig::TAGLIB)
        target_compile_definitions(tst_metadata PRIVATE LINOPUS_HAVE_TAGLIB)
    endif()
    if(EXIV2_FOUND)
        target_link_libraries(tst_metadata PRIVATE PkgConfig::EXIV2)
        target_compile_definitions(tst_metadata PRIVATE LINOPUS_HAVE_EXIV2)
    endif()

    if(LINOPUS_BUILD_UI)
        if(LINOPUS_WITH_KIO)
            # Needs live servers: tests/remote/run-remote-tests.sh starts them.
            add_executable(tst_remote tests/tst_remote.cpp)
            target_link_libraries(tst_remote PRIVATE linopus_kio Qt6::Test)
        endif()

        add_executable(tst_ui tests/tst_ui.cpp)
        target_link_libraries(tst_ui PRIVATE linopus_ui Qt6::Test)
        add_test(NAME ui COMMAND tst_ui)
        set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        # Renders a lister offscreen and saves a PNG. Used to eyeball the UI without a desktop.
        add_executable(linopus_snapshot tests/snapshot.cpp)
        target_link_libraries(linopus_snapshot PRIVATE linopus_ui Qt6::Test)

        # Showcase screenshots of a made-up Deck in every theme.
        add_executable(linopus_sizzle tests/sizzle.cpp)
        target_link_libraries(linopus_sizzle PRIVATE linopus_ui Qt6::Test)
    endif()
endif()
