#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <errno.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include "wfs.h"
#include "assert.h"

const char *disk_path;
void *mapped_disk; // starting of the superblock
int currFd; // file descriptor for the current open file
uint32_t head;
int inode_number;
int length;

struct wfs_log_entry *find_last_matching_inode(unsigned long inode_number);

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

char** tokenize(char str[]) {
    int i = 0;
    char *p = strtok(str, "/");
    char **array = malloc(100 * sizeof(char*));  // Allocate memory for an arra>

    while (p != NULL && i < 100) {
        array[i++] = strdup(p);  // Duplicate the token and store its pointer
        p = strtok(NULL, "/");
    }

    array[i] = NULL;  // Null-terminate the array

    return array;
} 

char* removeLastToken(char str[]) {
    char** tokens = tokenize(str);

    if (tokens[0] == NULL) {
        char* result = malloc(1);
        result[0] = '\0';
        return result;
    }

    int lastTokenIndex = 0;
    while (tokens[lastTokenIndex + 1] != NULL) {
        lastTokenIndex++;
    }

    int lengthWithoutLastToken = 0;
    for (int i = 0; i < lastTokenIndex; i++) {
        lengthWithoutLastToken += strlen(tokens[i]) + 1; // Add 1 for the '/'
    }

    char* result = malloc(lengthWithoutLastToken + 1); // Add 1 for the null terminator

    result[0] = '\0';
    for (int i = 0; i < lastTokenIndex; i++) {
        strcat(result, tokens[i]);
        strcat(result, "/");
    }

    if (lengthWithoutLastToken > 0) {
        result[lengthWithoutLastToken - 1] = '\0';
    }

    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);

    return result;
}


// two helper methods
// 1. get inode number from path
/*
    - tokenize by using slashes
        - use the count slashes method as a sanity check for later
    - traverse the directory starting from the root node and then by each component in the tokenized path
        - check if wfs_inode has file or directory
        - if file, can directly check and use data (you can stop, if mismatch and then there is error)
        - if directlry, have to parse through the directory to check, if next one exists good, if not error
    - return the inode number
*/

struct wfs_inode *get_inode_number_path(const char *filepath){


    int flag=0;
  //  printf("get_inode path %s\n",path);

    char input[100];
    for(int i = 0; i < 100; i++) {
        input[i] = filepath[i];
    }
    char**tokens=tokenize(input);

    struct wfs_log_entry *curr = find_last_matching_inode(0);
   // printf("curr %p\n",(void*)curr);
 //   printf("size: %ld\n", curr->inode.size/sizeof(struct wfs_dentry));

    int i = 0;
    if(tokens[i] == NULL)   return (struct wfs_inode*)curr;
    while(tokens[i] != NULL) {
        printf("Trying to find: %s\n", tokens[i]);
        flag=0;
        struct wfs_dentry*e=((void*)curr->data);
        for(int j = 0; j < curr->inode.size/sizeof(struct wfs_dentry); j++) {
            printf("DIR entry: %s\n", e->name);
            if(strcmp(tokens[i],e->name)==0) {
                curr = find_last_matching_inode(e->inode_number);
                flag=1;
            }
            e++;
        }
        i++;
    }

    if(flag==0){
        return NULL;
    }else{
        return (struct wfs_inode*)curr; 
    }
}

