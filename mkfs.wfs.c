#include "wfs.h"
#include "sys/stat.h" // for mode constants
#include "time.h"    // for time

static int init_fs(const char *path) {
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Error opening file");
        return -1;
    }

    struct wfs_sb supblock;
    struct wfs_inode root;

    supblock.magic = WFS_MAGIC_NUMBA;
    supblock.head = sizeof(struct wfs_sb) + sizeof(struct wfs_inode);
    
    root.inode_number = 0;
    root.deleted = 0;
    root.mode = S_IFDIR;
    root.uid = getuid();
    root.gid = getgid();
    root.flags = 0;
    root.size = 0;
    root.atime = time(NULL);
    root.mtime = time(NULL);
    root.ctime = time(NULL);
    root.links = 1;

    if (write(fd, &supblock, sizeof(struct wfs_sb)) == -1) {
        perror("Error writing superblock");
        close(fd);
        return -1;
    }

    if (write(fd, &root, sizeof(struct wfs_inode)) == -1) {
        perror("Error writing root inode");
        close(fd);
        return -1;
    }

    close(fd);

    printf("Filesystem init success at %s\n", path);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: mkfs.wfs disk_path\n");
        exit(-1);
    }

    const char *disk_path = argv[1];

    // Initialize the filesystem
    if (init_fs(disk_path) == -1) {
        fprintf(stderr, "Fail init filesystem.\n");
        exit(-1);
    }

    return 0;
}
