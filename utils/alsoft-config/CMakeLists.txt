project(alsoft-config)

include_directories("${alsoft-config_BINARY_DIR}")

find_package(Qt4 COMPONENTS QtCore QtGui)
if(QT_QTGUI_LIBRARY AND QT_QTCORE_LIBRARY)
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
    target_link_libraries(../../alsoft-config ${QT_QTCORE_LIBRARY} ${QT_QTGUI_LIBRARY})

    INSTALL(TARGETS ../../alsoft-config
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION "lib${LIB_SUFFIX}"
            ARCHIVE DESTINATION "lib${LIB_SUFFIX}"
    )
    message(STATUS "Building configuration program")
endif()