// 2. get inode from inode number (get log entry from inode number)
    // how to get the superblock
    /*
        while loop through every single log entry, bound to get the inode number
        check the inode portion of the log entry, because there is no imap
        check the inode number in the inode portion (simple check to match) .inode.inodenumber, return the inode
        do not break when you get matching inode, you need the last entry in the log with matching inode
            return that one :D
        if not, continue
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
struct wfs_log_entry *find_last_matching_inode(unsigned long inode_number){
    struct wfs_log_entry* last_matching_entry = NULL; // temp var for log entry, set it to null
    // struct wfs_sb* superblock_start = (struct wfs_sb*)mapped_disk; // temp struct for start, set it to mapped disk

    char *current =(char*)mapped_disk + sizeof(struct wfs_sb);

    // iterate through the superblock whilst maintaining current entry
    while (current < ((char*)mapped_disk + head)){
        struct wfs_log_entry* curr_log_entry = (struct wfs_log_entry*)(current); // temp struct for current, set it to start of disk + size of a SB struct
        // we want the time of the new to be more than the time of the old
        if (curr_log_entry->inode.inode_number == inode_number){
            last_matching_entry = (struct wfs_log_entry *)curr_log_entry;
        }
        // curr_log_entry += sizeof(struct wfs_inode);
        // curr_log_entry = (struct wfs_log_entry*)((char*)curr_log_entry + sizeof(struct wfs_inode) + curr_log_entry->inode.size); // pointer jump to the next 
        current += curr_log_entry->inode.size + sizeof(struct wfs_inode);
        curr_log_entry = (struct wfs_log_entry *)current;
    }

    if (last_matching_entry == NULL){
        printf("inode not found, still equal to NULL");
    }
    return last_matching_entry;
}

/*
helper method to find the highest inode number (used to find the next inode number to insert in at)
start at a 0 for root node, set it equal to a var
while loop that stops when find_last_matching_inode's log entry inode's inode number is not equal to the var
if the log entry inode is equal to the var, break out of the while loop
increment the var
return the var
*/
unsigned int find_current_highest_inode_number(){
    unsigned int current_inode_number = 0;
    struct wfs_log_entry *temp_log_entry;
    while(1){
        temp_log_entry = find_last_matching_inode((unsigned long)current_inode_number);
        // if (temp_log_entry.inode.inode_number == NULL || temp_log_entry.inode.inode_number != current_inode_number){
        if (temp_log_entry->inode.inode_number != current_inode_number){
            break;
        }
        current_inode_number++;
    }

    // Now you can use temp_log_entry outside of the loop
    return temp_log_entry->inode.inode_number;
}

/*
look at the logentry as you're looking at code and debugging
    - always organized as inode and data
    - print the inode
    - printInode(wfs.inode i), prints out everything about the inode
    - printDirEntry()
    - printLogEntry(wfs_log_entry)
        - if inode is directory, have to parse the directory entry
*/

static int wfs_getattr(const char *path, struct stat *stbuf) {
    struct wfs_log_entry *logx = (struct wfs_log_entry*)get_inode_number_path(path);
//    if (e->inode_number == -1){
  //      printf("function returned -1, meaning file doesn't exist\n");
    //    return(-1);
 //   }
 //   if (logx->data == NULL){
   //     printf("function returned NULL, meaning file doesn't exist\n");
  //      // return(-1);
 //       return -ENOENT;
 //   }

    struct wfs_inode *i = &logx->inode;
      if (!i)
        return -ENOENT;

    memset(stbuf, 0, sizeof(*stbuf));
    stbuf->st_uid = i->uid;
    stbuf->st_gid = i->gid;
    stbuf->st_atime = i->atime;
    stbuf->st_mtime = i->mtime;
    stbuf->st_mode = i->mode;
    stbuf->st_nlink = i->links;
    stbuf->st_size = i->size;

    /*
    If a field is meaningless or semi-meaningless (e.g., st_ino) then it should be set to 0 or given a "reasonable" value.
    //    stbuf->st_dev = 0;
    //    stbuf->st_blksize = 0;
    //    stbuf->st_blocks = 0;
    //    stbuf->st_rdev = 0;
    */
    return 0;
}

static int wfs_mknod(const char *path, mode_t mode, dev_t dev) {
    char x[100];
    strcpy(x,path);

    char y[100];
    strcpy(y,removeLastToken(x));
    if(strlen(y)==0){
        strcpy(y,"/");
    }

    struct wfs_inode *i=get_inode_number_path(y);
    struct wfs_log_entry *e=(void*)i;

    size_t size=sizeof(struct wfs_log_entry)+sizeof(struct wfs_dentry)+i->size;
    struct wfs_log_entry *new_entry=malloc(size);
    memcpy(new_entry,i,sizeof(*i));
    memcpy(new_entry->data,e->data,i->size);
    new_entry->inode.size+=sizeof(struct wfs_dentry);

    struct wfs_dentry *d=(void*)(new_entry->data+i->size);

    char input[25];
    for(int i = 0; i < 100; i++) {
        input[i] = path[i];
    }
    char**tokens=tokenize(input);
    int k=0;
    char z[25];
    while(tokens[k]!=NULL){
        strcpy(z,tokens[k]);
        k++;
    }
    strcpy(d->name,z);

    inode_number++;
    d->inode_number=inode_number;

    memcpy((char*)mapped_disk+head,new_entry,size);

    head+=size;
    free(new_entry);

    struct wfs_inode iii={
        .inode_number=inode_number,
        .deleted=0,
        .mode=S_IFREG|mode,
        .uid=getuid(),
        .gid=getgid(),
        .size=0,
        .atime=time(NULL),
        .mtime=time(NULL),
        .ctime=time(NULL),
        .links=1,
    };

    memcpy((char*)mapped_disk+head, &iii,sizeof(iii));

    head+=sizeof(iii);

    return 0;
    /*
    create a new log entry for the parent directory that holds the file that you are making
    make a log entry for the file
    check the stat of the file
    */
}

