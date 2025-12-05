# src/CMakeLists.txt

set(CMAKE_AUTOMOC ON)

set(SOURCES
    main.cpp
    MainWindow.cpp
    SerialHandler.cpp
    PlotManager.cpp
    DDSGenerator.cpp
    DigitalIO.cpp
    WaveformExporter.cpp
    qcustomplot.cpp
)

set(HEADERS
    MainWindow.h
    SerialHandler.h
    PlotManager.h
    DDSGenerator.h
    DigitalIO.h
    WaveformExporter.h
    qcustomplot.h
)

# Add Windows icon resource for the executable
if(WIN32)
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_SOURCE_DIR}/../ChatGPT_Image_Jul_15__2025__02_05_05_PM-removebg-preview.ico")
    add_executable(scope_app WIN32 ${SOURCES} ${HEADERS} ${APP_ICON_RESOURCE_WINDOWS})
else()
    add_executable(scope_app ${SOURCES} ${HEADERS})
endif()

target_link_libraries(scope_app PRIVATE Qt6::Widgets Qt6::SerialPort Qt6::PrintSupport)
# If you add QCustomPlot as a static lib, link it here as well
# target_link_libraries(QtOscilloscope PRIVATE QCustomPlot) 