/*
 * frame_compare.c - Compare two BMP files pixel-by-pixel
 * Outputs difference statistics and generates a diff image
 *
 * Usage: frame_compare <our.bmp> <ref.bmp> [diff_output.bmp]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#pragma pack(push, 1)
typedef struct {
    uint16_t type;       /* 'BM' */
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} bmp_file_header_t;

typedef struct {
    uint32_t header_size;
    int32_t  width;
    int32_t  height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t image_size;
    int32_t  x_ppm;
    int32_t  y_ppm;
    uint32_t colors_used;
    uint32_t colors_important;
} bmp_info_header_t;
#pragma pack(pop)

typedef struct {
    int width, height;
    uint8_t *pixels; /* RGB, row-major, top-to-bottom */
} image_t;

static image_t *load_bmp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open: %s\n", path);
        return NULL;
    }

    bmp_file_header_t fh;
    bmp_info_header_t ih;
    fread(&fh, sizeof(fh), 1, f);
    fread(&ih, sizeof(ih), 1, f);

    if (fh.type != 0x4D42) {
        fprintf(stderr, "Not a BMP: %s\n", path);
        fclose(f);
        return NULL;
    }

    int w = ih.width;
    int h = ih.height;
    int top_down = 0;
    if (h < 0) { h = -h; top_down = 1; }

    int bpp = ih.bpp;
    if (bpp != 24 && bpp != 32) {
        fprintf(stderr, "Unsupported BPP %d in %s\n", bpp, path);
        fclose(f);
        return NULL;
    }

    image_t *img = malloc(sizeof(image_t));
    img->width = w;
    img->height = h;
    img->pixels = malloc(w * h * 3);

    fseek(f, fh.offset, SEEK_SET);

    int src_stride = ((w * (bpp / 8) + 3) & ~3);
    uint8_t *row_buf = malloc(src_stride);

    for (int y = 0; y < h; y++) {
        int dst_y = top_down ? y : (h - 1 - y);
        fread(row_buf, 1, src_stride, f);
        for (int x = 0; x < w; x++) {
            int si = x * (bpp / 8);
            int di = (dst_y * w + x) * 3;
            img->pixels[di + 0] = row_buf[si + 2]; /* R */
            img->pixels[di + 1] = row_buf[si + 1]; /* G */
            img->pixels[di + 2] = row_buf[si + 0]; /* B */
        }
    }

    free(row_buf);
    fclose(f);
    return img;
}

static void save_bmp(const char *path, const image_t *img) {
    int stride = ((img->width * 3 + 3) & ~3);
    int img_size = stride * img->height;

    bmp_file_header_t fh = {0};
    fh.type = 0x4D42;
    fh.offset = sizeof(fh) + sizeof(bmp_info_header_t);
    fh.file_size = fh.offset + img_size;

    bmp_info_header_t ih = {0};
    ih.header_size = sizeof(ih);
    ih.width = img->width;
    ih.height = -img->height; /* top-down */
    ih.planes = 1;
    ih.bpp = 24;
    ih.image_size = img_size;

    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Cannot write: %s\n", path); return; }
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);

    uint8_t *row = calloc(stride, 1);
    for (int y = 0; y < img->height; y++) {
        memset(row, 0, stride);
        for (int x = 0; x < img->width; x++) {
            int si = (y * img->width + x) * 3;
            int di = x * 3;
            row[di + 0] = img->pixels[si + 2]; /* B */
            row[di + 1] = img->pixels[si + 1]; /* G */
            row[di + 2] = img->pixels[si + 0]; /* R */
        }
        fwrite(row, 1, stride, f);
    }
    free(row);
    fclose(f);
}

static void free_image(image_t *img) {
    if (img) { free(img->pixels); free(img); }
}

