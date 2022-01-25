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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detex.h"

// NOBODY CARES ABOUT BIG ENDIAN
static const char KTX_MAGIC[16] = "\xAB\x4B\x54\x58\x20\x31\x31\xBB\x0D\x0A\x1A\x0A\x01\x02\x03\x04";

typedef struct {
    uint32_t glType;                // 04 glType
    uint32_t typeSize;              // 05
    uint32_t glFormat;              // 06 glFormat
    uint32_t glInternalFormat;      // 07 glInternalFormat
    uint32_t glBaseInternalFormat;  // 08
    uint32_t width;                 // 09 width
    uint32_t height;                // 10 height
    uint32_t depth;                 // 11
    uint32_t nu_elements;           // 12
    uint32_t nu_faces;              // 13
    uint32_t nu_mipmaps;            // 14 nu_file_mipmaps
    uint32_t metada_size;           // 15
} KTX_HEADER;

// Load texture from KTX file with mip-maps. Returns true if successful.
// nu_mipmaps is a return parameter that returns the number of mipmap levels found.
// textures_out is a return parameter for an array of detexTexture pointers that is allocated,
// free with free(). textures_out[i] are allocated textures corresponding to each level, free
// with free();
bool detexFileLoadKTX(const char *filename, int max_mipmaps, detexTexture ***textures_out, int *nu_levels_out) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        detexSetErrorMessage("detexLoadKTXFileWithMipmaps: Could not open KTX file %s", filename);
        return false;
    }

    char magic[16];
    if (fread(magic, 1, 16, file) != 16 || memcmp(magic, KTX_MAGIC, 16) != 0) {
        detexSetErrorMessage("detexLoadKTXFileWithMipmaps: Couldn't find KTX signature");
        return false;
    }

    KTX_HEADER header;
    if (fread(&header, 1, sizeof(KTX_HEADER), file) != sizeof(KTX_HEADER)) {
        detexSetErrorMessage("detexLoadKTXFileWithMipmaps: Error reading KTX header %s", filename);
        return false;
    }

    const detexTextureFileInfo *info = detexLookupKTXFileInfo(header.glInternalFormat, header.glFormat, header.glType);
    if (info == NULL) {
        detexSetErrorMessage(
            "detexLoadKTXFileWithMipmaps: Unsupported format in .ktx file "
            "(glInternalFormat = 0x%04X)",
            header.glInternalFormat);
        return false;
    }

    if (fseek(file, header.metada_size, SEEK_CUR) != 0) {
        detexSetErrorMessage("detexLoadKTXFileWithMipmaps: Error reading KTX metadata %s", filename);
        return false;
    }

    int nu_mipmaps = min(header.nu_mipmaps, max_mipmaps);
    *nu_levels_out = nu_mipmaps;

    detexTexture **textures = (detexTexture **)calloc(nu_mipmaps, sizeof(detexTexture *));
    *textures_out = textures;

    uint32_t bytes_per_block = detextBytesPerBlock(info->texture_format);
    uint32_t block_width = info->block_width;
    uint32_t block_height = info->block_height;
    uint32_t current_width = header.width;
    uint32_t current_height = header.height;
    for (int i = 0; i < nu_mipmaps; i++) {
        uint32_t correct_size;
        if (fread(&correct_size, 1, 4, file) != 4) {
            detexSetErrorMessage("detexLoadKTXFileWithMipmaps: Error reading KTX mipmap size %s", filename);
            return false;
        }
        uint32_t width_in_blocks = max((current_width + block_width - 1) / block_width, 1);
        uint32_t height_in_blocks = max((current_height + block_height - 1) / block_height, 1);
        uint32_t size = width_in_blocks * height_in_blocks * bytes_per_block;
        if (size != correct_size) {
            detexSetErrorMessage(
                "detexLoadKTXFileWithMipmaps: Error loading file %s: "
                "Image size field of mipmap level %d should be %u but is %u",
                filename,
                i,
                correct_size,
                size);
            return false;
        }
        // Allocate texture.
        textures[i] = (detexTexture *)malloc(sizeof(detexTexture));
        *textures[i] = (detexTexture){
            .format = info->texture_format,
            .data = (uint8_t *)malloc(size),
            .width = current_width,
            .height = current_height,
            .width_in_blocks = width_in_blocks,
            .height_in_blocks = height_in_blocks,
        };
        if (fread(textures[i]->data, 1, size, file) != size) {
            detexSetErrorMessage("detexLoadKTXFileWithMipmaps: Error reading file %s", filename);
            return false;
        }
        // Divide by two for the next mipmap level, rounding down.
        current_width >>= 1;
        current_height >>= 1;
        uint32_t unaligned = size % 4;
        if (unaligned > 0) {
            fseek(file, 4 - unaligned, SEEK_CUR);
        }
    }
    fclose(file);
    return true;
}

