add_library(libxpgraph STATIC
        ${PROJECT_SOURCE_DIR}/thirdparty/pugixml/pugixml.cpp
        config.cu
        write_buffer.cpp
        cuda_cache.cu
        mgxpgraph.cpp
        xpgraph.cpp
        package_loader.cpp
)
target_include_directories(libxpgraph PRIVATE ${PROJECT_SOURCE_DIR}/thirdparty/pugixml)
target_link_libraries(libxpgraph PUBLIC mgclient_cpp)
set_target_properties(libxpgraph PROPERTIES POSITION_INDEPENDENT_CODE ON PREFIX "")

add_executable(console console.cpp)
target_link_libraries(console PRIVATE libxpgraph)

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
add_library(pyxpgraph MODULE python_binding.cpp)
target_include_directories(pyxpgraph PRIVATE ${Python3_INCLUDE_DIRS})
target_link_libraries(pyxpgraph PRIVATE libxpgraph Python3::Python)
set_target_properties(pyxpgraph PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/python)

add_library(qmxpgraph SHARED query_module.cpp config.cu)
set_target_properties(qmxpgraph PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/query_modules
)
