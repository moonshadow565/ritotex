#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detex.h"

#define tex_magic "TEX"

enum {
    TEX_FORMAT_1 = 1,
    TEX_FORMAT_2 = 2,
    TEX_FORMAT_3 = 3,
    TEX_FORMAT_DXT1 = 10,
    TEX_FORMAT_DXT5 = 12,
};

typedef struct {
    uint8_t magic[4];
    uint16_t image_width;
    uint16_t image_height;
    uint8_t unk1;
    uint8_t tex_format;
    uint8_t unk2;
    bool has_mipmaps;
} TEX_HEADER;

bool detexLoadTEXFileWithMipmaps(const char *filename,
                                 int max_mipmaps,
                                 detexTexture ***textures_out,
                                 int *nu_levels_out) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        detexSetErrorMessage("detexLoadTEXFileWithMipmaps: Could not open file %s", filename);
        return false;
    }

    TEX_HEADER header;
    if (fread(&header, 1, sizeof(TEX_HEADER), file) != sizeof(TEX_HEADER)) {
        fprintf(stderr, "detexLoadTEXFileWithMipmaps: Couldn't read TEX header!\n");
        fclose(file);
        return false;
    }

    if (memcmp(header.magic, tex_magic, 4) != 0) {
        fprintf(stderr, "detexLoadTEXFileWithMipmaps: Not a valid tex file!\n");
        fclose(file);
        return false;
    }

    uint32_t format = DETEX_TEXTURE_FORMAT_BC1;
    switch (header.tex_format) {
        case 10:
            format = DETEX_TEXTURE_FORMAT_BC1;
            break;
        case 12:
            format = DETEX_TEXTURE_FORMAT_BC3;
            break;
        case TEX_FORMAT_1:
        case TEX_FORMAT_2:
        case TEX_FORMAT_3:
            // FIXME: figure what those are...
        default:
            // NOTE: technically riot handles all other formats as DXT1 ?????
            detexSetErrorMessage("detexLoadTEXFileWithMipmaps: Unhandled TEX format %d", header.tex_format);
            return false;
    }

    detexTextureFileInfo const *info = detexLookupTextureFormatFileInfo(format);
    uint32_t bytes_per_block = detextBytesPerBlock(info->texture_format);
    uint32_t block_width = info->block_width;
    uint32_t block_height = info->block_height;

    // TODO: log if clz method yields same result here
    // NOTE: this might actually be faster than clz
    uint32_t count_mipmaps = header.has_mipmaps ? floor(log2(max(header.image_height, header.image_width))) + 1.0 : 1;
    uint32_t nu_levels = min(max_mipmaps, count_mipmaps);
    *nu_levels_out = nu_levels;
    detexTexture **textures = malloc(nu_levels * sizeof(detexTexture *));
    *textures_out = textures;

    uint32_t current_width = header.image_width;
    uint32_t current_height = header.image_height;
    fseek(file, 0, SEEK_END);

    for (int i = 0; i < nu_levels; ++i) {
        // Calculate sizes
        uint32_t width_in_blocks = max((current_width + block_width - 1) / block_width, 1);
        uint32_t height_in_blocks = max((current_height + block_height - 1) / block_height, 1);
        uint32_t size = width_in_blocks * height_in_blocks * bytes_per_block;

        // Read in texture
        if (fseek(file, -size, SEEK_CUR) != 0 || ftell(file) < sizeof(TEX_HEADER)) {
            detexSetErrorMessage("detexLoadTEXFileWithMipmaps: Can't read texture %d", i);
            return false;
        }

        textures[i] = (detexTexture *)malloc(sizeof(detexTexture));
        *textures[i] = (detexTexture const){
            .format = format,
            .width = current_width,
            .height = current_height,
            .width_in_blocks = width_in_blocks,
            .height_in_blocks = height_in_blocks,
            .data = malloc(size),
        };
        fread(textures[i]->data, 1, size, file);
        fseek(file, -size, SEEK_CUR);

        // Next texture
        current_width >>= 1;
        current_height >>= 1;
    }
    return true;
}

bool detexLoadTEXFile(const char *filename, detexTexture **texture_out) {
    int nu_mipmaps;
    detexTexture **textures;
    bool r = detexLoadTEXFileWithMipmaps(filename, 1, &textures, &nu_mipmaps);
    if (!r) return false;
    *texture_out = textures[0];
    free(textures);
    return true;
}

bool detexSaveTEXFileWithMipmaps(detexTexture **textures, int nu_levels, const char *filename) {
    FILE* tex_file = fopen(filename, "wb");
    if (!tex_file) {
        detexSetErrorMessage("detexSaveTEXFileWithMipmaps: Could not open file %s for writing", filename);
        return false;
    }

    TEX_HEADER tex_header = {
        .magic = "TEX",
        .image_width = textures[nu_levels-1]->width,
        .image_height = textures[nu_levels-1]->height,
        .unk1 = 1,
        .tex_format = textures[0]->format == DETEX_COMPRESSED_TEXTURE_FORMAT_INDEX_DXT5 ? 0xC : 0xA,
        .has_mipmaps = nu_levels > 1
    };
    fwrite(&tex_header, sizeof(TEX_HEADER), 1, tex_file);
    for (int i = nu_levels-1; i >= 0; i++) {
        fwrite(textures[i]->data, 1, textures[i]->width * textures[i]->height, tex_file);
    }

    fclose(tex_file);
    return true;
}

bool detexSaveTEXFile(detexTexture *texture, const char *filename) {
    detexTexture *textures[1];
    textures[0] = texture;
    return detexSaveTEXFileWithMipmaps(textures, 1, filename);
}
