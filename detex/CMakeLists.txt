cmake_minimum_required(VERSION 3.20)

project(detex)

add_library(detex STATIC
    include/detex.h
    src/bits.c
    src/bptc-tables.c
    src/clamp.c
    src/convert.c
    src/dds.c
    src/decompress-bc.c
    src/decompress-bptc.c
    src/decompress-bptc-float.c
    src/decompress-eac.c
    src/decompress-etc.c
    src/decompress-rgtc.c
    src/division-tables.c
    src/file-info.c
    src/half-float.c
    src/hdr.c
    src/ktx.c
    src/misc.c
    src/raw.c
    src/tex.c
    src/texture.c
)
target_include_directories(detex PUBLIC include/)
