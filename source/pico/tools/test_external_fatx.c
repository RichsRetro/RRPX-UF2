#include "fatx.h"

int main(int argc, char **argv) {
    if (argc != 4) return 99;
    struct fatx_fs fs = {0};
    fatx_log_init(&fs, stderr, FATX_LOG_LEVEL_ERROR);
    if (fatx_open_device(&fs, argv[1], 0, 8388608, atoi(argv[3]), FATX_READ_FROM_SUPERBLOCK)) return 1;
    printf("External libfatx: FAT=%zu bytes; data begins at 0x%llx\n",
           fs.fat_size, (unsigned long long)fs.cluster_offset);
    const char *paths[] = {
        "/helper/TitleMeta.xbx", "/helper/0/SaveImage.xbx",
        "/trigger/TitleMeta.xbx", "/trigger/1/SaveImage.xbx", "/payload.xbe"
    };
    for (unsigned i = 0; i < sizeof(paths)/sizeof(paths[0]); ++i) {
        struct fatx_attr attr;
        if (fatx_get_attr(&fs, paths[i], &attr)) {
            fprintf(stderr, "FAIL: external reader cannot find %s\n", paths[i]); return 2;
        }
        unsigned char *actual = malloc(attr.file_size), *expected = malloc(attr.file_size);
        if (!actual || !expected) return 3;
        int read_status = fatx_read(&fs, paths[i], 0, attr.file_size, actual);
        if (read_status < 0) return 4;
        char host_path[2048];
        snprintf(host_path, sizeof(host_path), "%s%s", argv[2], paths[i]);
        FILE *file = fopen(host_path, "rb");
        if (!file) return 5;
        size_t bytes = fread(expected, 1, attr.file_size, file);
        int extra = fgetc(file);
        fclose(file);
        if (bytes != attr.file_size || extra != EOF || memcmp(actual, expected, bytes)) {
            fprintf(stderr, "FAIL: contents differ: %s\n", paths[i]); return 6;
        }
        printf("PASS: %s (%zu bytes), date %u-%u-%u\n", paths[i], attr.file_size,
               attr.modified.year, attr.modified.month, attr.modified.day);
        free(actual); free(expected);
    }
    fatx_close_device(&fs);
    return 0;
}
