/*
 * inf1102_project_tool.c
 *
 * Image-based assignment system for INF1102  
 *
 *   inf1102_tool init <id1> <id2>
 *       Derives assignment params from hashed IDs + Pi MAC.
 *       Writes:
 *         - inf1102_config.h (operation and parameters)
 *         - input_image.ppm (Mandelbrot image, PPM P6 format)
 *
 *   inf1102_tool finalise <id1> <id2> <report.txt> <output_image.ppm>
 *       Reads report and output image.
 *       Creates submission.bin with both inputs + cryptographic watermark.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <dirent.h>

#define CLASS_SALT "AY26_tri1"
#define IMAGE_WIDTH 256
#define IMAGE_HEIGHT 256
#define IMAGE_CHANNELS 3
#define IMAGE_SIZE (IMAGE_WIDTH * IMAGE_HEIGHT * IMAGE_CHANNELS)

/* ===================== FNV-1a 64-bit ===================== */

static uint64_t fnv1a64(const uint8_t *data, size_t len, uint64_t seed) {
    uint64_t h = 0xcbf29ce484222325ULL ^ seed;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static uint64_t fnv1a64_str(const char *s, uint64_t seed) {
    return fnv1a64((const uint8_t *)s, strlen(s), seed);
}

/* ===================== splitmix64 PRNG ===================== */

typedef struct { uint64_t state; } SplitMix64;

static uint64_t splitmix64_next(SplitMix64 *rng) {
    uint64_t z = (rng->state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* ===================== ID and MAC ===================== */

static void normalize_id(char *s) {
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start > 0) memmove(s, s + start, len - start + 1);
    for (size_t i = 0; s[i]; i++) s[i] = (char)toupper((unsigned char)s[i]);
}

static uint64_t pair_seed(const char *id1_in, const char *id2_in) {
    char a[128], b[128];
    snprintf(a, sizeof(a), "%s", id1_in);
    snprintf(b, sizeof(b), "%s", id2_in);
    normalize_id(a);
    normalize_id(b);
    const char *first = a, *second = b;
    if (strcmp(a, b) > 0) { first = b; second = a; }
    char joined[300];
    snprintf(joined, sizeof(joined), "%s:%s", first, second);
    return fnv1a64_str(joined, 0);
}

static int get_pi_mac(char *mac_out, size_t max_len) {
    DIR *d = opendir("/sys/class/net");
    if (!d) return 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, "lo") == 0) continue;
        char path[512];
        snprintf(path, sizeof(path), "/sys/class/net/%s/address", ent->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (fgets(mac_out, max_len, f)) {
            size_t l = strlen(mac_out);
            while (l > 0 && isspace((unsigned char)mac_out[l - 1])) mac_out[--l] = '\0';
            fclose(f);
            closedir(d);
            return 1;
        }
        fclose(f);
    }
    closedir(d);
    return 0;
}

static int get_hotspot_mac(char *mac_out, size_t max_len) {
    FILE *f = popen("iw dev wlan0 link 2>/dev/null | grep 'Connected to' | awk '{print $3}'", "r");
    if (!f) return 0;
    if (fgets(mac_out, max_len, f)) {
        size_t l = strlen(mac_out);
        while (l > 0 && isspace((unsigned char)mac_out[l - 1])) mac_out[--l] = '\0';
        pclose(f);
        return strlen(mac_out) > 0 ? 1 : 0;
    }
    pclose(f);
    return 0;
}

/* ===================== Assignment parameters ===================== */

#define NUM_OPERATIONS 6

static const char *OPERATIONS[NUM_OPERATIONS] = {
    "rotate_left", "rotate_right", "channel_swap", "pink_scale", "threshold", "edge_detect"
};

typedef struct {
    uint64_t seed;
    const char *operation;
    int threshold_val;
} ImageAssignParams;

static ImageAssignParams derive_image_params(uint64_t seed) {
    SplitMix64 rng = { seed };
    ImageAssignParams p;
    p.seed = seed;
    int op_idx = (int)(splitmix64_next(&rng) % NUM_OPERATIONS);
    p.operation = OPERATIONS[op_idx];
    p.threshold_val = 64 + (int)(splitmix64_next(&rng) % 128);
    return p;
}

