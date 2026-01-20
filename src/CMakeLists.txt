add_library(libdepgraph
        disk_graph.cpp
        buffer_graph.cpp
        gpu_graph.cu
        dependency_graph.cu
        package_loader.cpp
)

add_executable(console console.cpp)
target_link_libraries(console PRIVATE libdepgraph)