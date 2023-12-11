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

/**
 * Helper method that tokenizes a string based on the '/' delimiter.
 *
 * @param str       The input string to be tokenized.
 * @return          An array of strings containing the tokens.
 *                  The array is null-terminated.
 *                  Memory is dynamically allocated, and the caller is responsible for freeing it.
 */
char** tokenize(char str[]) {
    // Initialize an array to store pointers to tokens
    char **array = malloc(MAX_LENGTH * sizeof(char*));
    if (array == NULL) { // Handle memory allocation failure
        printf("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    // Tokenize the input string with forward slashes
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

/**
 * Helper method that removes the last token from the input string and returns the modified string.
 *
 * @param str   The input string to be processed.
 * @return      A newly allocated string without the last token.
 *              The caller is responsible for freeing the returned string.
 */
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

/**
 * Helper method that retrieves the inode number associated with the given file path.
 *
 * This function tokenizes the provided file path, traverses the directory structure
 * starting from the root node, and iteratively checks each component in the tokenized path.
 * If a file is encountered, the function directly uses the data; if a directory is encountered,
 * it parses through the directory to check the existence of the next component in the path.
 *
 * @param filepath The file path for which to retrieve the inode number.
 * @return A pointer to the wfs_inode structure corresponding to the provided file path,
 *         or NULL if the file path does not exist or an error occurs.
 */
struct wfs_inode *get_inode_number_path(const char *filepath){
    int found_flag = 0;
    // Make a copy of the input filepath to avoid modifying the original string
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
        // printf("Trying to find: %s\n", tokens[i]);
        found_flag=0;
        struct wfs_dentry* entry = (struct wfs_dentry*)curr->data;

        for(int j = 0; j < (curr->inode.size/sizeof(struct wfs_dentry)); j++) {
            printf("DIR entry: %s\n", entry->name);
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

/**
 * Finds the last log entry with the specified inode number.
 *
 * This function iterates through all log entries, checking the inode portion of each entry
 * to find the last log entry with a matching inode number. It returns the corresponding log entry.
 *
 * @param inode_number The inode number to search for.
 * @return A pointer to the last log entry with the specified inode number,
 *         or NULL if no matching inode is found.
 */
struct wfs_log_entry *find_last_matching_inode(unsigned long inode_number){
    struct wfs_log_entry* last_matching_entry = NULL; // temp var for log entry, set it to null
    char *current =(char*)mapped_disk + sizeof(struct wfs_sb);

    // iterate through the superblock whilst maintaining current entry
    while (current < ((char*)mapped_disk + head)){
        struct wfs_log_entry *curr_log_entry = (struct wfs_log_entry*)(current); // temp struct for current, set it to start of disk + size of a SB struct
        // we want the time of the new to be more than the time of the old
        if (curr_log_entry->inode.inode_number == inode_number){
            last_matching_entry = (struct wfs_log_entry *)curr_log_entry;
        }
        current += curr_log_entry->inode.size + sizeof(struct wfs_inode);
        curr_log_entry = (struct wfs_log_entry *)current;
    }

    if (last_matching_entry == NULL){
        printf("inode not found, still equal to NULL"); // ENOENT
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

/**
 * Get file or directory attributes for the specified path.
 *
 * This function retrieves attribute information for the file or directory specified by the given path.
 * It populates the provided struct stat with the relevant information.
 *
 * @param path The path to the file or directory.
 * @param stbuf A pointer to the struct stat to be populated with attribute information.
 * @return 0 on success, or a negative error code on failure (e.g., -ENOENT for "No such file or directory").
 */
static int wfs_getattr(const char *path, struct stat *stbuf) {
    struct wfs_log_entry *entry  = (struct wfs_log_entry*)get_inode_number_path(path);
    if (!entry || entry == NULL) {
        // Handle the case where the specified path does not exist
        return -ENOENT;
    }
    struct wfs_inode *i = &entry->inode;
    if (!i || i == NULL) {
        return -ENOENT;
    }
        
    memset(stbuf, 0, sizeof(*stbuf)); // initialize the struct stat (stbuf) to all zeros before populating
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

/**
 * FUSE callback for reading data from a file.
 *
 * @param path The path to the file.
 * @param buf The buffer to store the read data.
 * @param size The size of the buffer.
 * @param offset The offset within the file to start reading.
 * @param fi Information about the opened file.
 * @return On success, the actual size of data read. On failure, a negative error code.
 */
static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct wfs_inode *file_inode = get_inode_number_path(path);
    if (file_inode == NULL) {
        return -ENOENT; // Handle the case where the specified path does not exist
    }
    struct wfs_log_entry *file_log_entry = (void *)file_inode;

    size_t read_size; // don't read more data than is available in the file
    if (file_inode->size > size) {
        read_size = size;
    } else {
        read_size = file_inode->size;
    }
    memcpy(buf, file_log_entry->data + offset, read_size);

    return read_size;
}

/**
 * @brief Writes data to a file in the custom file system.
 *
 * This function is called to write data to a file specified by the path.
 * It updates the file's content, size, and metadata (access time, modification time, and change time).
 *
 * @param path The path of the file to write.
 * @param buf The buffer containing the data to be written.
 * @param size The size of the data to write.
 * @param offset The offset in the file where writing should start.
 * @param fi File information (not used in this implementation).
 * @return On success, returns the number of bytes written. On failure, returns an appropriate error code.
 *         Possible error codes include -ENOENT (file does not exist).
 */
static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct wfs_inode *file_inode = get_inode_number_path(path);
    if (file_inode == NULL) {
        // Handle the case where the specified path does not exist
        return -ENOENT;
    }
    struct wfs_log_entry *file_log_entry = (struct wfs_log_entry *)file_inode;

    size_t write_size;
    if(file_inode->size>size){
        write_size = file_inode->size;
    }else{
        write_size = size;
    }
    memcpy(file_log_entry->data+offset, buf, write_size);

    // update inode
    file_inode->size = write_size;
    file_inode->atime = time(NULL);
    file_inode->mtime = time(NULL);
    file_inode->ctime = time(NULL);

    return write_size;
}

/**
 * @brief Reads the contents of a directory and fills the buffer with directory entries.
 *
 * This function is a callback registered with FUSE to handle directory listing operations.
 *
 * @param path The path of the directory to read.
 * @param buf The buffer to be filled with directory entries.
 * @param filler The filler function to add entries to the buffer.
 * @param offset The offset within the directory (unused in this implementation).
 * @param fi Information about the opened file (unused in this implementation).
 *
 * @return 0 on success, or a negative error code on failure.
 *         -ENOENT is returned if the specified path does not exist.
 */
static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) { // walk through the directory entries
    filler(buf, ".", NULL, 0);  // Add entry for current Directory
    filler(buf, "..", NULL, 0); // Add entry for parent Directory
    struct wfs_log_entry *current_log_entry = (struct wfs_log_entry*)get_inode_number_path(path);
    if (current_log_entry == NULL) {
        return -ENOENT; // Handle the case where the specified path does not exist
    }
    struct wfs_dentry *current_directory_entry = (void*) current_log_entry->data;
    size_t entries = current_log_entry->inode.size / sizeof(struct wfs_dentry); // Calculate the number of directory entries
    assert(current_log_entry->inode.size % sizeof(struct wfs_dentry) == 0); // Verify that the size is a multiple of the entry size
    // Iterate through each directory entry and add its name to the buffer
    for(size_t i = 0; i < entries; i++){
        filler(buf, (current_directory_entry + i)->name, NULL, 0);
    }
    return 0;
}

static int wfs_unlink(const char *path) { // same as deleting, set the inode delete,
    // struct wfs_log_entry *old_parent;
    // struct wfs_log_entry *new_parent;
    // struct wfs_log_entry *current_entry;

    // current_entry = get_inode_number_path(path);
    // if (current_entry == NULL) { // Handle the case where the specified path is not found
    //     printf("Error: The specified path does not exist\n");
    //     return -ENOENT; // Return -ENOENT for "No such file or directory"
    // }

    // current_entry->inode.deleted = 1; // set to deleted


    return 1;
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