/* ===================== Mandelbrot image generation with zoom ===================== */

static void gen_mandelbrot_image(uint8_t *img, int width, int height, uint64_t seed) {
    SplitMix64 rng = { seed };

    // Derive zoom level and pan offset from seed
    double zoom = 0.5 + (double)(splitmix64_next(&rng) % 1000) / 2000.0;
    double pan_x = -0.5 + (double)(splitmix64_next(&rng) % 1000) / 500.0;
    double pan_y = -0.25 + (double)(splitmix64_next(&rng) % 1000) / 2000.0;

    double x_min = -2.5 * zoom + pan_x;
    double x_max = 1.0 * zoom + pan_x;
    double y_min = -1.25 * zoom + pan_y;
    double y_max = 1.25 * zoom + pan_y;
    int max_iter = 128 + (int)(splitmix64_next(&rng) % 128);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            double cx = x_min + (double)x * (x_max - x_min) / width;
            double cy = y_min + (double)y * (y_max - y_min) / height;

            double zx = 0, zy = 0;
            int iter = 0;
            while (iter < max_iter && zx*zx + zy*zy < 4.0) {
                double zx_new = zx*zx - zy*zy + cx;
                zy = 2*zx*zy + cy;
                zx = zx_new;
                iter++;
            }

            uint8_t col = (uint8_t)((iter * 255) / max_iter);
            uint8_t r = (iter < max_iter) ? col : 0;
            uint8_t g = (uint8_t)(col * 0.7);
            uint8_t b = (uint8_t)(col * 0.5);

            int idx = (y * width + x) * 3;
            img[idx] = r;
            img[idx+1] = g;
            img[idx+2] = b;
        }
    }
}

/* ===================== Image I/O ===================== */

static int write_ppm_file(const char *path, const uint8_t *img, int width, int height) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    size_t nw = fwrite(img, 1, width * height * 3, f);
    fclose(f);
    return (nw == (size_t)(width * height * 3)) ? 0 : -1;
}

/* ===================== Commands ===================== */

static void cmd_init(const char *id1, const char *id2) {
    uint64_t seed = pair_seed(id1, id2);
    ImageAssignParams p = derive_image_params(seed);

    // Write config header
    FILE *f = fopen("inf1102_config.h", "w");
    if (!f) { perror("fopen inf1102_config.h"); exit(1); }
    fprintf(f,
        "/* AUTO-GENERATED by inf1102_tool init */\n"
        "#ifndef INF1102_CONFIG_H\n"
        "#define INF1102_CONFIG_H\n\n"
        "#define INF1102_SEED_HEX \"%016llx\"\n"
        "#define INF1102_OPERATION \"%s\"\n"
        "#define INF1102_THRESHOLD_VAL %d\n"
        "#define INF1102_IMAGE_WIDTH %d\n"
        "#define INF1102_IMAGE_HEIGHT %d\n\n"
        "#endif\n",
        (unsigned long long)seed, p.operation, p.threshold_val,
        IMAGE_WIDTH, IMAGE_HEIGHT);
    fclose(f);

    // Generate and write image
    uint8_t *img = malloc(IMAGE_SIZE);
    if (!img) { perror("malloc"); exit(1); }
    gen_mandelbrot_image(img, IMAGE_WIDTH, IMAGE_HEIGHT, seed);
    if (write_ppm_file("input_image.ppm", img, IMAGE_WIDTH, IMAGE_HEIGHT) != 0) {
        perror("write_ppm_file");
        exit(1);
    }
    free(img);

    printf("Initialised for pair (%s, %s)\n", id1, id2);
    printf("  seed           : %016llx\n", (unsigned long long)seed);
    printf("  operation      : %s\n", p.operation);
    printf("  threshold      : %d\n", p.threshold_val);
    printf("\nWrote inf1102_config.h and input_image.ppm (%dx%d)\n",
           IMAGE_WIDTH, IMAGE_HEIGHT);
}

