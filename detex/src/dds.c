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
#define DDS_HEADER_FLAGS_LINEARSIZE 0x00080000  // DDSD_LINEARSIZE
#define DDS_HEADER_FLAGS_MIPMAP 0x00020000      // DDSD_MIPMAPCOUNT

#define DDS_FOURCC 0x00000004  // DDPF_FOURCC

#define DDS_SURFACE_FLAGS_TEXTURE 0x00001000  // DDSCAPS_TEXTURE
#define DDS_SURFACE_FLAGS_MIPMAP 0x00400008   // DDSCAPS_COMPLEX | DDSCAPS_MIPMAP

#define DDS_MAGIC ((uint8_t[4]){'D', 'D', 'S', ' '})

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
    uint32_t unk[3];
} DX10_HEADER;

typedef struct {
    char magic[4];                // 000
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
bool detexLoadDDSFileWithMipmaps(const char *filename,
                                 int max_mipmaps,
                                 detexTexture ***textures_out,
                                 int *nu_levels_out) {
    FILE *f = fopen(filename, "rb");
    if (f == NULL) {
        detexSetErrorMessage("detexLoadDDSFileWithMipmaps: Could not open file %s", filename);
        return false;
    }

    DDS_HEADER header = {0};
    DX10_HEADER dx10_header = {0};

    if (fread(&header, 1, sizeof(DDS_HEADER), f) != sizeof(DDS_HEADER)) {
        detexSetErrorMessage("detexLoadDDSFileWithMipmaps: Error reading DDS file header %s", filename);
        return false;
    }

    if (memcmp(header.magic, "DDS ", 4) != 0) {
        detexSetErrorMessage("detexLoadDDSFileWithMipmaps: Couldn't find DDS signature");
        return false;
    }

    if (strncmp(header.pixelFormat.fourCC, "DX10", 4) == 0) {
        if (fread(&dx10_header, 1, sizeof(DX10_HEADER), f) != sizeof(DX10_HEADER)) {
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
        if (fread(textures[i]->data, 1, size, f) != size) {
            detexSetErrorMessage("detexLoadDDSFileWithMipmaps: Error reading file %s", filename);
            return false;
        }
        // Divide by two for the next mipmap level, rounding down.
        current_width >>= 1;
        current_height >>= 1;
    }
    fclose(f);
    return true;
}

// Load texture from DDS file (first mip-map only). Returns true if successful.
// The texture is allocated, free with free().
bool detexLoadDDSFile(const char *filename, detexTexture **texture_out) {
    int nu_mipmaps;
    detexTexture **textures;
    bool r = detexLoadDDSFileWithMipmaps(filename, 1, &textures, &nu_mipmaps);
    if (!r) return false;
    *texture_out = textures[0];
    free(textures);
    return true;
}

static const char dds_id[4] = {'D', 'D', 'S', ' '};

// Save textures to DDS file (multiple mip-maps levels). Return true if succesful.
bool detexSaveDDSFileWithMipmaps(detexTexture **textures, int nu_levels, const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (f == NULL) {
        detexSetErrorMessage("detexSaveDDSFileWithMipmaps: Could not open file %s for writing", filename);
        return false;
    }
    const detexTextureFileInfo *info = detexLookupTextureFormatFileInfo(textures[0]->format);
    if (info == NULL) {
        detexSetErrorMessage("detexSaveDDSFileWithMipmaps: Could not match texture format with file format");
        return false;
    }
    if (!info->dds_support) {
        detexSetErrorMessage("detexSaveDDSFileWithMipmaps: Could not match texture format with DDS file format");
        return false;
    }
    size_t r = fwrite(dds_id, 1, 4, f);
    if (r != 4) {
        detexSetErrorMessage("detexSaveDDSFileWithMipmaps: Error writing to file %s", filename);
        return false;
    }
    int n;
    int block_size;
    if (detexFormatIsCompressed(textures[0]->format)) {
        n = textures[0]->width_in_blocks * textures[0]->height_in_blocks;
        block_size = detexGetCompressedBlockSize(textures[0]->format);
    } else {
        n = textures[0]->width * textures[0]->height;
        block_size = detexGetPixelSize(textures[0]->format);
    }
    uint8_t header[124];
    uint8_t dx10_header[20];
    memset(header, 0, 124);
    memset(dx10_header, 0, 20);
    uint32_t *header32 = (uint32_t *)header;
    *(uint32_t *)header32 = 124;
    uint32_t flags = 0x1007;
    if (nu_levels > 1) flags |= 0x20000;
    if (!detexFormatIsCompressed(textures[0]->format))
        flags |= 0x8;  // Pitch specified.
    else
        flags |= 0x80000;               // Linear size specified.
    *(uint32_t *)(header + 4) = flags;  // Flags
    *(uint32_t *)(header + 8) = textures[0]->height;
    *(uint32_t *)(header + 12) = textures[0]->width;
    *(uint32_t *)(header + 16) = n * block_size;  // Linear size for compressed textures.
    *(uint32_t *)(header + 24) = nu_levels;       // Mipmap count.
    *(uint32_t *)(header + 72) = 32;
    *(uint32_t *)(header + 76) = 0x4;  // Pixel format flags (fourCC present).
    bool write_dx10_header = false;
    if (strncmp(info->dx_four_cc, "DX10", 4) == 0) {
        write_dx10_header = true;
        uint32_t *dx10_header32 = (uint32_t *)dx10_header;
        *(uint32_t *)dx10_header32 = info->dx10_format;
        *(uint32_t *)(dx10_header + 4) = 3;   // Resource dimensions = 2D.
        *(uint32_t *)(dx10_header + 12) = 1;  // Array size.
    }
    if (!detexFormatIsCompressed(info->texture_format)) {
        uint64_t red_mask, green_mask, blue_mask, alpha_mask;
        detexGetComponentMasks(info->texture_format, &red_mask, &green_mask, &blue_mask, &alpha_mask);
        int component_size = detexGetComponentSize(info->texture_format) * 8;
        int nu_components = detexGetNumberOfComponents(info->texture_format);
        // Note: Some readers don't like the absence of other fields (such as the component masks and pixel
        // formats) for uncompressed data with a DX10 header.
        *(uint32_t *)(header + 84) = nu_components * component_size;  // bit count
        *(uint32_t *)(header + 88) = red_mask;
        *(uint32_t *)(header + 92) = green_mask;
        *(uint32_t *)(header + 96) = blue_mask;
        *(uint32_t *)(header + 100) = alpha_mask;
        // Format does not have a FOURCC code (legacy uncompressed format).
        uint32_t pixel_format_flags = 0x40;                            // Uncompressed RGB data present.
        if (strlen(info->dx_four_cc) > 0) pixel_format_flags |= 0x04;  // FourCC present.
        if (detexFormatHasAlpha(info->texture_format)) pixel_format_flags |= 0x01;
        *(uint32_t *)(header + 76) = pixel_format_flags;
    }
    if (strlen(info->dx_four_cc) > 0) {
        // In case of DXTn or DX10 fourCC, set it.
        strncpy((char *)(header + 80), info->dx_four_cc, 4);
        // Pixel format field was already set to 0x4 (FourCC present) by default.
    }
    uint32_t caps = 0x1000;
    if (nu_levels > 1) caps |= 0x400008;
    *(uint32_t *)(header + 104) = caps;  // Caps.
    int pitch = textures[0]->width * detexGetPixelSize(textures[0]->format);
    if (!detexFormatIsCompressed(textures[0]->format)) *(uint32_t *)(header + 16) = pitch;
    r = fwrite(header, 1, 124, f);
    if (r != 124) {
        detexSetErrorMessage("detexSaveDDSFileWithMipmaps: Error writing to file %s", filename);
        return false;
    }
    if (write_dx10_header) {
        r = fwrite(dx10_header, 1, 20, f);
        if (r != 20) {
            detexSetErrorMessage("detexSaveDDSFileWithMipmaps: Error writing to file %s", filename);
            return false;
        }
    }
    // Write data.
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
        // Write level data.
        r = fwrite(textures[i]->data, 1, n * block_size, f);
        if (r != n * block_size) {
            detexSetErrorMessage("detexSaveDDSFileWithMipmaps: Error writing to file %s", filename);
            return false;
        }
    }
    fclose(f);
    return true;
}

// Save texture to DDS file (single mip-map level). Returns true if succesful.
bool detexSaveDDSFile(detexTexture *texture, const char *filename) {
    detexTexture *textures[1];
    textures[0] = texture;
    return detexSaveDDSFileWithMipmaps(textures, 1, filename);
}
