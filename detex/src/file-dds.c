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

#define DDS_HEADER_FLAGS_TEXTURE 0x00001007     // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT
#define DDS_HEADER_FLAGS_PTICH 0x08
#define DDS_HEADER_FLAGS_LINEARSIZE 0x00080000  // DDSD_LINEARSIZE
#define DDS_HEADER_FLAGS_MIPMAP 0x00020000      // DDSD_MIPMAPCOUNT

#define DDS_FOURCC 0x00000004  // DDPF_FOURCC

#define DDS_SURFACE_FLAGS_TEXTURE 0x00001000  // DDSCAPS_TEXTURE
#define DDS_SURFACE_FLAGS_MIPMAP 0x00400008   // DDSCAPS_COMPLEX | DDSCAPS_MIPMAP

typedef struct {
    uint32_t size;         // 072
    uint32_t flags;        // 076
    char fourCC[4];        // 080
    uint32_t bitCountRGB;  // 084
    uint32_t bitMaskR;     // 088
    uint32_t bitMaskG;     // 092
    uint32_t bitMaskB;     // 096
    uint32_t bitMaskA;     // 100
} DDS_PIXELFORMAT;

typedef struct {
    uint32_t format;
    uint32_t resource_dimension;
    uint32_t array_size;
    uint32_t unk[2];
} DX10_HEADER;

typedef struct {
    uint32_t size;                // 000
    uint32_t flags;               // 004
    uint32_t height;              // 008
    uint32_t width;               // 012
    uint32_t pitchOrLinearSize;   // 016
    uint32_t depth;               // 020
    uint32_t mipMapCount;         // 024
    uint32_t reserved1[11];       // 028
    DDS_PIXELFORMAT pixelFormat;  // 072
    uint32_t caps;                // 104
    uint32_t caps2;               // 108
    uint32_t caps3;               // 112
    uint32_t caps4;               // 116
    uint32_t reserved2;           // 120
} DDS_HEADER;

// Load texture from DDS file with mip-maps. Returns true if successful.
// nu_levels is a return parameter that returns the number of mipmap levels found.
// textures_out is a return parameter for an array of detexTexture pointers that is allocated,
// free with free(). textures_out[i] are allocated textures corresponding to each level, free
// with free();
bool detexFileLoadDDS(const char *filename, int max_mipmaps, detexTexture ***textures_out, int *nu_levels_out) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        detexSetErrorMessage("detexLoadDDSFileWithMipmaps: Could not open file %s", filename);
        return false;
    }

    DDS_HEADER header = {0};
    DX10_HEADER dx10_header = {0};

    char magic[4];
    if (fread(magic, 1, 4, file) != 4 || memcmp(magic, "DDS ", 4) != 0) {
        detexSetErrorMessage("detexLoadDDSFileWithMipmaps: Couldn't find DDS signature");
        return false;
    }

    if (fread(&header, 1, sizeof(DDS_HEADER), file) != sizeof(DDS_HEADER)) {
        detexSetErrorMessage("detexLoadDDSFileWithMipmaps: Error reading DDS file header %s", filename);
        return false;
    }

    if (strncmp(header.pixelFormat.fourCC, "DX10", 4) == 0) {
        if (fread(&dx10_header, 1, sizeof(DX10_HEADER), file) != sizeof(DX10_HEADER)) {
            detexSetErrorMessage("detexLoadDDSFileWithMipmaps: Error reading DX10 header %s", filename);
            return false;
        }
        if (dx10_header.resource_dimension != 3) {
            detexSetErrorMessage("detexLoadDDSFileWithMipmaps: Only 2D textures supported for .dds files");
            return false;
        }
    }

    const detexTextureFileInfo *info = detexLookupDDSFileInfo(header.pixelFormat.fourCC,
                                                              dx10_header.format,
                                                              header.pixelFormat.flags,
                                                              header.pixelFormat.bitCountRGB,
                                                              header.pixelFormat.bitMaskR,
                                                              header.pixelFormat.bitMaskG,
                                                              header.pixelFormat.bitMaskB,
                                                              header.pixelFormat.bitMaskA);
    if (info == NULL) {
        detexSetErrorMessage("detexLoadDDSFileWithMipmaps: Unsupported format in .dds file (DX10 format = %d).",
                             dx10_header.format);
        return false;
    }

    int nu_file_mipmaps = (header.flags & DDS_HEADER_FLAGS_MIPMAP) ? header.mipMapCount : 1;
    int nu_mipmaps = min(nu_file_mipmaps, max_mipmaps);
    *nu_levels_out = nu_mipmaps;

    detexTexture **textures = (detexTexture **)calloc(nu_mipmaps, sizeof(detexTexture *));
    *textures_out = textures;

    uint32_t bytes_per_block = detextBytesPerBlock(info->texture_format);
    uint32_t block_width = info->block_width;
    uint32_t block_height = info->block_height;
    uint32_t current_width = header.width;
    uint32_t current_height = header.height;
    for (int i = 0; i < nu_mipmaps; i++) {
        uint32_t width_in_blocks = max((current_width + block_width - 1) / block_width, 1);
        uint32_t height_in_blocks = max((current_height + block_height - 1) / block_height, 1);
        uint32_t size = width_in_blocks * height_in_blocks * bytes_per_block;
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
            detexSetErrorMessage("detexLoadDDSFileWithMipmaps: Error reading file %s", filename);
            return false;
        }
        // Divide by two for the next mipmap level, rounding down.
        current_width >>= 1;
        current_height >>= 1;
    }
    fclose(file);
    return true;
}

