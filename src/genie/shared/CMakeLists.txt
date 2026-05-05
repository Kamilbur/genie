project("genie-shared")

add_library(genie-shared SHARED api.cc)

target_link_libraries(genie-shared PUBLIC genie-core)
target_link_libraries(genie-shared PUBLIC genie-mgb)
target_link_libraries(genie-shared PUBLIC genie-fastq)
target_link_libraries(genie-shared PUBLIC genie-fasta)
target_link_libraries(genie-shared PUBLIC genie-util)
target_link_libraries(genie-shared PUBLIC genie-refcoder)
target_link_libraries(genie-shared PUBLIC genie-localassembly)
target_link_libraries(genie-shared PUBLIC genie-lowlatency)
target_link_libraries(genie-shared PUBLIC genie-spring)
target_link_libraries(genie-shared PUBLIC genie-calq)
target_link_libraries(genie-shared PUBLIC genie-nametoken)
target_link_libraries(genie-shared PUBLIC genie-namewriteout)
target_link_libraries(genie-shared PUBLIC genie-gabac)
target_link_libraries(genie-shared PUBLIC genie-lzma)
target_link_libraries(genie-shared PUBLIC genie-zstd)
target_link_libraries(genie-shared PUBLIC genie-bsc)
target_link_libraries(genie-shared PUBLIC genie-paramcabac)
target_link_libraries(genie-shared PUBLIC genie-paramqv1)

if (${GENIE_USE_OPENMP})
    target_compile_definitions(genie-shared PRIVATE GENIE_USE_OPENMP)
    target_link_libraries(genie-shared PUBLIC ${OpenMP_CXX_LIBRARIES})
endif ()

get_filename_component(TOP_DIR ../../ ABSOLUTE)
target_include_directories(genie-shared PUBLIC "${TOP_DIR}")

set_target_properties(genie-shared PROPERTIES
        OUTPUT_NAME genie
        PUBLIC_HEADER api.h
        INSTALL_RPATH_USE_LINK_PATH TRUE)

install(TARGETS genie-shared
        LIBRARY DESTINATION "/usr/local/lib"
        PUBLIC_HEADER DESTINATION "/usr/local/include/genie/shared")
