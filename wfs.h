#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "wfs.h"
#include "sys/stat.h"
#include "time.h"

static int init_fs(const char *path) {
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Error opening file");
        return -1;
    }

    struct wfs_sb supblock;
    struct wfs_inode root;

    supblock.magic = WFS_MAGIC;
    supblock.head = sizeof(struct wfs_sb) + sizeof(struct wfs_inode);
    
    // creating the root
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

    if (fwrite(fd, &supblock, sizeof(struct wfs_sb)) == -1) {
        perror("Error writing superblock");
        close(fd);
        return -1;
    }

    if (fwrite(fd, &root, sizeof(struct wfs_inode)) == -1) {
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


#ifndef MOUNT_WFS_H_
#define MOUNT_WFS_H_

#define MAX_FILE_NAME_LEN 32
#define WFS_MAGIC 0xdeadbeef

struct wfs_sb {
    uint32_t magic;
    uint32_t head;
};

struct wfs_inode {
    unsigned int inode_number;
    unsigned int deleted;       // 1 if deleted, 0 otherwise
    unsigned int mode;          // type. S_IFDIR if the inode represents a directory or S_IFREG if it's for a file
    unsigned int uid;           // user id
    unsigned int gid;           // group id
    unsigned int flags;         // flags
    unsigned int size;          // size in bytes
    unsigned int atime;         // last access time
    unsigned int mtime;         // last modify time
    unsigned int ctime;         // inode change time (the last time any field of inode is modified)
    unsigned int links;         // number of hard links to this file (this can always be set to 1)
};

struct wfs_dentry {
    char name[MAX_FILE_NAME_LEN];
    unsigned long inode_number;
};

struct wfs_log_entry {
    struct wfs_inode inode;
    char data[];
};

#endif
