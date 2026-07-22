cmake_minimum_required(VERSION 3.20)
project(ra3-registry-helper LANGUAGES CXX)

add_executable(ra3-registry-helper main.cpp)

target_compile_features(ra3-registry-helper PRIVATE cxx_std_17)
target_compile_definitions(ra3-registry-helper PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
  target_compile_options(ra3-registry-helper PRIVATE /W4 /utf-8)
  target_compile_options(ra3-registry-helper PRIVATE
    $<$<CONFIG:Release>:/O2 /Gy /GL /Gw>
  )
  target_link_options(ra3-registry-helper PRIVATE
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO>
  )
endif()

target_link_libraries(ra3-registry-helper PRIVATE advapi32 rpcrt4)