/* Print a text representation of an image region (useful for debugging) */
static void print_region(const image_t *img, int x0, int y0, int w, int h) {
    /* Map pixel brightness to ASCII characters */
    const char *shade = " .:-=+*#%@";
    int shade_len = 10;

    if (x0 + w > img->width) w = img->width - x0;
    if (y0 + h > img->height) h = img->height - y0;

    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            int i = (y * img->width + x) * 3;
            int brightness = (img->pixels[i] + img->pixels[i+1] + img->pixels[i+2]) / 3;
            int idx = brightness * (shade_len - 1) / 255;
            putchar(shade[idx]);
        }
        putchar('\n');
    }
}

/* Collect unique colors from an image (up to max_colors) */
typedef struct {
    uint8_t r, g, b;
    int count;
    int brightness; /* (r + g + b) / 3 */
} unique_color_t;

static int find_unique_colors(const image_t *img, unique_color_t *colors, int max_colors) {
    int n = 0;
    for (int i = 0; i < img->width * img->height; i++) {
        uint8_t r = img->pixels[i * 3 + 0];
        uint8_t g = img->pixels[i * 3 + 1];
        uint8_t b = img->pixels[i * 3 + 2];

        int found = 0;
        for (int j = 0; j < n; j++) {
            if (colors[j].r == r && colors[j].g == g && colors[j].b == b) {
                colors[j].count++;
                found = 1;
                break;
            }
        }
        if (!found && n < max_colors) {
            colors[n].r = r;
            colors[n].g = g;
            colors[n].b = b;
            colors[n].brightness = (r + g + b) / 3;
            colors[n].count = 1;
            n++;
        }
    }
    /* Sort by brightness (descending - lightest first) */
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (colors[j].brightness > colors[i].brightness) {
                unique_color_t tmp = colors[i];
                colors[i] = colors[j];
                colors[j] = tmp;
            }
    return n;
}

/* Map a pixel to a shade index (0-3) using the palette's own shade boundaries.
 *
 * The palette is sorted by brightness descending. We cluster the N palette
 * colors into exactly 4 groups using the largest brightness gaps as boundaries.
 * Each palette entry gets a shade assignment, and we map each pixel to the
 * nearest palette entry, then return that entry's shade.
 *
 * This correctly handles palettes with any number of unique colors (e.g., 8
 * colors from the BGB emulator) by normalizing them to Game Boy shade indices.
 */
