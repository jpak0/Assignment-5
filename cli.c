#include "kernel.h"
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

static void release_mmap_image(struct image *image) {
    if (image != NULL && image->pixels != NULL && image->width > 0 && image->height > 0) {
        void *mapping = (char *)image->pixels - sizeof(struct image);
        size_t length = sizeof(struct image) + (size_t)image->width * (size_t)image->height * sizeof(struct pixel);
        munmap(mapping, length);
        image->pixels = NULL;
    }
}

int generate_pagefault() {
    const char *filename = "pagefault.bin";
    const size_t bytes = 16 * 1024 * 1024;
    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd == -1 || ftruncate(fd, (off_t)bytes) == -1) { if (fd != -1) close(fd); return -1; }
    unsigned char *written = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (written == MAP_FAILED) { close(fd); return -1; }
    for (size_t i = 0; i < bytes; i += 4096) written[i] = (unsigned char)i;
    if (msync(written, bytes, MS_SYNC) == -1) { munmap(written, bytes); close(fd); return -1; }
    munmap(written, bytes);
    /* Discard clean cache pages on Linux so the following access needs I/O. */
#ifdef __linux__
    posix_fadvise(fd, 0, (off_t)bytes, POSIX_FADV_DONTNEED);
#endif
    unsigned char *readback = mmap(NULL, bytes, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (readback == MAP_FAILED) return -1;
    madvise(readback, bytes, MADV_DONTNEED);
    volatile unsigned int checksum = 0;
    for (size_t i = 0; i < bytes; i += 4096) checksum += readback[i];
    munmap(readback, bytes);
    unlink(filename);
    return checksum == UINT_MAX ? -1 : 0;
}

int main(int argc, char** argv){
    // TODO: parse the arguments in argv. 
    // You can expect argv[1] to be the mode
    // You can expect argv[2] to be the filepath
    // You can expect argv[3] to be the integer width
    // You can expect argv[4] to be the integer height
    // You can expect argv[5] to be the output filepath.

    if(argc != 6) {
        printf("Incorrect number of arguments. Expected: ./cli <MODE=kernel|mmap|convert|uconvert|fault> <input_image> <width> <height> <output_image_path>\n");
        return -1;
    }

    if (strcmp(argv[1], "fault") == 0) return generate_pagefault();
    char *end = NULL;
    long width = strtol(argv[3], &end, 10);
    if (*argv[3] == '\0' || *end != '\0' || width <= 0 || width > INT_MAX) return -1;
    long height = strtol(argv[4], &end, 10);
    if (*argv[4] == '\0' || *end != '\0' || height <= 0 || height > INT_MAX) return -1;
    struct image image = {.pixels = NULL, .width = (int)width, .height = (int)height};
    if (strcmp(argv[1], "convert") == 0) {
        if (loadimage(argv[2], &image) != 0) return -1;
        int result = saveimage_mmap(argv[5], &image); free(image.pixels); return result;
    }
    if (strcmp(argv[1], "uconvert") == 0) {
        if (loadimage_mmap(argv[2], &image) != 0) return -1;
        int result = saveimage(argv[5], &image); release_mmap_image(&image); return result;
    }
    int mapped = strcmp(argv[1], "mmap") == 0;
    if (!mapped && strcmp(argv[1], "kernel") != 0) return -1;
    if ((mapped ? loadimage_mmap(argv[2], &image) : loadimage(argv[2], &image)) != 0) return -1;
    int kernel[3][3] = {{1,1,1},{1,1,1},{1,1,1}};
    struct image *output = apply_kernel(&image, (int *)kernel, 3, 1.0f / 9.0f);
    if (output == NULL) { if (mapped) release_mmap_image(&image); else free(image.pixels); return -1; }
    int result = saveimage(argv[5], output);
    free(output->pixels); free(output);
    if (mapped) release_mmap_image(&image); else free(image.pixels);
    return result;
}
