cmake_minimum_required(VERSION 3.28)   # imposé par vision.cpp (voir depend/visioncpp-src une fois récupéré)
project(PhotoClone VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets Svg)
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs photo)
find_package(OpenMP)
find_package(Threads REQUIRED)

# ============================================================================================================
# Fonctionnalités IA optionnelles : compilées à partir des SOURCES upstream, récupérées par ./depend/fetch-sources.sh
# (jamais commitées : voir depend/README.md). Absentes du dépôt Git par conception — un dépôt GitHub ne doit pas
# contenir de binaires .a/.so. Sans elles, PhotoClone compile et s'exécute quand même : les commandes IA du menu
# restent visibles mais indiquent que la fonctionnalité est indisponible.
# ============================================================================================================

# --- vision.cpp (https://github.com/Acly/vision.cpp) : lié STATIQUEMENT, comme n'importe quelle dépendance CMake ---
option(PC_USE_VISIONCPP "Activer les fonctions IA de vision.cpp (nécessite depend/visioncpp-src, voir fetch-sources.sh)" ON)
set(VISIONCPP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/depend/visioncpp-src)
set(PC_HAVE_VISIONCPP FALSE)
if(PC_USE_VISIONCPP AND EXISTS ${VISIONCPP_SRC}/CMakeLists.txt)
  set(PC_HAVE_VISIONCPP TRUE)
  message(STATUS "vision.cpp trouvé : compilation depuis les sources (${VISIONCPP_SRC})")

  # Réglages forcés AVANT add_subdirectory (respectés par les option() du sous-projet tant qu'ils sont posés en CACHE) :
  # bibliothèques statiques, base d'instructions portable x86_64 (Haswell 2013+, PAS -march=native qui plante sur un
  # processeur plus ancien que la machine de compilation — voir depend/README.md).
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(VISP_STATIC_GGML ON CACHE BOOL "" FORCE)
  set(VISP_TESTS OFF CACHE BOOL "" FORCE)
  set(VISP_VULKAN OFF CACHE BOOL "" FORCE)             # activer pour un build GPU (voir depend/README.md)
  set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
  foreach(feat SSE42 AVX AVX2 F16C BMI2 FMA)           # ~ Haswell et plus récent
    set(GGML_${feat} ON CACHE BOOL "" FORCE)
  endforeach()
  add_subdirectory(${VISIONCPP_SRC} ${CMAKE_BINARY_DIR}/visioncpp-build EXCLUDE_FROM_ALL)
  if(TARGET vision-cli)
    set_target_properties(vision-cli PROPERTIES EXCLUDE_FROM_ALL TRUE)   # exécutable de démo upstream : pas besoin de le construire
  endif()
else()
  message(STATUS "vision.cpp absent (${VISIONCPP_SRC}) : lancez ./depend/fetch-sources.sh pour l'obtenir — fonctionnalités IA compilées mais indisponibles à l'exécution")
endif()

