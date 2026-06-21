add_library(libxpackagegraph STATIC storage_graph.cpp buffer_graph.cpp cache_graph.cu x_package_graph.cpp package_loader.cpp)
set_target_properties(libxpackagegraph PROPERTIES POSITION_INDEPENDENT_CODE ON PREFIX "")

add_executable(console console.cpp)
target_link_libraries(console PRIVATE libxpackagegraph)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import sysconfig; print (sysconfig.get_config_var ('MULTIARCH') or '') "
        OUTPUT_VARIABLE PYTHON3_MULTIARCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_library(pyxpackagegraph MODULE python_binding.cpp)
target_include_directories(pyxpackagegraph PRIVATE ${Python3_INCLUDE_DIRS})
target_link_libraries(pyxpackagegraph PRIVATE libxpackagegraph Python3::Python)
target_compile_options(pyxpackagegraph PRIVATE -I/usr/include -I/usr/include/${PYTHON3_MULTIARCH})
set_target_properties(pyxpackagegraph PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/python)