static void cmd_finalise(const char *id1, const char *id2, const char *report_path, const char *image_path) {
    uint64_t seed = pair_seed(id1, id2);
    ImageAssignParams p = derive_image_params(seed);
    (void)p;

    // Read report
    FILE *f = fopen(report_path, "rb");
    if (!f) { perror("fopen report.txt"); exit(1); }
    fseek(f, 0, SEEK_END);
    long report_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *report_data = malloc(report_size);
    if (!report_data) { perror("malloc"); exit(1); }
    size_t nread = fread(report_data, 1, report_size, f);
    fclose(f);
    if ((long)nread != report_size) {
        fprintf(stderr, "short read from report\n");
        exit(1);
    }

    // Read output image (PPM file)
    f = fopen(image_path, "rb");
    if (!f) { perror("fopen output image"); exit(1); }
    fseek(f, 0, SEEK_END);
    long img_file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *img_file = malloc(img_file_size);
    if (!img_file) { perror("malloc"); exit(1); }
    nread = fread(img_file, 1, img_file_size, f);
    fclose(f);
    if ((long)nread != img_file_size) {
        fprintf(stderr, "short read from image\n");
        exit(1);
    }

    // Extract raw pixel data from PPM (skip header)
    char *header_end = strchr((char*)img_file, '\n');
    header_end = strchr(header_end + 1, '\n');
    header_end = strchr(header_end + 1, '\n');
    uint8_t *output_img_ptr = (uint8_t*)header_end + 1;
    size_t img_read = img_file_size - (output_img_ptr - img_file);

    // Get Pi MAC
    char mac[32] = {0};
    get_pi_mac(mac, sizeof(mac));
    
    // Get hotspot/AP MAC address
    char hotspot_mac[32] = {0};
    get_hotspot_mac(hotspot_mac, sizeof(hotspot_mac));
    if (strlen(hotspot_mac) == 0) {
        snprintf(hotspot_mac, sizeof(hotspot_mac), "unknown");
    }

    // Compute watermark token
    char token_input[512];
    snprintf(token_input, sizeof(token_input), "%s|%s|%016llx|%s|%s|%ld|%zu",
             id1, id2, (unsigned long long)seed, mac, hotspot_mac, (long)report_size, img_read);
    uint64_t token = fnv1a64_str(token_input, fnv1a64_str(CLASS_SALT, 0));

    // Write submission binary
    f = fopen("submission.bin", "wb");
    if (!f) { perror("fopen submission.bin"); exit(1); }
    fwrite("INF1102", 1, 7, f);
    fputc(0, f);
    fprintf(f, "%s|%s|%016llx|%s|%s", id1, id2, (unsigned long long)seed, mac, hotspot_mac);
    fputc(0, f);

    uint32_t report_sz_le = (uint32_t)report_size;
    uint32_t img_sz_le = (uint32_t)img_read;
    fwrite(&report_sz_le, 4, 1, f);
    fwrite(&img_sz_le, 4, 1, f);
    fwrite(report_data, 1, report_size, f);
    fwrite(output_img_ptr, 1, img_read, f);
    fwrite(&token, 8, 1, f);
    fclose(f);

    free(report_data);
    free(img_file);

    printf("Finalised submission for pair (%s, %s)\n", id1, id2);
    printf("  report size    : %ld bytes\n", report_size);
    printf("  image size     : %zu bytes\n", img_read);
    printf("  mac            : %s\n", mac);
    printf("  submission     : submission.bin\n");
    printf("  token          : %016llx (for verification)\n", (unsigned long long)token);
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s init <student_id_1> <student_id_2>\n"
        "  %s finalise <student_id_1> <student_id_2> <report.txt> <output_image.ppm>\n",
        prog, prog);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    const char *id1 = argv[2];
    const char *id2 = argv[3];

    if (strcmp(cmd, "init") == 0) {
        if (argc != 4) { print_usage(argv[0]); return 1; }
        cmd_init(id1, id2);
    } else if (strcmp(cmd, "finalise") == 0) {
        if (argc != 6) { print_usage(argv[0]); return 1; }
        const char *report_path = argv[4];
        const char *image_path = argv[5];
        cmd_finalise(id1, id2, report_path, image_path);
    } else {
        print_usage(argv[0]);
        return 1;
    }
    return 0;
}