static int wfs_mkdir(const char *path, mode_t mode) {
printf("mkdir called\n");
    char x[100];
    strcpy(x,path);

    char y[100];
    strcpy(y,removeLastToken(x));
    if(strlen(y)==0){
        strcpy(y,"/");
    }

    struct wfs_inode *i=get_inode_number_path(y);
    struct wfs_log_entry *e=(void*)i;

    size_t size=sizeof(struct wfs_log_entry)+sizeof(struct wfs_dentry)+i->size;
    struct wfs_log_entry *new_entry=malloc(size);
    memcpy(new_entry,i,sizeof(*i));
    memcpy(new_entry->data,e->data,i->size);
    new_entry->inode.size+=sizeof(struct wfs_dentry);

    struct wfs_dentry *d=(void*)(new_entry->data+i->size);

    char input[25];
    for(int i = 0; i < 100; i++) {
        input[i] = path[i];
    }
    char**tokens=tokenize(input);
    int k=0;
    char z[25];
    while(tokens[k]!=NULL){
        strcpy(z,tokens[k]);
        k++;
    }
    strcpy(d->name,z);

    inode_number++;
    d->inode_number=inode_number;

    memcpy((char*)mapped_disk+head,new_entry,size);

    head+=size;
    free(new_entry);

    struct wfs_inode iii={
        .inode_number=inode_number,
        .deleted=0,
        .mode=S_IFDIR|mode,
        .uid=getuid(),
        .gid=getgid(),
        .size=0,
        .atime=time(NULL),
        .mtime=time(NULL),
        .ctime=time(NULL),
        .links=1,
    };

    memcpy((char*)mapped_disk+head, &iii,sizeof(iii));

    head+=sizeof(iii);

    return 0;
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct wfs_inode *i=get_inode_number_path(path);
    struct wfs_log_entry*e=(void *)i;

    size_t new_size;
    if(i->size>size){
        new_size=i->size;
    }else{
        new_size=size;
    }

    memcpy(buf, e->data+offset, new_size);

    return new_size;
}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct wfs_inode *i = get_inode_number_path(path);
    struct wfs_log_entry *e=(void *)i;

    size_t new_size;
    if(i->size>size){
        new_size=i->size;
    }else{
        new_size=size;
    }

    memcpy(e->data+offset, buf, new_size);

    // update inode
    i->size = new_size;
    i->atime = time(NULL);
    i->mtime = time(NULL);
    i->ctime = time(NULL);

    return new_size;
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) { // walk through the directory entries
    
    

    filler(buf, ".", NULL, 0);  // Current Directory
    filler(buf, "..", NULL, 0); // Parent Directory
    struct wfs_log_entry *log = (struct wfs_log_entry*)get_inode_number_path(path);
    struct wfs_dentry* dentry = (void*) log->data;

    size_t entries = log->inode.size/sizeof(struct wfs_dentry);
    assert(log->inode.size % sizeof(struct wfs_dentry) == 0);

    for(size_t i = 0; i < entries; i++){
        filler(buf, (dentry + i) -> name, NULL, 0);
    }
    return 0;
}

static int wfs_unlink(const char *path) { // same as deleting, set the inode delete,

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
    disk_path = argv[argc-2];
    int file_descriptor = open(disk_path, O_RDWR); // check that the disk path is valid
    struct stat stat_info;
    stat(disk_path,&stat_info);
    int mapping_length = stat_info.st_size;
    mapped_disk = mmap(0, mapping_length, PROT_READ|PROT_WRITE, MAP_SHARED, file_descriptor, 0);
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;
    struct wfs_sb*sb=(void*)mapped_disk;
    head = sb->head;
    length=stat_info.st_size;

    fuse_main(argc, argv, &ops, NULL);
    sb->head = head;

    // Unmap the memory
    munmap(mapped_disk, mapping_length);
    close(file_descriptor);
    return 0;
}
