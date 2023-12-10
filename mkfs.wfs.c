#include "wfs.h"
#include <fcntl.h>   // for open
#include <unistd.h>  // for close, read, write
#include <stdio.h>   // for printf
#include <stdlib.h>  // for exit
#include <sys/stat.h> // for S_IFDIR
#include <time.h>    // for time
#include "errno.h"
#include "sys/mman.h"
#include "string.h"

int main(int argc, char *argv[]) {
    int fd;
    void* disk;
    struct stat file_stat;
	struct wfs_sb* superblock;
	struct wfs_log_entry root;

    if(argc != 2){
        return -1;
	}

    if((fd = open(argv[1], O_RDWR)) < 0)
        return -1;
    if(stat(argv[1], &file_stat) < 0)
        return -1;
    if((disk = mmap(0, file_stat.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED){
        return -1;
	}

	//printf("hi5\n");
	superblock = (struct wfs_sb*) disk;
    superblock->magic = WFS_MAGIC;
    superblock->head = sizeof(struct wfs_sb) + sizeof(struct wfs_log_entry); //size of superblock and root entry
	printf("%ld\n", sizeof(struct wfs_sb) + sizeof(struct wfs_log_entry));
	printf("%d\n", superblock->head);

	root.inode.inode_number = 0;
    root.inode.deleted = 0;
    root.inode.mode = S_IFDIR;
    root.inode.atime = time(NULL);
    root.inode.ctime = time(NULL);
    root.inode.mtime = time(NULL);
    root.inode.links = 2;
    root.inode.gid = (uid_t)getgid();
    root.inode.uid = (uid_t)getuid();
    root.inode.flags = 1;
    root.inode.size = 0; // size of the directory/file

	//printf("hi6\n");
    memcpy((char*) disk + sizeof(struct wfs_sb *), &root, sizeof(root));
    munmap(disk, sizeof(struct wfs_sb)+sizeof(struct wfs_inode));
    
    close(fd);
    return 0;
}
