add_library(libdepgraph
        storage_graph.cpp
        buffer_graph.cpp
        cache_graph.cu
        dependency_graph.cpp
        package_loader.cpp
)

add_executable(console console.cpp)
target_link_libraries(console PRIVATE libdepgraph)