static int pixel_to_shade(uint8_t r, uint8_t g, uint8_t b,
                           const unique_color_t *palette, int n_colors,
                           const int *shade_map) {
    int brightness = (r + g + b) / 3;
    int best = 0, best_dist = 999;
    for (int i = 0; i < n_colors; i++) {
        int dist = abs(brightness - palette[i].brightness);
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return shade_map[best];
}

/* Build a shade map for a palette with 4 or fewer colors (direct mapping) */
static void build_shade_map_direct(const unique_color_t *palette, int n_colors,
                                    int *shade_map) {
    (void)palette; /* sorted by brightness descending, index = shade */
    for (int i = 0; i < n_colors; i++)
        shade_map[i] = i;
}

/* Build a shade map by cross-correlating two images.
 *
 * For each color in the 'src' palette, examine all pixels of that color and
 * check what shade they correspond to in the 'ref' palette (which must have
 * a pre-built shade map). The shade that appears most often wins.
 *
 * This correctly handles BGB's multi-palette colorized output: even though
 * BGP shade 2 (#393984, brightness=82) and OBP0 shade 2 (#9494C6, brightness=164)
 * have very different brightnesses, they both appear at coordinates where our
 * emulator has shade 2 pixels, so the cross-correlation maps them correctly.
 */
static void build_shade_map_cross(const image_t *src_img, const unique_color_t *src_palette, int n_src,
                                   const image_t *ref_img, const unique_color_t *ref_palette, int n_ref,
                                   const int *ref_shade_map, int *src_shade_map) {
    int cw = src_img->width < ref_img->width ? src_img->width : ref_img->width;
    int ch = src_img->height < ref_img->height ? src_img->height : ref_img->height;

    /* For each src palette entry, count how many of its pixels correspond
     * to each shade (0-3) in the ref image */
    int votes[64][4];
    memset(votes, 0, sizeof(votes));

    for (int y = 0; y < ch; y++) {
        for (int x = 0; x < cw; x++) {
            int si = (y * src_img->width + x) * 3;
            int ri = (y * ref_img->width + x) * 3;

            /* Find which src palette entry this pixel belongs to */
            uint8_t sr = src_img->pixels[si], sg = src_img->pixels[si+1], sb = src_img->pixels[si+2];
            int src_idx = -1;
            for (int j = 0; j < n_src; j++) {
                if (src_palette[j].r == sr && src_palette[j].g == sg && src_palette[j].b == sb) {
                    src_idx = j;
                    break;
                }
            }
            if (src_idx < 0) continue;

            /* Find which ref shade this pixel's ref counterpart belongs to */
            uint8_t rr = ref_img->pixels[ri], rg = ref_img->pixels[ri+1], rb = ref_img->pixels[ri+2];
            int ref_brightness = (rr + rg + rb) / 3;
            int ref_idx = 0, best_dist = 999;
            for (int j = 0; j < n_ref; j++) {
                int dist = abs(ref_brightness - ref_palette[j].brightness);
                if (dist < best_dist) { best_dist = dist; ref_idx = j; }
            }
            int ref_shade = ref_shade_map[ref_idx];

            votes[src_idx][ref_shade]++;
        }
    }

    /* Print vote counts for diagnostics */
    printf("\n=== Cross-Correlation Votes ===\n");
    for (int i = 0; i < n_src; i++) {
        printf("  #%02X%02X%02X (brightness=%3d): shade0=%d shade1=%d shade2=%d shade3=%d",
               src_palette[i].r, src_palette[i].g, src_palette[i].b,
               src_palette[i].brightness,
               votes[i][0], votes[i][1], votes[i][2], votes[i][3]);
        int total_votes = votes[i][0] + votes[i][1] + votes[i][2] + votes[i][3];
        int best_count = -1, best_shade = 0;
        for (int s = 0; s < 4; s++) {
            if (votes[i][s] > best_count) { best_count = votes[i][s]; best_shade = s; }
        }
        if (total_votes > 0)
            printf(" -> shade %d (%.0f%% confidence)\n",
                   best_shade, 100.0 * best_count / total_votes);
        else
            printf(" -> no votes\n");
    }

    /* For each src color, pick the shade with the most votes */
    for (int i = 0; i < n_src; i++) {
        int best_shade = 0, best_count = -1;
        for (int s = 0; s < 4; s++) {
            if (votes[i][s] > best_count) {
                best_count = votes[i][s];
                best_shade = s;
            }
        }
        src_shade_map[i] = best_shade;
    }

    /* Enforce monotonicity: src palette is sorted brightness-descending,
     * so shade values must be non-decreasing. Fix violations by clamping
     * to the previous entry's shade value. */
    for (int i = 1; i < n_src; i++) {
        if (src_shade_map[i] < src_shade_map[i - 1])
            src_shade_map[i] = src_shade_map[i - 1];
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <ours.bmp> <ref.bmp> [diff.bmp]\n", argv[0]);
        return 1;
    }

    image_t *ours = load_bmp(argv[1]);
    image_t *ref = load_bmp(argv[2]);
    if (!ours || !ref) return 1;

    printf("Our image: %dx%d\n", ours->width, ours->height);
    printf("Ref image: %dx%d\n", ref->width, ref->height);

    /* Analyze unique colors in each image */
    unique_color_t our_colors[64] = {0};
    unique_color_t ref_colors[64] = {0};
    int n_our = find_unique_colors(ours, our_colors, 64);
    int n_ref = find_unique_colors(ref, ref_colors, 64);

    printf("\n=== Our Palette (%d unique colors) ===\n", n_our);
    for (int i = 0; i < n_our && i < 16; i++)
        printf("  #%02X%02X%02X  brightness=%3d  count=%d\n",
               our_colors[i].r, our_colors[i].g, our_colors[i].b,
               our_colors[i].brightness, our_colors[i].count);
    if (n_our > 16) printf("  ... and %d more\n", n_our - 16);

    printf("\n=== Ref Palette (%d unique colors) ===\n", n_ref);
    for (int i = 0; i < n_ref && i < 16; i++)
        printf("  #%02X%02X%02X  brightness=%3d  count=%d\n",
               ref_colors[i].r, ref_colors[i].g, ref_colors[i].b,
               ref_colors[i].brightness, ref_colors[i].count);
    if (n_ref > 16) printf("  ... and %d more\n", n_ref - 16);

    /* Build shade maps: assign each palette entry to a GB shade 0-3.
     *
     * Strategy: the image with exactly 4 colors (our emulator output) gets
     * a direct 1:1 shade map. The image with more colors (BGB reference)
     * gets a cross-correlated shade map -- for each of its colors, we look
     * at what shade the 4-color image has at the same pixel coordinates.
     * This correctly handles BGB's multi-palette colorized output where
     * different GB palettes (BGP, OBP0, OBP1) produce different RGB colors
     * for the same shade level.
     */
    int our_shade_map[64] = {0};
    int ref_shade_map[64] = {0};

    if (n_our <= 4 && n_ref <= 4) {
        /* Both have 4 or fewer colors: direct mapping */
        build_shade_map_direct(our_colors, n_our, our_shade_map);
        build_shade_map_direct(ref_colors, n_ref, ref_shade_map);
    } else if (n_our <= 4) {
        /* Our palette is the anchor (4 colors), cross-correlate ref against it */
        build_shade_map_direct(our_colors, n_our, our_shade_map);
        build_shade_map_cross(ref, ref_colors, n_ref,
                              ours, our_colors, n_our,
                              our_shade_map, ref_shade_map);
    } else if (n_ref <= 4) {
        /* Ref palette is the anchor (4 colors), cross-correlate ours against it */
        build_shade_map_direct(ref_colors, n_ref, ref_shade_map);
        build_shade_map_cross(ours, our_colors, n_our,
                              ref, ref_colors, n_ref,
                              ref_shade_map, our_shade_map);
    } else {
        /* Both have more than 4 colors: fall back to direct mapping
         * (treat palette index as shade, clamped to 0-3) */
        build_shade_map_direct(our_colors, n_our > 4 ? 4 : n_our, our_shade_map);
        build_shade_map_direct(ref_colors, n_ref > 4 ? 4 : n_ref, ref_shade_map);
    }

    printf("\n=== Shade Mapping (Our Palette -> GB Shades) ===\n");
    for (int i = 0; i < n_our && i < 16; i++)
        printf("  #%02X%02X%02X (brightness=%3d) -> shade %d\n",
               our_colors[i].r, our_colors[i].g, our_colors[i].b,
               our_colors[i].brightness, our_shade_map[i]);

    printf("\n=== Shade Mapping (Ref Palette -> GB Shades) ===\n");
    for (int i = 0; i < n_ref && i < 16; i++)
        printf("  #%02X%02X%02X (brightness=%3d) -> shade %d\n",
               ref_colors[i].r, ref_colors[i].g, ref_colors[i].b,
               ref_colors[i].brightness, ref_shade_map[i]);

    /* Compare dimensions */
    int cw = ours->width < ref->width ? ours->width : ref->width;
    int ch = ours->height < ref->height ? ours->height : ref->height;

    if (ours->width != ref->width || ours->height != ref->height) {
        printf("WARNING: Size mismatch! Comparing %dx%d overlap.\n", cw, ch);
    }

    /* ================================================================
     * RAW pixel comparison (exact RGB match)
     * ================================================================ */
    int total = cw * ch;
    int matching = 0;
    int different = 0;
    double sum_diff = 0;
    double max_diff = 0;

    for (int y = 0; y < ch; y++) {
        for (int x = 0; x < cw; x++) {
            int oi = (y * ours->width + x) * 3;
            int ri = (y * ref->width + x) * 3;

            int dr = abs(ours->pixels[oi+0] - ref->pixels[ri+0]);
            int dg = abs(ours->pixels[oi+1] - ref->pixels[ri+1]);
            int db = abs(ours->pixels[oi+2] - ref->pixels[ri+2]);

            double pixel_diff = (dr + dg + db) / 3.0;
            sum_diff += pixel_diff;
            if (pixel_diff > max_diff) max_diff = pixel_diff;

            if (dr == 0 && dg == 0 && db == 0)
                matching++;
            else
                different++;
        }
    }

    printf("\n=== Raw Pixel Comparison ===\n");
    printf("Total pixels:    %d\n", total);
    printf("Matching:        %d (%.1f%%)\n", matching, 100.0 * matching / total);
    printf("Different:       %d (%.1f%%)\n", different, 100.0 * different / total);
    printf("Avg difference:  %.2f / 255\n", sum_diff / total);
    printf("Max difference:  %.0f / 255\n", max_diff);

    /* ================================================================
     * STRUCTURAL comparison (palette-normalized, shade-index based)
     * Maps each pixel to shade index 0-3 by brightness, then compares.
     * This ignores palette color differences and finds real rendering bugs.
     * ================================================================ */
    int struct_matching = 0;
    int struct_different = 0;

    /* Create diff image showing structural differences */
    image_t diff;
    diff.width = cw;
    diff.height = ch;
    diff.pixels = malloc(cw * ch * 3);

    /* Structural diff heatmap */
    int grid_w = (cw + 19) / 20;
    int grid_h = (ch + 17) / 18;
    int *region_diffs = calloc(20 * 18, sizeof(int));

    for (int y = 0; y < ch; y++) {
        for (int x = 0; x < cw; x++) {
            int oi = (y * ours->width + x) * 3;
            int ri = (y * ref->width + x) * 3;
            int di = (y * cw + x) * 3;

            int our_shade = pixel_to_shade(
                ours->pixels[oi], ours->pixels[oi+1], ours->pixels[oi+2],
                our_colors, n_our, our_shade_map);
            int ref_shade = pixel_to_shade(
                ref->pixels[ri], ref->pixels[ri+1], ref->pixels[ri+2],
                ref_colors, n_ref, ref_shade_map);

            if (our_shade == ref_shade) {
                struct_matching++;
                /* Gray pixel at shade brightness */
                uint8_t gray = (uint8_t)(255 - our_shade * 85);
                diff.pixels[di+0] = gray / 2;
                diff.pixels[di+1] = gray / 2;
                diff.pixels[di+2] = gray / 2;
            } else {
                struct_different++;
                /* Red = our shade, Blue = ref shade */
                uint8_t our_bright = (uint8_t)(255 - our_shade * 85);
                uint8_t ref_bright = (uint8_t)(255 - ref_shade * 85);
                diff.pixels[di+0] = our_bright;
                diff.pixels[di+1] = 0;
                diff.pixels[di+2] = ref_bright;

                int gx = x / grid_w;
                int gy = y / grid_h;
                if (gx < 20 && gy < 18)
                    region_diffs[gy * 20 + gx]++;
            }
        }
    }

    printf("\n=== Structural Comparison (palette-normalized) ===\n");
    printf("Total pixels:    %d\n", total);
    printf("Matching shades: %d (%.1f%%)\n", struct_matching, 100.0 * struct_matching / total);
    printf("Different shades:%d (%.1f%%)\n", struct_different, 100.0 * struct_different / total);

    if (struct_different > 0) {
        /* Print structural diff heatmap */
        printf("\n=== Structural Difference Heatmap (20x18 grid) ===\n");
        printf("  . = identical, 1-9 = some diffs, X = many diffs\n\n");
        int max_region = 0;
        for (int i = 0; i < 20 * 18; i++)
            if (region_diffs[i] > max_region) max_region = region_diffs[i];

        for (int gy = 0; gy < 18; gy++) {
            printf("  ");
            for (int gx = 0; gx < 20; gx++) {
                int d = region_diffs[gy * 20 + gx];
                if (d == 0) putchar('.');
                else if (max_region > 0 && d > max_region * 8 / 10) putchar('X');
                else {
                    int level = (d * 9) / (max_region > 0 ? max_region : 1);
                    if (level < 1) level = 1;
                    if (level > 9) level = 9;
                    putchar('0' + level);
                }
            }
            putchar('\n');
        }
    }

    /* Print first mismatches with coordinates and shade info */
    if (struct_different > 0) {
        printf("\n=== First 30 Shade Mismatches ===\n");
        printf("  %-8s %-10s %-10s %-14s %-14s\n",
               "Coord", "Our shade", "Ref shade", "Our color", "Ref color");
        int printed = 0;
        for (int y = 0; y < ch && printed < 30; y++) {
            for (int x = 0; x < cw && printed < 30; x++) {
                int oi = (y * ours->width + x) * 3;
                int ri = (y * ref->width + x) * 3;
                int our_shade = pixel_to_shade(
                    ours->pixels[oi], ours->pixels[oi+1], ours->pixels[oi+2],
                    our_colors, n_our, our_shade_map);
                int ref_shade = pixel_to_shade(
                    ref->pixels[ri], ref->pixels[ri+1], ref->pixels[ri+2],
                    ref_colors, n_ref, ref_shade_map);
                if (our_shade != ref_shade) {
                    printf("  (%3d,%3d) %-10d %-10d #%02X%02X%02X       #%02X%02X%02X\n",
                           x, y, our_shade, ref_shade,
                           ours->pixels[oi], ours->pixels[oi+1], ours->pixels[oi+2],
                           ref->pixels[ri], ref->pixels[ri+1], ref->pixels[ri+2]);
                    printed++;
                }
            }
        }

        /* Count mismatches by shade transition type */
        printf("\n=== Mismatch Types ===\n");
        int type_counts[4][4] = {0};
        for (int y = 0; y < ch; y++) {
            for (int x = 0; x < cw; x++) {
                int oi = (y * ours->width + x) * 3;
                int ri = (y * ref->width + x) * 3;
                int os = pixel_to_shade(ours->pixels[oi], ours->pixels[oi+1], ours->pixels[oi+2],
                                        our_colors, n_our, our_shade_map);
                int rs = pixel_to_shade(ref->pixels[ri], ref->pixels[ri+1], ref->pixels[ri+2],
                                        ref_colors, n_ref, ref_shade_map);
                if (os != rs && os < 4 && rs < 4)
                    type_counts[os][rs]++;
            }
        }
        printf("  Our\\Ref   shade0  shade1  shade2  shade3\n");
        for (int i = 0; i < 4; i++) {
            printf("  shade%d    ", i);
            for (int j = 0; j < 4; j++) {
                if (i == j) printf("   -    ");
                else printf("%6d  ", type_counts[i][j]);
            }
            printf("\n");
        }
    }

    /* Print ASCII art of both images */
    printf("\n=== Our Frame (ASCII, top-left 80x36) ===\n");
    print_region(ours, 0, 0, 80, 36);
    printf("\n=== Ref Frame (ASCII, top-left 80x36) ===\n");
    print_region(ref, 0, 0, 80, 36);

    /* Save diff image */
    if (argc >= 4) {
        save_bmp(argv[3], &diff);
        printf("\nStructural diff image saved to: %s\n", argv[3]);
    }

    free(region_diffs);
    free(diff.pixels);
    free_image(ours);
    free_image(ref);

    return struct_different > 0 ? 1 : 0;
}