# --- stable-diffusion.cpp (https://github.com/leejet/stable-diffusion.cpp) ---
# Embarque SA PROPRE copie de ggml (version différente de celle de vision.cpp ci-dessus) : add_subdirectory le lierait
# statiquement dans le même binaire et provoquerait des symboles ggml en double. Solution : le compiler comme
# bibliothèque PARTAGÉE isolée (libpcsd.so), avec un script de version (depend/stablediffusioncpp/pcsd.map, un
# fichier À NOUS, committé) qui ne laisse passer que l'API C dont PhotoClone a besoin — zéro symbole ggml exporté.
# PhotoClone la charge à l'exécution (dlopen/QLibrary) : jamais liée au moment de l'édition de liens, donc jamais en
# conflit avec le ggml de vision.cpp, quelle que soit sa version. Voir depend/README.md pour le détail.
option(PC_USE_SDCPP "Activer Stable Diffusion (nécessite depend/stablediffusioncpp-src, voir fetch-sources.sh)" ON)
set(SDCPP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/depend/stablediffusioncpp-src)
set(SDCPP_OWN ${CMAKE_CURRENT_SOURCE_DIR}/depend/stablediffusioncpp)
set(PC_HAVE_SDCPP FALSE)
if(PC_USE_SDCPP AND EXISTS ${SDCPP_SRC}/CMakeLists.txt)
  set(PC_HAVE_SDCPP TRUE)
  message(STATUS "stable-diffusion.cpp trouvé : compilation isolée depuis les sources (${SDCPP_SRC})")
  include(ExternalProject)
  ExternalProject_Add(sdcpp_build
    SOURCE_DIR ${SDCPP_SRC}
    BINARY_DIR ${CMAKE_BINARY_DIR}/sdcpp-build
    CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=Release
      -DSD_BUILD_EXAMPLES=OFF -DSD_WEBP=OFF -DSD_WEBM=OFF
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      -DGGML_NATIVE=OFF -DGGML_SSE42=ON -DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_F16C=ON -DGGML_BMI2=ON -DGGML_FMA=ON
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel 1   # sdcpp compile quelques fichiers volumineux (tokenizers) qui peuvent saturer la RAM en parallèle sur une petite machine ; augmentez si vous avez plusieurs Go de marge
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS
      <BINARY_DIR>/libstable-diffusion.a
      <BINARY_DIR>/ggml/src/libggml.a
      <BINARY_DIR>/ggml/src/libggml-cpu.a
      <BINARY_DIR>/ggml/src/libggml-base.a
  )
  ExternalProject_Get_Property(sdcpp_build BINARY_DIR)   # -> variable BINARY_DIR (chemin réel, résolu) dans CE scope

  set(PCSD_LIB ${SDCPP_OWN}/lib/libpcsd.so)
  add_custom_command(
    OUTPUT ${PCSD_LIB}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SDCPP_OWN}/lib
    COMMAND ${CMAKE_CXX_COMPILER} -shared -fPIC -o ${PCSD_LIB}
            -Wl,--whole-archive ${BINARY_DIR}/libstable-diffusion.a -Wl,--no-whole-archive
            ${BINARY_DIR}/ggml/src/libggml.a ${BINARY_DIR}/ggml/src/libggml-cpu.a ${BINARY_DIR}/ggml/src/libggml-base.a
            -Wl,--version-script=${SDCPP_OWN}/pcsd.map -Wl,-Bsymbolic -Wl,-soname,libpcsd.so -fopenmp -lpthread -ldl
    COMMAND ${CMAKE_STRIP} --strip-unneeded ${PCSD_LIB}
    DEPENDS sdcpp_build ${SDCPP_OWN}/pcsd.map
    COMMENT "Édition de liens de libpcsd.so (stable-diffusion.cpp, symboles masqués sauf l'API C — voir depend/README.md)"
    VERBATIM
  )
  add_custom_target(pcsd_shared ALL DEPENDS ${PCSD_LIB})
else()
  message(STATUS "stable-diffusion.cpp absent (${SDCPP_SRC}) : lancez ./depend/fetch-sources.sh pour l'obtenir — commandes Stable Diffusion compilées mais indisponibles")
endif()

# Tout le code applicatif dans une bibliothèque (réutilisée par l'exécutable et les tests)
file(GLOB_RECURSE PC_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
list(REMOVE_ITEM PC_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
add_library(photoclone_lib STATIC ${PC_SOURCES})
target_include_directories(photoclone_lib PUBLIC src ${OpenCV_INCLUDE_DIRS})
target_link_libraries(photoclone_lib PUBLIC Qt5::Widgets Qt5::Svg ${OpenCV_LIBS})
if(OpenMP_CXX_FOUND)
  target_link_libraries(photoclone_lib PUBLIC OpenMP::OpenMP_CXX)
endif()
if(PC_HAVE_VISIONCPP)
  target_link_libraries(photoclone_lib PUBLIC visioncpp)
  target_compile_definitions(photoclone_lib PUBLIC PC_HAVE_VISIONCPP=1)
endif()
if(PC_HAVE_SDCPP)
  add_dependencies(photoclone_lib pcsd_shared)   # ordonnancement seulement : pas d'édition de liens, chargée via dlopen à l'exécution
  target_include_directories(photoclone_lib PUBLIC ${SDCPP_SRC}/include)
  target_compile_definitions(photoclone_lib PUBLIC PC_HAVE_SDCPP=1 "PC_SD_LIB_PATH=\"${PCSD_LIB}\"")
  target_link_libraries(photoclone_lib PUBLIC ${CMAKE_DL_LIBS})
endif()
target_compile_options(photoclone_lib PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(PhotoClone src/main.cpp)
target_link_libraries(PhotoClone PRIVATE photoclone_lib)
install(TARGETS PhotoClone RUNTIME DESTINATION bin)
if(PC_HAVE_SDCPP)
  install(FILES ${PCSD_LIB} DESTINATION lib)   # bibliothèque chargée à l'exécution : à installer à côté du binaire
endif()

option(PC_BUILD_TESTS "Compiler le test de fumée" ON)
if(PC_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_test.cpp)
  add_executable(smoke_test tests/smoke_test.cpp)
  target_link_libraries(smoke_test PRIVATE photoclone_lib)
endif()
