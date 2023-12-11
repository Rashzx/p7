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

#define MAX_LENGTH 100

const char *disk_path;
void *mapped_disk; // starting of the superblock
int currFd; // file descriptor for the current open file
uint32_t head;
int inode_number;
int length;

struct wfs_log_entry *find_last_matching_inode(unsigned long inode_number);

/**
 * Helper method that counts the number of slashes ('/') in the given file path.
 *
 * @param filepath  A null-terminated string representing the file path.
 * @return          The count of slashes in the file path.
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

char** tokenize(char str[]) {
    char **array = malloc(MAX_LENGTH * sizeof(char*));
    if (array == NULL) { // Handle memory allocation failure
        printf("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    char *token = strtok(str, "/");
    int i = 0;
    while (token != NULL && i < MAX_LENGTH) {
        array[i++] = strdup(token); // Duplicate the token and store its pointer
        if (array[i - 1] == NULL) { // Handle memory allocation failure
            printf("Memory allocation failed");
            exit(EXIT_FAILURE);
        }
        token = strtok(NULL, "/");
    }

    array[i] = NULL; // Null-terminate the array
    return array;
}

char* remove_last_token(char str[]) {
    char** tokens = tokenize(str);
    if (tokens[0] == NULL) { // return empty string on an empty path
        printf("Error: The string is empty after tokenization\n");
        return strdup("");
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
    if (result == NULL) {
        printf("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
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

/**
 * Helper method that frees the memory allocated for an array of strings.
 *
 * This function takes an array of strings and iteratively frees each string 
 * along with the memory allocated for the array itself. The array is expected 
 * to be null-terminated.
 *
 * @param tokens An array of strings to be freed.
 */
void free_tokens(char** tokens) {
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
}

struct wfs_inode *get_inode_number_path(const char *filepath){
    int found_flag = 0;

    char input[MAX_LENGTH];
    strncpy(input, filepath, MAX_LENGTH - 1);
    input[MAX_LENGTH - 1] = '\0';  // Null-termination for strings

    
    char**tokens=tokenize(input);
    if (tokens == NULL) {
        printf("Error: Tokenization failed in get_inode_number_path()\n");
        return NULL; // Return -ENOMEM on memory allocation failure
    }

    struct wfs_log_entry *curr = find_last_matching_inode(0);
    if (curr == NULL) {
        printf("Error: Failed to find last matching inode\n");
        free_tokens(tokens);
        return NULL; // Return -ENOENT when the last matching inode is not found
    }

    int i = 0;
    if (tokens[i] == NULL) {
        free_tokens(tokens);
        return (struct wfs_inode*)curr;
    }

