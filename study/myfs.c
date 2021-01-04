/*
   Simple File system in User Space

   Name: Han Yejin 
   Email : hyj97225@gmail.com
 */

#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
/* each array can store 256 strings and each string has the maximum length of 256 bytes. */
char dir_list[256][256]; //maintains the names of directories that have been created by the user
int curr_dir_idx = -1; //current index of each array

char files_list[256][256]; //maintains the names of files that have been created by the user
int curr_file_idx = -1;

char files_content[256][256]; //maintains the contents of the files
int curr_file_content_idx = -1;

void add_dir( const char *dir_name){
	printf("[add_dir] Called\n");
	printf("\tAttributes of %s requested\n", dir_name);
	curr_dir_idx++;
	//store the names of the objects instead of their paths
	strcpy(dir_list[curr_dir_idx], dir_name); 
	printf("[add_dir] Complete!!\n");
}


/*check if this path is a directory available in the filesystem
  by trying to find the name of directory in the "dir_list"*/

int is_dir( const char * path){
	path++;	//Eliminating "/" in the path, root-level-only objects
	for(int curr_idx = 0; curr_idx <=curr_dir_idx; curr_idx++)
		if( strcmp(path, dir_list[ curr_idx] ) == 0)
			return 1;
	return 0;
}

void add_file( const char *filename){
	printf("[add_file] Called\n");
	printf("\tAttributes of %s requested\n", filename);
	curr_file_idx++;
	strcpy( files_list[ curr_file_idx], filename);

	curr_file_content_idx++;
	strcpy( files_content[ curr_file_content_idx], ""); //initialize the content of this file to be an empty string
	printf("[add_file] Complete!!\n");
}

// see if there is a file with the same name exists in the filesystem.
int is_file(const char *path){
	printf("[is_file] Called\n");
	printf("\tAttributes of %s requested\n", path);
	path++;	//Eliminating "/" in the path
	for(int curr_idx = 0; curr_idx <= curr_file_idx; curr_idx++)
		if( strcmp( path, files_list[curr_idx] ) == 0)
			return 1;
	return 0;
}

int get_file_index(const char *path){
	path++; //Eliminating "/" in the path

	for(int curr_idx = 0; curr_idx <= curr_file_idx; curr_idx++){
		if( strcmp( path, files_list[curr_idx] ) ==0)
			return curr_idx;
	}
	return -1;
}

static int  write_to_file( const char *path, const char *new_content){
	printf("yejin's write_to_file start\n");
	int file_idx = get_file_index(path);

	if( file_idx == -1)
		return 0;
	strcpy(files_content[file_idx], new_content);
	int ret = sizeof(new_content);
	return ret;
}

/*
void write_to_file ( const char *path, const char *new_content){
	printf("yejin's write_to_file start!\n");
	int file_idx = get_file_index(path);

	if( file_idx == -1) // No such file
		return;
	strcpy(files_content[file_idx], new_content);
}
*/
// will be executed when the system asks for attributes of a file or a directory that 
// were stored in the mount point

static int do_getattr(const char *path, struct stat *st, struct fuse_file_info *fi){
	printf("[getattr] Called\n");
	printf("\tAttributes of %s requested\n", path);
	(void) fi;
	st->st_uid = getuid(); // The owner of the file/directory is the user who mounted the filesystem
	st->st_gid = getgid(); // The group of the file/directory is the same as the group of the user who mounted filesystem
	st->st_atime = st->st_mtime = time(NULL);
	if(strcmp(path, "/") == 0){
		st->st_mode = S_IFDIR | 0755;
		st->st_nlink = 2;
	}
	else if(is_dir(path) == 1){
		st->st_mode = S_IFDIR | 0755;
		st->st_nlink = 2;
	}
	else if(is_file(path) == 1)
	{
		st->st_mode = S_IFREG | 0644;
		st->st_nlink = 1;
		st->st_size = 1024;
	}
	else{
		return -ENOENT;
	}
	return 0;
}

// will be executed when the system asks for a list of files that were stored in the mount point
static int do_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, 
		struct fuse_file_info *fi, enum fuse_readdir_flags flags){
	printf("--> readdir: Getting The List of Files of %s\n", path);
	(void) offset;
	(void) fi;
	(void) flags;
	
	if(strcmp ( path, "/" ) != 0)
		return 0;
	filler(buffer, ".", NULL, 0, 0); //Current Directory
	filler(buffer, "..", NULL, 0, 0); //Parent Directory
	
// if the user is trying to show the files/directories of the root directory show the following
		for(int curr_idx = 0; curr_idx <= curr_dir_idx; curr_idx++)
			filler(buffer, dir_list[curr_idx], NULL, 0, 0);
		for(int curr_idx = 0; curr_idx <= curr_file_idx; curr_idx++)
			filler(buffer, files_list[curr_idx], NULL, 0, 0);
	return 0;
}

static int do_read( const char *path, char *buffer, size_t size, off_t offset, 
		struct fuse_file_info *fi){
	printf("-->Trying to read %s, %lu, %lu\n", path, offset, size);
	(void) fi;

	if(is_file(path) != 1)
		return -ENOENT;
	int file_idx = get_file_index(path);
	if(file_idx == -1)
		return -1;
	char *content = files_content[file_idx];
	size_t len;
	len = strlen(content);

	if(offset < len){
		if(offset + size > len){
			size = len-offset;
			memcpy(buffer, content + offset, size);
		}
		memcpy(buffer, content + offset, size);
	} else{
		size = 0;
	}
	return size;
}
/*
static int do_read( const char *path, char *buffer, size_t size, off_t offset,
		struct fuse_file_info *fi){
	printf("-->Trying to read %s, %lu, %lu\n", path, offset, size);
	(void) fi;

	int file_idx = get_file_index(path);

	if ( file_idx == -1)
		return -1;
	char *content = files_content[ file_idx ];
	memcpy(buffer, content + offset, size);

	return strlen(content) - offset;
}
*/
static int do_mkdir(const char *path, mode_t mode)
{
	printf("yejin's do_mkdir start!!\n");
	path++;
	add_dir(path);
	printf("yejin's do_mkdir complete!!\n");
	return 0;
}

static int do_mknod(const char *path, mode_t mode, dev_t rdev){
	printf("yejin's do_mknod start\n");
	path++;
	add_file(path);
	printf("yejin's do_mknod complete!!\n");
	return 0;
}

static int do_write(const char *path, const char *buffer, size_t size,
		off_t offset, struct fuse_file_info *info){
	
	(void) info;
	write_to_file(path, buffer);
	return size;
}

static const struct fuse_operations operations ={
	.getattr 	= do_getattr,
	.readdir 	= do_readdir,
	.read 		= do_read,
	.mkdir 		= do_mkdir,
	.mknod 		= do_mknod,
	.write 		= do_write,
};

int main(int argc, char * argv[]){
	return fuse_main(argc, argv, &operations, NULL);
}
