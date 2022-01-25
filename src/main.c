/*

Copyright (c) 2015 Harm Hanemaaijer <fgenfb@yahoo.com>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

*/

/* Texture file converter. */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detex.h"

typedef enum FILE_TYPE {
    FILE_TYPE_NONE = 0,
    FILE_TYPE_KTX = 1,
    FILE_TYPE_DDS = 2,
    FILE_TYPE_TEX = 3,
} FILE_TYPE;

static FILE_TYPE get_extension(char* filename) {
    int filename_length = strlen(filename);
    if (filename_length > 4) {
        char ext[4] = {0};
        memcpy(ext, filename + filename_length - 4, 4);
        if (memcmp(ext, ".ktx", 4) == 0) {
            return FILE_TYPE_KTX;
        }
        if (memcmp(ext, ".dds", 4) == 0) {
            return FILE_TYPE_DDS;
        }
        if (memcmp(ext, ".tex", 4) == 0) {
            return FILE_TYPE_TEX;
        }
    }
    return FILE_TYPE_NONE;
}

static FILE_TYPE get_magic(char* filename) {
    FILE* file = fopen(filename, "rb");
    if (file) {
        char magic[4] = {0};
        fread(magic, 4, 1, file);
        fclose(file);
        if (memcmp(magic, "\xABKTX", 4) == 0) {
            return FILE_TYPE_KTX;
        }
        if (memcmp(magic, "DDS ", 4) == 0) {
            return FILE_TYPE_DDS;
        }
        if (memcmp(magic, "TEX\0", 4) == 0) {
            return FILE_TYPE_TEX;
        }
        return get_extension(filename);
    }
    return FILE_TYPE_NONE;
}

static uint32_t format_for_dds(uint32_t format) {
    detexTextureFileInfo const* info = detexLookupTextureFormatFileInfo(format);
    if (info == NULL || !info->dds_support) {
        format = detexGetPixelFormat(format);
    }
    switch (format) {
        case DETEX_PIXEL_FORMAT_BGR8:
        case DETEX_PIXEL_FORMAT_BGRX8:
        case DETEX_PIXEL_FORMAT_RGBX8:
            return DETEX_PIXEL_FORMAT_RGB8;
        case DETEX_PIXEL_FORMAT_BGRA8:
            return DETEX_PIXEL_FORMAT_RGBA8;
        case DETEX_PIXEL_FORMAT_RGB16:
        case DETEX_PIXEL_FORMAT_FLOAT_RGB16:
        case DETEX_PIXEL_FORMAT_FLOAT_RGBX16:
            return DETEX_PIXEL_FORMAT_FLOAT_RGB32;
    }
    return format;
}

static uint32_t format_for_ktx(uint32_t format) {
    detexTextureFileInfo const* info = detexLookupTextureFormatFileInfo(format);
    if (info == NULL || !info->ktx_support) {
        format = detexGetPixelFormat(format);
    }
    switch (format) {
        case DETEX_PIXEL_FORMAT_BGR8:
        case DETEX_PIXEL_FORMAT_BGRX8:
        case DETEX_PIXEL_FORMAT_RGBX8:
            return DETEX_PIXEL_FORMAT_RGB8;
        case DETEX_PIXEL_FORMAT_BGRA8:
            return DETEX_PIXEL_FORMAT_RGBA8;
        case DETEX_PIXEL_FORMAT_FLOAT_RGBX16:
            return DETEX_PIXEL_FORMAT_FLOAT_RGB16;
    }
    return format;
}

static uint32_t format_for_tex(uint32_t format) {
    switch (format) {
        case DETEX_TEXTURE_FORMAT_BC1:
        case DETEX_TEXTURE_FORMAT_BC2:
        case DETEX_TEXTURE_FORMAT_BC3:
            return format;
        default:
            format = detexGetPixelFormat(format);
            break;
    }
    if (detexFormatHasAlpha(format)) {
        return DETEX_PIXEL_FORMAT_BGRA8;
    } else {
        return DETEX_PIXEL_FORMAT_BGR8;
    }
}

