# cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1 .
# cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug
# cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -B .\build -DCMAKE_EXPORT_COMPILE_COMMANDS=1
# ninja

cmake_minimum_required(VERSION 3.16)

project(vg5kdither LANGUAGES C)


# libimagequant quantizer
# file(GLOB LIBIMAGEQUANT_SOURCES "${PROJECT_SOURCE_DIR}/exoquant/*.c")
# add_library(imquant STATIC
#     ${LIBIMAGEQUANT_SOURCES}
# )
# target_compile_options(imquant PRIVATE -Wno-attributes)

# tetrapal DEVICE_TEST_DIR
file(GLOB LIBTETRAPAL_SOURCES "${PROJECT_SOURCE_DIR}/tetrapal/*.c")
add_library(tetrapal STATIC
    ${LIBTETRAPAL_SOURCES}
)


# lib vg5k_dither
file(GLOB LIBVG5KDITHER_SOURCES "${PROJECT_SOURCE_DIR}/vg5k_dither_exoq.c")
add_library(vg5kditherexoq_lib STATIC
    ${LIBVG5KDITHER_SOURCES}
)

# lib vg5k_chars
file(GLOB LIBVG5KCHARS_SOURCES "${PROJECT_SOURCE_DIR}/vg5k_chars.c")
add_library(vg5kchars_lib STATIC
    ${LIBVG5KCHARS_SOURCES}
)

# lib vg5k_dither_yliluoma
file(GLOB LIBVG5KDITHERYLILUOMA_SOURCES "${PROJECT_SOURCE_DIR}/vg5k_dither_yliluoma2.c")
add_library(vg5kditheryliluoma_lib STATIC
    ${LIBVG5KDITHERYLILUOMA_SOURCES}
)

# lib vg5k_dither_tetrapal
file(GLOB LIBVG5KDITHERTETRAPAL_SOURCES "${PROJECT_SOURCE_DIR}/vg5k_dither_tetrapal.c")
add_library(vg5kdithertetrapal_lib STATIC
    ${LIBVG5KDITHERTETRAPAL_SOURCES}
)
target_include_directories(vg5kdithertetrapal_lib PRIVATE
    ${PROJECT_SOURCE_DIR}/tetrapal
)



add_executable(vg5kd vg5k_dither.c)
target_include_directories(vg5kd PRIVATE 
    ${PROJECT_SOURCE_DIR}/stb 
    ${PROJECT_SOURCE_DIR}/exoquant
)

# Lien avec les bibliothèques principales
target_link_libraries(vg5kd PRIVATE vg5kditherexoq_lib vg5kchars_lib vg5kditheryliluoma_lib vg5kdithertetrapal_lib tetrapal)


add_executable(vg5kdf vg5k_dither_fusion.c)
target_include_directories(vg5kdf PRIVATE
    ${PROJECT_SOURCE_DIR}/stb
    ${PROJECT_SOURCE_DIR}/exoquant
)
target_link_libraries(vg5kdf PRIVATE vg5kditherexoq_lib vg5kchars_lib vg5kditheryliluoma_lib vg5kdithertetrapal_lib tetrapal)


add_executable(dithery dither_8_colors_yliluoma.c)
target_include_directories(dithery PRIVATE
    ${PROJECT_SOURCE_DIR}/stb
)
target_link_libraries(dithery PRIVATE vg5kditheryliluoma_lib)




set(SOURCE_FILE_ORIGINAL "${PROJECT_SOURCE_DIR}/disp.c")
set(SOURCE_FILE_COPIED "${CMAKE_CURRENT_BINARY_DIR}/disp.c")
add_custom_command(
    OUTPUT "${SOURCE_FILE_COPIED}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SOURCE_FILE_ORIGINAL}" "${SOURCE_FILE_COPIED}"
    DEPENDS "${SOURCE_FILE_ORIGINAL}"
    COMMENT "Copie de la ressource disp.c dans le répertoire de build..."
    VERBATIM
)
add_custom_target(CopyResources DEPENDS "${SOURCE_FILE_COPIED}")
add_dependencies(vg5kd CopyResources)
