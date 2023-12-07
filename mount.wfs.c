#define FUSE_USE_VERSION 30
#define _POSIX_C_SOURCE 200809L
#include "wfs.h"
#include <fuse.h>
#include <errno.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>


const char *disk_path;
void *mapped_disk; // starting of the superblock
int currFd; // file descriptor for the current open file

// two helper methods
// 1. get inode number from path
/*

*/
// 2. get inode from inode number (get log entry from inode number)
/*
while loop through every single log entry, bound to get the inode number
check the inode portion of the log entry, because there is no imap
check the inode number in the inode portion (simple check to match) .inode.inodenumber, return the inode
do not break when you get matching inode, you need the last entry in the log with matching inode
    return that one :D
if not, continue

*/
    // Your implementation here
    // how to get the superblock
    /*
        make variable superblock of type wfs_sb (magic num)
        declare a variable superblock
        
        struct wfs_sb superblock = (wfs_sb)mapped_disk; // now it's just a pointer
        superblock.head tells us the head of the superblock
        struct wfs_log_entry root = (wfs_log_entry) mapped_disk + sizeof(superblock);
        loop through log?
        declare a wfs_log_entry currtemppointer
        currtemppointer += sizeof (wfs_inode) + wfs_inode.size = pointer jump to next inode
        stop when you get to the end of what's written (the wfs_sb.head), run the while loop until you gett o the head
        return the logentry_inode NO WRONG
        return a wfs_log_entry,     unsigned int inode_number;

    */
   

int count_slashes(const char *filepath) {
    int count = 0;

    while (*filepath) {
        if (*filepath == '/') {
            count++;
        }
        filepath++;
    }

    return count;
}

static int wfs_getattr(const char *path, struct stat *stbuf) {

    return 0;
}

static int wfs_mknod(const char *path, mode_t mode, dev_t dev) {
    // Your implementation here
    return 0;
}

static int wfs_mkdir(const char *path, mode_t mode) {
    // Your implementation here
    return 0;
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    // Your implementation here
    return 0;
}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    // Your implementation here
    return 0;
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    printf("at the readdirectory wfs\n");
    return 0;
}

static int wfs_unlink(const char *path) {
    // Your implementation here
    return 0;
}

static struct fuse_operations ops = {
    .getattr    = wfs_getattr,
    .mknod      = wfs_mknod,
    .mkdir      = wfs_mkdir,
    .read       = wfs_read,
    .write      = wfs_write,
    .readdir    = wfs_readdir,
    .unlink     = wfs_unlink,
};

// take disk out , build a new argument vector without disk
// check that argc is passed in correctly 
// keep argument 0, mount.wfs is the first argument
int main(int argc, char *argv[]) {
    if (argc < 3 || strcmp(argv[0], "./mount.wfs") != 0 || argv[argc - 2][0] == '-' || argv[argc - 1][0] == '-') {
        printf("Usage: mount.wfs [FUSE options] disk_path mount_point\n");
        exit(-1);
    }

    // disk_path = realpath(argv[argc - 2], NULL); // second to last is disk path

    disk_path = argv[argc-2];

    int file_descriptor = open(disk_path, O_RDONLY); // check that the disk path is valid
    if (file_descriptor == -1) {
        printf("Error opening file");
        exit(-1);
    }

    struct stat stat_info;
    if (fstat(file_descriptor, &stat_info) == -1) { //check error condition for error during operation
        printf("Error getting file size");
        close(file_descriptor);
        exit(-1);
    }

    int mapping_length = stat_info.st_size;
    mapped_disk = mmap(NULL, mapping_length, PROT_READ, MAP_PRIVATE, file_descriptor, 0);
    if (mapped_disk == (void *)-1) {
        printf("Error mapping file into memory");
        close(file_descriptor);
        exit(-1);
    }
    close(file_descriptor);

    // const char *fuse_argv[argc - 1];
    // for (int i = 0; i < argc -2; ++i){
    //     fuse_argv[i] = argv[i];
    // }

    // fuse_argv[argc - 2] = argv[argc - 1];
    // fuse_argv[argc - 1] = NULL;
    // int fuse_ret = fuse_main(argc - 1, fuse_argv, &ops, NULL);

    // https://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/html/init.html
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;

    int fuse_ret = fuse_main(argc, argv, &ops, NULL);

    // Unmap the memory
    munmap(mapped_disk, mapping_length);

    return fuse_ret;
}
