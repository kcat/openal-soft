project(alsoft-config)

if(Qt6Widgets_FOUND)
    qt6_wrap_ui(UIS  mainwindow.ui)

    qt6_wrap_cpp(MOCS  mainwindow.h)

    add_executable(alsoft-config
        main.cpp
        mainwindow.cpp
        mainwindow.h
        verstr.cpp
        verstr.h
        ${UIS} ${RSCS} ${TRS} ${MOCS})
    target_link_libraries(alsoft-config PUBLIC Qt6::Widgets PRIVATE alsoft.common)
    target_include_directories(alsoft-config PRIVATE "${alsoft-config_BINARY_DIR}"
        "${OpenAL_BINARY_DIR}")
    target_compile_definitions(alsoft-config PRIVATE QT_NO_KEYWORDS)
    if(TARGET alsoft.build_version)
        add_dependencies(alsoft-config alsoft.build_version)
    endif()
endif()