    while(tokens[i] != NULL) {
        found_flag=0;
        struct wfs_dentry* entry = (struct wfs_dentry*)curr->data;

        for(int j = 0; j < (curr->inode.size/sizeof(struct wfs_dentry)); j++) {
            if(strcmp(tokens[i],entry->name) == 0) {
                curr = find_last_matching_inode(entry->inode_number);
                if (curr == NULL) {
                    printf("Error: Failed to find last matching inode\n");
                    free_tokens(tokens);
                    return NULL; //ERR_PTR(-ENOENT)
                }
                found_flag = 1;
            }
            entry++;
        }
        i++;
    }
    free_tokens(tokens);
    if(found_flag == 0){
        return NULL; // Return -ENOENT when the file/directory does not exist
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

/**
 * Finds the highest inode number currently in use.
 *
 * This function starts at inode number 0 for the root node and iterates
 * through log entries until it finds the last matching inode number.
 *
 * @return The highest inode number currently in use.
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

    /*If a field is meaningless or semi-meaningless (e.g., st_ino) then it should be set to 0 or given a "reasonable" value.*/
    stbuf->st_dev = 0;
    stbuf->st_blksize = 0;
    stbuf->st_blocks = 0;
    stbuf->st_rdev = 0;
    return 0;
}

static int wfs_mknod(const char *path, mode_t mode, dev_t dev) {
    char x[MAX_LENGTH];
    strcpy(x,path);

    char y[MAX_LENGTH];
    strcpy(y,remove_last_token(x));
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
    for(int i = 0; i < MAX_LENGTH; i++) {
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
        .mode=__S_IFREG|mode,
        .uid=getuid(),
        .gid=getgid(),
        .size=0,
        .atime=time(NULL),
        .mtime=time(NULL),
        .ctime=time(NULL),
        .links=1,
    };

    memcpy((char*)mapped_disk+head, &iii,sizeof(iii));
    struct wfs_sb *sb=(void*)mapped_disk;
    head+=sizeof(iii);
    sb->head = head;

    return 0;
    /*
    create a new log entry for the parent directory that holds the file that you are making
    make a log entry for the file
    check the stat of the file
    */
}

static int wfs_mkdir(const char *path, mode_t mode) {
printf("mkdir called\n");
    char x[MAX_LENGTH];
    strcpy(x,path);

    char y[MAX_LENGTH];
    strcpy(y,remove_last_token(x));
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
    for(int i = 0; i < MAX_LENGTH; i++) {
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
        .mode= __S_IFDIR | mode,
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
    char *fullpath;
    
    fullpath = strdup(path);

    struct wfs_log_entry *log_entr = (struct wfs_log_entry*)get_inode_number_path(fullpath);

    size_t updated_size;

    if(log_entr->inode.size > size){
        
        updated_size = log_entr->inode.size;
    }
    else{

        updated_size = size;
    
    }

    memcpy(log_entr->data + offset, buf, updated_size);

    log_entr->inode.size = updated_size;
    log_entr->inode.mtime = time(NULL);
    log_entr->inode.atime = time(NULL);

    free(fullpath);
    return updated_size;}

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

    char *fullpath;

    fullpath = strdup(path);

    char *beforeLastSlash;
    beforeLastSlash = malloc(sizeof(char) * 256);
    const char *lastSlash = strrchr(fullpath, '/');
    
    if (lastSlash != NULL) {

        if(lastSlash == fullpath) {
            strcpy(beforeLastSlash, "/");
        }
        else {
            ptrdiff_t length = lastSlash - fullpath;
            strncpy(beforeLastSlash, fullpath, length);
            beforeLastSlash[length] = '\0';
        }

    }

    struct wfs_log_entry *subDirOfDelete  = (struct wfs_log_entry*)get_inode_number_path(beforeLastSlash);
    struct wfs_log_entry *toDelete = (struct wfs_log_entry*)get_inode_number_path(fullpath);
    if(subDirOfDelete == (struct wfs_log_entry *) NULL || toDelete == (struct wfs_log_entry *) NULL) {

        return -ENOENT;

    }

    unsigned long inode_num = toDelete->inode.inode_number;
    struct wfs_log_entry *curr = (struct wfs_log_entry*)( (char*) mapped_disk + sizeof(struct wfs_sb));

    while(curr < (struct wfs_log_entry *) ((char*)mapped_disk + ((struct wfs_sb *)mapped_disk)->head)) {

        if(curr->inode.deleted == 0 && curr->inode.inode_number == inode_num) {

            curr->inode.deleted = 1;
        
        }

        curr = (struct wfs_log_entry*) ((char *)curr + sizeof(struct wfs_inode) + curr->inode.size);
    }


    struct wfs_log_entry* subDir = subDirOfDelete;
    unsigned long inodeToDel = toDelete->inode.inode_number;
    struct wfs_log_entry *newLogEntry = (struct wfs_log_entry *) ((char *) mapped_disk + ((struct wfs_sb *)mapped_disk)->head);

    memcpy(newLogEntry, &subDir->inode, sizeof(struct wfs_inode));

    int inDirectory = subDir->inode.size / sizeof(struct wfs_dentry);

    struct wfs_dentry *currDEntry = (struct wfs_dentry *) (subDir->data);
    struct wfs_dentry *currData = (struct wfs_dentry *) ((char *)newLogEntry + sizeof(struct wfs_inode));



    for(int i = 0; i < inDirectory; i++) {

        if(currDEntry->inode_number != inodeToDel) {

            currData->inode_number = currDEntry->inode_number;
            strcpy(currData->name, currDEntry->name);
            currData++;

        }

        currDEntry++;

    }

    newLogEntry->inode.size -= sizeof(struct wfs_dentry);



    return 0;


    // struct wfs_log_entry *old_parent;
    // struct wfs_log_entry *new_parent;
    // struct wfs_log_entry *current_entry;

    // current_entry = get_inode_number_path(path);
    // if (current_entry == NULL) { // Handle the case where the specified path is not found
    //     printf("Error: The specified path does not exist\n");
    //     return -ENOENT; // Return -ENOENT for "No such file or directory"
    // }

    // current_entry->inode.deleted = 1; // set to deleted

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

/**
 * Mounts a WFS (Writeable File System) using FUSE.
 *
 * @param argc      The number of command-line arguments.
 * @param argv      An array of strings representing the command-line arguments.
 *                 Expected format: mount.wfs [FUSE options] disk_path mount_point
 * @return          The exit status of the FUSE filesystem operation.
 *                 Returns 0 on success, non-zero on failure.
 *                 Refer to FUSE documentation for specific error codes.
 */
int main(int argc, char *argv[]) {
    // if (argc < 3 || strcmp(argv[0], "./mount.wfs") != 0 || argv[argc - 2][0] == '-' || argv[argc - 1][0] == '-') {
    if (argc < 3 || argv[argc - 2][0] == '-' || argv[argc - 1][0] == '-') { // checks from fuse website
        printf("Usage: mount.wfs [FUSE options] disk_path mount_point\n");
        exit(EXIT_FAILURE);
    }
    disk_path = argv[argc-2]; // get disk path from the second last parameter of the string
    int file_descriptor = open(disk_path, O_RDWR); // check that the disk path is valid, opening using 
    if (file_descriptor == -1) {
        printf("Error opening file");
        exit(EXIT_FAILURE);
    }

    // Create a struct to hold the information from the file
    struct stat stat_info;
    if (fstat(file_descriptor, &stat_info) == -1) { //check error condition for error during operation
        printf("Error getting file size");
        close(file_descriptor);
        exit(EXIT_FAILURE);
    }
    stat(disk_path,&stat_info); // gets info from the disk path and stores into stat info

    // mmap the starting of the disk
    length = stat_info.st_size;
    mapped_disk = mmap(0, length, PROT_READ|PROT_WRITE, MAP_SHARED, file_descriptor, 0);
    if (mapped_disk == (void *)-1) {
        printf("Error mapping file into memory\n");
        close(file_descriptor);
        exit(EXIT_FAILURE);
    }
    close(file_descriptor);

    // Set the global variable for head
    struct wfs_sb *sb=(void*)mapped_disk;
    head = sb->head;

    // code from https://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/html/init.html
    // building new argument vector from argc and argv, without the disk
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;

    int fuse_ret = fuse_main(argc, argv, &ops, NULL); // start fuse
    // sb->head = head;

    // Unmap the memory
    munmap(mapped_disk, length);
    return fuse_ret;
}