// Save textures to KTX file (multiple mip-maps levels). Return true if succesful.
bool detexFileSaveKTX(const char *filename, detexTexture **textures, int nu_levels) {
    /*
    FILE *f = fopen(filename, "wb");
    if (f == NULL) {
        detexSetErrorMessage("detexSaveKTXFileWithMipmaps: Could not open file %s for writing", filename);
        return false;
    }
    uint32_t header[16];
    memset(header, 0, 64);
    memcpy(header, ktx_id, 12);  // Set id.
    header[3] = 0x04030201;
    const detexTextureFileInfo *info = detexLookupTextureFormatFileInfo(textures[0]->format);
    if (info == NULL) {
        detexSetErrorMessage("detexSaveKTXFileWithMipmaps: Could not match texture format with file format");
        return false;
    }
    if (!info->ktx_support) {
        detexSetErrorMessage("detexSaveKTXFileWithMipmaps: Could not match texture format with KTX file format");
        return false;
    }
    int glType = 0;
    int glTypeSize = 1;
    int glFormat = 0;
    glType = info->gl_type;
    glFormat = info->gl_format;
    int glInternalFormat = info->gl_internal_format;
    header[4] = glType;      // glType
    header[5] = glTypeSize;  // glTypeSize
    header[6] = glFormat;    // glFormat
    header[7] = glInternalFormat;
    header[9] = textures[0]->width;
    header[10] = textures[0]->height;
    header[11] = 0;
    header[13] = 1;          // Number of faces.
    header[14] = nu_levels;  // Mipmap levels.
    header[15] = 0;
    size_t r = fwrite(header, 1, 64, f);
    if (r != 64) {
        detexSetErrorMessage("detexSaveKTXFileWithMipmaps: Error writing to file %s", filename);
        return false;
    }

    int data[1] = {0};
    for (int i = 0; i < nu_levels; i++) {
        uint32_t pixel_size = detexGetPixelSize(textures[i]->format);
        // Block size is block size for compressed textures and the pixel size for
        // uncompressed textures.
        int n;
        int block_size;
        if (detexFormatIsCompressed(textures[i]->format)) {
            n = textures[i]->width_in_blocks * textures[i]->height_in_blocks;
            block_size = detexGetCompressedBlockSize(textures[i]->format);
        } else {
            n = textures[i]->width * textures[i]->height;
            block_size = pixel_size;
        }
        //		if (!option_quiet)
        //			printf("Writing mipmap level %d of size %d x %d.\n", i, textures[i]->width, textures[i]->height);
        // Because of per row 32-bit alignment is mandated by the KTX specification, we have to handle
        // special cases of unaligned uncompressed textures.
        if (detexFormatIsCompressed(textures[i]->format) || (pixel_size & 3) == 0) {
            // Regular 32-bit aligned texture.
            data[0] = n * block_size;  // Image size.
            size_t r1 = fwrite(data, 1, 4, f);
            size_t r2 = fwrite(textures[i]->data, 1, n * block_size, f);
            if (r1 != 4 || r2 != n * block_size) {
                detexSetErrorMessage("detexSaveKTXFileWithMipmaps: Error writing to file %s", filename);
                return false;
            }
        } else {
            // Uncompressed texture with pixel size that is not a multiple of four.
            int row_size = (textures[i]->width * pixel_size + 3) & (~3);
            data[0] = textures[i]->height * row_size;  // Image size.
            size_t r1 = fwrite(data, 1, 4, f);
            if (r1 != 4) {
                detexSetErrorMessage("detexSaveKTXFileWithMipmaps: Error writing to file %s", filename);
                return false;
            }
            uint8_t *row = (uint8_t *)malloc(row_size);
            for (int y = 0; y < textures[i]->height; y++) {
                memcpy(row, &textures[i]->data[y * textures[i]->width * pixel_size], textures[i]->width * pixel_size);
                for (int j = textures[i]->width * pixel_size; j < row_size; j++) row[j] = 0;
                size_t r2 = fwrite(row, 1, row_size, f);
                if (r2 != row_size) {
                    detexSetErrorMessage("detexSaveKTXFileWithMipmaps: Error writing to file %s", filename);
                    return false;
                }
            }
            free(row);
        }
    }
    fclose(f);
    */
    return true;
}
