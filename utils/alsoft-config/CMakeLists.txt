project(alsoft-config)

include_directories("${alsoft-config_BINARY_DIR}")

# Need Qt 4.8.0 or newer for the iconset theme attribute to work
find_package(Qt4 COMPONENTS QtCore QtGui)
if(QT_QTCORE_FOUND AND QT_QTGUI_FOUND AND ("${QT_VERSION_MAJOR}" GREATER "4" OR ("${QT_VERSION_MAJOR}" EQUAL "4" AND "${QT_VERSION_MINOR}" GREATER "7")))
    include(${QT_USE_FILE})

    include_directories(${QT_QTCORE_INCLUDE_DIR} ${QT_QTGUI_INCLUDE_DIR})

    set(alsoft-config_SRCS  main.cpp
                            mainwindow.cpp
        )

    set(alsoft-config_UIS  mainwindow.ui)
    QT4_WRAP_UI(UIS ${alsoft-config_UIS})

    set(alsoft-config_MOCS  mainwindow.h)
    QT4_WRAP_CPP(MOCS ${alsoft-config_MOCS})

    add_executable(../../alsoft-config ${alsoft-config_SRCS} ${UIS} ${RSCS} ${TRS} ${MOCS})
    target_link_libraries(../../alsoft-config ${QT_LIBRARIES})

    INSTALL(TARGETS ../../alsoft-config
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION "lib${LIB_SUFFIX}"
            ARCHIVE DESTINATION "lib${LIB_SUFFIX}"
    )
    message(STATUS "Building configuration program")
endif()
