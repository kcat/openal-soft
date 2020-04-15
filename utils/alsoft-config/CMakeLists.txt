project(alsoft-config)

find_package(Qt5Widgets)
if(Qt5Widgets_FOUND)
    qt5_wrap_ui(UIS  mainwindow.ui)

    qt5_wrap_cpp(MOCS  mainwindow.h)

    add_executable(alsoft-config
        main.cpp
        mainwindow.cpp
        mainwindow.h
        verstr.cpp
        verstr.h
        ${UIS} ${RSCS} ${TRS} ${MOCS})
    target_link_libraries(alsoft-config Qt5::Widgets)
    target_include_directories(alsoft-config PRIVATE "${alsoft-config_BINARY_DIR}"
        "${OpenAL_BINARY_DIR}")
    set_target_properties(alsoft-config PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${OpenAL_BINARY_DIR})
    if(TARGET build_version)
        add_dependencies(alsoft-config build_version)
    endif()
endif()
