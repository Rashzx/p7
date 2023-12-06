#define FUSE_USE_VERSION 30
#include "wfs.h"
#include <fuse.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

const char *disk_path;
void *mapped_disk;

static struct fuse_operations ops = {
    .getattr    = wfs_getattr,
    .mknod      = wfs_mknod,
    .mkdir      = wfs_mkdir,
    .read       = wfs_read,
    .write      = wfs_write,
    .readdir    = wfs_readdir,
    .unlink     = wfs_unlink,
};

int main(int argc, char *argv[]) {
    if (argc < 3 || argv[argc - 2][0] == '-' || argv[argc - 1][0] == '-') {
        fprintf(stderr, "Usage: %s [FUSE options] disk_path mount_point\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Get the disk_path and mount_point
    disk_path = realpath(argv[argc - 2], NULL);

    // Open the disk file
    int fd = open(disk_path, O_RDONLY);
    if (fd == -1) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    // Map the entire disk into memory
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        perror("Error getting file size");
        close(fd);
        exit(EXIT_FAILURE);
    }

    mapped_disk = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped_disk == MAP_FAILED) {
        perror("Error mapping file into memory");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Close the file
    close(fd);

    // Set up FUSE-specific arguments
    argv[argc - 2] = argv[argc - 1];
    argv[argc - 1] = NULL;
    --argc;

    // Initialize FUSE with specified operations
    int fuse_ret = fuse_main(argc, argv, &ops, NULL);

    // Unmap the memory
    munmap(mapped_disk, sb.st_size);

    return fuse_ret;
}
