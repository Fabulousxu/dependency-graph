add_library(libdepgraph
        dependency_graph.cpp
        gpu_dependency_graph.cu
        package_loader.cpp
)

add_executable(console console.cpp)
target_link_libraries(console libdepgraph)
