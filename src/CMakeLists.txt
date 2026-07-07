add_library(libxpgraph STATIC
        ${PROJECT_SOURCE_DIR}/thirdparty/pugixml/pugixml.cpp
        storage_graph.cpp
        buffer_graph.cpp
        data_model.cu
        cache_graph.cu
        xpgraph.cpp
        package_loader.cpp
)
set_target_properties(libxpgraph PROPERTIES POSITION_INDEPENDENT_CODE ON PREFIX "")

add_executable(console console.cpp)
target_link_libraries(console PRIVATE libxpgraph)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import sysconfig; print (sysconfig.get_config_var ('MULTIARCH') or '') "
        OUTPUT_VARIABLE PYTHON3_MULTIARCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_library(pyxpgraph MODULE python_binding.cpp)
target_include_directories(pyxpgraph PRIVATE ${Python3_INCLUDE_DIRS})
target_link_libraries(pyxpgraph PRIVATE libxpgraph Python3::Python)
target_compile_options(pyxpgraph PRIVATE -I/usr/include -I/usr/include/${PYTHON3_MULTIARCH})
set_target_properties(pyxpgraph PROPERTIES PREFIX "" OUTPUT_NAME "pyxpgraph"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/python)