// Save textures to DDS file (multiple mip-maps levels). Return true if succesful.
bool detexFileSaveDDS(const char *filename, detexTexture **textures, int nu_levels) {
    const detexTextureFileInfo *info = detexLookupTextureFormatFileInfo(textures[0]->format);

    if (info == NULL || !info->dds_support) {
        detexSetErrorMessage("detexSaveDDSFileWithMipmaps: Could not match texture format with DDS file format");
        return false;
    }

    DDS_HEADER header = {
        .size = sizeof(DDS_HEADER),
        .flags = DDS_HEADER_FLAGS_TEXTURE,
        .width = textures[0]->width,
        .height = textures[0]->height,
        .mipMapCount = nu_levels,
        .pixelFormat =
            (DDS_PIXELFORMAT){
                .size = sizeof(DDS_PIXELFORMAT),
            },
        .caps = 0x1000,
    };

    DX10_HEADER dx10_header = {
        .format = info->dx10_format,
        .resource_dimension = 3,
        .array_size = 1,
    };

    if (nu_levels > 1) {
        header.flags |= DDS_HEADER_FLAGS_MIPMAP;
        header.caps |= 0x400008;
    }

    if (!detexFormatIsCompressed(info->texture_format)) {
        header.flags |= DDS_HEADER_FLAGS_PTICH;
        header.pitchOrLinearSize = textures[0]->width * detexGetPixelSize(info->texture_format);

        uint64_t red_mask, green_mask, blue_mask, alpha_mask;
        detexGetComponentMasks(info->texture_format, &red_mask, &green_mask, &blue_mask, &alpha_mask);

        uint32_t component_size = detexGetComponentSize(info->texture_format);
        uint32_t nu_components = detexGetNumberOfComponents(info->texture_format);

        header.pixelFormat.bitCountRGB = nu_components * component_size * 8;
        header.pixelFormat.flags |= 0x40;
        header.pixelFormat.bitMaskR = (uint32_t)red_mask;
        header.pixelFormat.bitMaskG = (uint32_t)green_mask;
        header.pixelFormat.bitMaskB = (uint32_t)blue_mask;
        header.pixelFormat.bitMaskA = (uint32_t)alpha_mask;

        if (detexFormatHasAlpha(info->texture_format)) {
            header.pixelFormat.flags |= 0x01;
        }
    } else {
        header.flags |= DDS_HEADER_FLAGS_LINEARSIZE;
        header.pitchOrLinearSize = detexGetCompressedBlockSize(info->texture_format) *
                                   (textures[0]->width_in_blocks * textures[0]->height_in_blocks);
    }

    int dx_four_cc_length = strlen(info->dx_four_cc);
    if (dx_four_cc_length) {
        header.pixelFormat.flags |= DDS_FOURCC;
        memcpy(header.pixelFormat.fourCC, info->dx_four_cc, dx_four_cc_length);
    }

    FILE *file = fopen(filename, "wb");

    if (file == NULL) {
        detexSetErrorMessage("detexSaveDDSFileWithMipmaps: Could not open file %s for writing", filename);
        return false;
    }

    fwrite("DDS ", 1, 4, file);
    fwrite(&header, 1, sizeof(DDS_HEADER), file);
    if (strncmp(info->dx_four_cc, "DX10", 4) == 0) {
        fwrite(&dx10_header, 1, sizeof(DX10_HEADER), file);
    }

    uint32_t bytes_per_block = detextBytesPerBlock(info->texture_format);
    for (int i = 0; i < nu_levels; i++) {
        uint32_t size = textures[i]->width_in_blocks * textures[i]->height_in_blocks * bytes_per_block;
        fwrite(textures[i]->data, 1, size, file);
    }

    fclose(file);

    return true;
}