static bool convert_textures(detexTexture** textures, int nu_levels, uint32_t (*selectFormat)(uint32_t in_format)) {
    for (int i = 0; i < nu_levels; ++i) {
        detexTexture* in_texture = textures[i];
        uint32_t out_format = selectFormat(in_texture->format);
        if (in_texture->format == out_format) {
            continue;
        }
        detexTexture* out_texture = (detexTexture*)malloc(sizeof(detexTexture));
        uint32_t size = detexGetPixelSize(out_format) * in_texture->width * in_texture->height;
        out_texture->data = (uint8_t*)malloc(size);
        if (!detexDecompressTextureLinear(in_texture, out_texture->data, out_format)) {
            free(out_texture->data);
            free(out_texture);
            return false;
        }
        out_texture->format = out_format;
        out_texture->width = in_texture->width;
        out_texture->height = in_texture->height;
        out_texture->width_in_blocks = in_texture->width;
        out_texture->height_in_blocks = in_texture->height;
        textures[i] = out_texture;
    }
    return true;
}

static bool read_textures(char* in_filename, detexTexture*** textures, int* nu_levels, FILE_TYPE in_file_type) {
    *textures = NULL;
    *nu_levels = 0;
    if (in_file_type == FILE_TYPE_NONE) {
        in_file_type = get_magic(in_filename);
    }
    switch (in_file_type) {
        case FILE_TYPE_KTX:
            if (!detexLoadKTXFileWithMipmaps(in_filename, 32, textures, nu_levels)) {
                return false;
            }
            break;
        case FILE_TYPE_DDS:
            if (!detexLoadDDSFileWithMipmaps(in_filename, 32, textures, nu_levels)) {
                return false;
            }
            break;
        case FILE_TYPE_TEX:
            if (!detexLoadTEXFileWithMipmaps(in_filename, 32, textures, nu_levels)) {
                return false;
            }
            break;
        default:
            detexSetErrorMessage("Invalid input file type %d", in_file_type);
            return false;
    }
    return true;
}

static bool write_textures(char* out_filename, detexTexture** textures, int nu_levels, FILE_TYPE out_file_type) {
    if (out_file_type == FILE_TYPE_NONE) {
        out_file_type = get_extension(out_filename);
    }
    switch (out_file_type) {
        case FILE_TYPE_KTX:
            if (!convert_textures(textures, nu_levels, &format_for_ktx)) {
                return false;
            }
            if (!detexSaveKTXFileWithMipmaps(textures, nu_levels, out_filename)) {
                return false;
            }
            break;
        case FILE_TYPE_DDS:
            if (!convert_textures(textures, nu_levels, &format_for_dds)) {
                return false;
            }
            if (!detexSaveDDSFileWithMipmaps(textures, nu_levels, out_filename)) {
                return false;
            }
            break;
        case FILE_TYPE_TEX:
            if (!convert_textures(textures, nu_levels, &format_for_tex)) {
                return false;
            }
            if (!detexSaveTEXFileWithMipmaps(textures, nu_levels, out_filename)) {
                return false;
            }
            break;
        default:
            detexSetErrorMessage("Invalid output file type %d", out_file_type);
            return false;
    }
    return true;
}

int main(int argc, char** argv) {
    int nu_levels = 0;
    detexTexture** textures = NULL;
    // Check arguments
    if (argc < 3) {
        fprintf(stderr, "Bad arguments: ritotex <INPUT_FILE> <OUTPUT_FILE>");
        return EXIT_FAILURE;
    }
    if (!read_textures(argv[1], &textures, &nu_levels, FILE_TYPE_NONE)) {
        fprintf(stderr, "Failed to read_textures: %s\n", detexGetErrorMessage());
        return EXIT_FAILURE;
    }
    if (!write_textures(argv[2], textures, nu_levels, FILE_TYPE_NONE)) {
        fprintf(stderr, "Failed to write_textures: %s\n", detexGetErrorMessage());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
