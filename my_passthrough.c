/*
    FUSE: Filesystem in USErspace
*/

/*
 *
 * 현 시스템의 루트부터 시작해서 파일 시스템 계층구조를 보여주는 FUSE file system
 *
 * This is implemented by just "passing through" all requests 
 * to the corresponding user-space libc functions.
 * 
 * But its performance is terrible. // 성능 측정해보기(gettimeofday, ...)
 * 
 *
 * Compile with
 *
 * gcc -Wall my_passthrough.c `pkg-config fuse3 --cflags --libs` -o my_passthrough
 *
 * ## Source code ##
 * \include my_passthrough.c
 */

#define FUSE_USE_VERSION 31

/* 
 ** 구현에 따라 존재하지 않을 수도 있는 매크로의 사용 **
    경우에 따라 특정 매크로가 일부 유닉스 구현에는 정의되어 있지 않을 수도 있기 때문에
    이식성 있게 처리하려면 C전처리기 지시자 #ifdef을 써서 처리한다. */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/* glibc 고유의 기능 테스트 매크로
   _GNU_SOURCE: (어떤 값으로든)정의되어 있으면, 이상의 모든 매크로를 설정해서 모든 기능을 제공할 뿐만 아니라
                다양한  GNU 확장 기능도 제공한다. */
#define _GNU_SOURCE


#ifdef linux
/* For pread()/pwrite()/utimesat() */
/* 
_XOPEN_SOURCE가 어떤 값으로든 정의되어 있으면 POSIX.1, POSIX.2, X/Open(XPG4)기능을 제공한다. 
    - 500 이상의 값으로 정의: SUSv2(유닉스 98과 XPG5) 확장 기능 제공
    - 600 이상의 값으로 정의: 추가로 SUSv3 XSI(유닉스 03) , C99 확장 기능 제공
    - 700 이상의 값으로 정의: SUSv4 XSI 확장 기능 제공
*/
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h> // 여러 시스템콜 proto type: access(), chdir(), fsync(), getcwd, unlink, truncate, ...
#include <fcntl.h> // fcntl()과 open()을 위한 자료형,상수,함수 정의
#include <sys/stat.h> // 파일의 상태를 확인하기 위한 자료형, 구조체, 상수와 관련된 함수 정의:fstat, ...
#include <dirent.h> // 파일시스템의 디렉터리를 나타내기 위한 구조체 정의: closedir/opendir/readdir/...
#include <errno.h> // errno 변수와 에러 상수 정의

#ifdef __FreeBSD__
#include <sys/socket.h> //네트워크 통신을 위한 소켓 인터페이스를 위한 자료형,구조체,함수정의
#include <sys/un.h> //유닉스 도메인 소켓을 위해 sockaddr_un구조체를 정의
#endif

#include <sys/time.h> //시스템 시간을 위한 자료형, 상수, 함수 정의: gettimeofday(), settimeofday(),...

#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include "my_passthrough_helpers.h"

/* 함수 원형: void* (* init) (struct fuse_conn_info *conn, struct fuse_config *cfg) */
/* Initialize filesystem, 파일시스템이 mount될 때 가장 먼저 호출되는 함수.
   The return value will passed in the ``private_data field`` of ``struct fuse_context``
   (Extra context that may be needed by some file systems) to all file operations, and 
   as a parameter to the destroy() method. (`` void(*fuse_operations::destroy) (void *private_data)``)
   It overrides the initial value provided to fuse_main() / fuse_new().

   @param fuse_conn_info *conn: Connection information, passed to the ->init()method
   @param fuse_config *cfg: Configuration of the high-level API, initialized from the arguments
                            passed to fuse_new(), and then passed to the fs's init() handler which
                            should ensure that the configuration is compatible with the fs implementation.
*/
static void *myfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    printf("[myfs_init] Called\n");
    (void) conn;
    /*
        struct fuse_config { ... ``use_ino`` ...}
        This value is used to fill in the st_ino field in the stat, lstat, fstat functions
        and the d_ino field in the readdir function. 
        **************************************************************************************************************
        //ino_t st_ino: The file serial number, which distinguishes this file from all other files on the same device//
        **************************************************************************************************************
        The filesystem does not have to guarantee uniqueness, 
        however some applications rely on this value being unique for the whole filesystem.
    */
    cfg->use_ino = 1;

    /* 
     *    Pick up changes form lower filesystem !!right away!!. This is also necessary for better hardlink support.
     *    When the kernel calls the unlink() handler, it does not know the inode of the to-be-removed entry and
     *    can therefore not invalidate the cache of the associated inode - resulting in an incorrect st_nlink value
     *    being reported for any remaining hardlinks to this inode.
    */

    /* entry_timeout=T
       The timeout in seconds for which name lookups will be cached. The default is 1.0 second.
       For all the timeout options it is possible to give fractions of a second as well (e.g. entry_timeout=2.8)
    */
    cfg->entry_timeout = 0; // name lookups are not cached.

    /* attr_timeout=T
       The timeout in seconds for which file/directory attributes are cached. The default is 1.0 second.
    */
    cfg->attr_timeout = 0; //file/directory attributes are not cached.

    /* negative_timeout=T
       The timeout in seconds for which a negative lookup will be cached. This means, that if file did not 
       exist (lookup returned ENOENT), the lookup will only be redone after the timeout, and the file/directory
       will be assumed to not exist until then. The default is 0.0 second, meaning that 
       caching negative lookups are disabled. 
    */
    cfg->negative_timeout = 0; // negative lookups are not cached.
    return NULL;
}

/* 함수 원형: int (* getattr) (const char *, struct stat *, struct fuse_file_info *fi) */
/* Get file attributes 
   Similar to stat(). The 'st_dev' and 'st_blksize'fields are ignored. The 'st_ino'field is ignored except
   if the 'use_ino' mount option is given. In that case it is passed to userspace, but libfuse and the kernel
   will still assign a different inode for internal use (called the "nodeid").

   `fi` will always be NULL if the file is not currently open, but may also be NULL if the file is open.
   @param fuse_file_info *fi:
                Information about an open file. File Handles are created by the open, opendir, and create 
                method and closed by the release and releasedir methods. Multiple file handles may be 
                concurrently open for the same file. Generally, a client will create one file handle
                per file descriptor, though in some cases multiple fd can share a single file handle.
*/
static int myfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    (void) fi;
    int res;
    res = lstat(path, stbuf); // path에 위치한 파일의 정보를 얻어옴
    if(res == -1)  // 실패시 -1, 성공시 0
        return -errno;
    return 0;
}

/* 함수 원형: int (*access)(const char *, int) */
/* 파일의 사용자 권한을 체크하기 위해서 사용하지만, 보통 파일이 존재하는지 간단히 체크하기 위해 사용
   (단지 파일의 존재유무를 파악하기 위해 open을 사용하는 것은 너무 번거롭기 때문)
   If the 'default_permissions' mount option is given, this method is not called.
*/
static int myfs_access(const char* path, int mask)
{
    int res;
    /*
        int access(const char *pathname, int mode);
        path로 지정된 파일에 대해 읽기, 쓰기, 실행 권한을 가지고 있는지 체크
        만약 path 파일이 심볼릭 링크된 파일이면 원본 파일을 체크함
        mode: 1) R_OK, 2) W_OK, 3) X_OK, 4) F_OK(파일의 존재 유무)
    */
    res = access(path, mask); // path에 있는 file에 mask 권한이 존재하는지 확인
    
    if(res == -1) // mask 권한 가지고 있다면 0, 아니면 -1
        return -errno;
    return 0;
}

/*** 함수 원형: int (*readlink)(const char *, char *, size_t) ***/
/* Read the target of a symbolic link. The buffer should be filled with a null terminated string.
   The buffer size argument includes the space for the terminating null character.
   If the linkname is too long to fit in the buffer, it should be truncated.
   The return value should be 0 for success.
*/
static int myfs_readlink(const char* path, char *buf, size_t size)
{
    int res;
    /*
        int readlink(const char *path, char *buf, size_t bufsize);
        심볼릭 링크인 path가 가리키는 원본의 파일 이름을 돌려줌. 알아낸 원본 파일의 이름은
        buf에 저장된다. 만약 buf의 크기가 원본 파일의 이름을 담기에 충분히 크지 않다면 
        나머지 부분은 잘리게 된다. readlink는 원본파일의 완전한 경로를 가져온다.
        성공할 경우 버퍼에 들어 있는 문자의 갯수가 반환되며, 에러가 발생되면 -1이 리턴되고
        적당한 errno 코드가 설정된다.
    */
    res = readlink(path, buf, size-1);
    if(res == -1)
        return -errno;
    buf[res] = '\0';
    return 0;
}

/* 함수 원형: int (*readdir)(const char *, void *, fuse_fill_dir_t, off_t, struct fuse_file_info*, enum fuse_readdir_flags) */
/* Read directory. 
       디렉토리의 inode를 읽어 해당 디렉토리가 위치한 block주소 위치를 알아내어 
       디렉토리 entry를 읽어 들인다.
   The filesystem may choose between two modes of operation.
   1) The readdir implementation ignores the offset parameter, and passes zero to the
   filler function's offset. The filler function will not return '1', so the whole
   directory is read in a single readdir operation.
   2) The readdir implementation keeps track of the offsets of the directory entries. It uses the offset parameter
   and always passes non-zero offset to the filler function. When the buffer is full (or an error happes) the
   filler function will return '1'.
*/
static int myfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
    DIR *dp;
    struct dirent *de;
    (void) fi;
    (void) offset;
    (void) flags;
    /* 디렉토리 이름을 인수로 받아 해당 디렉토리 스트림을 연다. 정상적이면 DIR* 포인터가 리턴,
       실패하면 NULL을 리턴하고 errno에 에러코드가 들어간다.
       정상적으로 리턴된 DIR* 포인터를 가지고 readdir()과 closedir()을 사용 */
    dp = opendir(path); 
    if(dp == NULL)
        return -errno;
    /* opendir()로 얻어진 디렉토리 포인터를 통해 디렉토리 정보를 하나씩 차례대로 읽음
       디렉토리 스트림 끝에 도달(디렉토리 내 파일 정보를 모두 읽음)하거나 에러가 발생하면
       NULL을 리턴한다.
       
       readdir()이 정상적이면 struct dirent 구조체의 포인터를 리턴하기 때문에 사용전
       DIR* 포인터가 정상이지 확인하고 연속적으로 readdir()을 호출할 시에는 리턴 포인터를
       반드시 확인해야 한다.

       readdir()이 NULL을 리턴했는데 디렉토리 끝인지 에러인지 구분하고 싶다면 
       호출 전 errno를 0으로 지우고 readdir()을 호출한다. 디렉토리 끝으로 NULL을 리턴한 경우 
       errno값이 0으로 유지되므로 에러를 구분할 수 있다. 
    */
    while((de = readdir(dp)) != NULL){
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        /* 
          Why shift 12 bit for d_type?
          https://stackoverflow.com/questions/8420234/why-shift-12-bit-for-d-type-in-fusexmp
        */
        st.st_mode = de->d_type << 12; 
        /* filler함수: typedef int(*fuse_fill_dir_t) (void *buf, const char *name, 
                                    const struct stat *stbuf, off_t off, enum fuse_fill_dir_flags flags)
         * function to add an entry in a readdir() operation.
         * @param buf the buffer passed to the readdir() operation
         * @param name the file name of the directory entry
         * @param stat file attributes, can be NULL
         * @param off offset of the next entry or zero
         * @param flags fill flags
         * return 1 if buffer is full, zero otherwise
         * 
         * 
         * 디렉토리로부터 읽어들인 디렉토리 내에 존재하는 파일 이름을 ``name에, 그 파일의 metadata를 ``stat에 채워
         * filler함수를 호출함으로써 읽어 들인 내용을 반환할 수 있다.
         *
         * The *off* parameter - 1) 0으로 설정: 한 번의 readdir함수 호출로 전체 directory를 읽어 들임
                               - 2) 디렉토리에서 현재 읽어 들인 엔트리의 offset을 설정: 디렉토리 entry를 읽어 들일
                                    때마다 readdir 함수가 호출되도록 함.
         */
        if(filler(buf, de->d_name, &st, 0, 0)) //return 값이 1이면 버퍼가 꽉참
            break;
    }
    /* int closedir(DIR *dirstream)
       디렉토리 스트림을 닫음, 시스템 자원을 사용하므로 DIR*포인터를 통해서 닫아야 함.
       정상이면 0, 실패하면 -1을 리턴함.
     */
    closedir(dp); 
    return 0;
}

/* 함수 원형: int (*mknod)(const char *, mode_t, dev_t) */
/* 
    create a file node
    This is called for creation of all non-directory, non-symlink nodes.
    If the filesystem defines a create() method, then for regular files that will be called instead.

    시스템콜 mknod: 특수 및 일반 파일을 생성한다.
    path를 이름으로 가지는 파일 시스템 노드 -file, 장치 파일 혹은 named::pipe- 를 생성한다.
    mode는 생성되는 파일의 권한을 결정한다. 만약 S_IFCHR이나 S_IFBLK를 생성하고자 한다면
    dev를 명시해주어야 한다.
    dev는 새로 생성될 파일이 생성될 장치의 major num과 minor num을 가진다. 
*/
static int myfs_mknod(const char *path, mode_t mode, dev_t rdev)
{ 
    int res;
    res = mknod_wrapper(AT_FDCWD, path, NULL, mode, rdev); //my_passthrouhg_helpers.h
    if(res == -1)
        return -errno;
    return 0;
}

/* 함수 원형: int (*mkdir)(const char *, mode_t) */
/*
    create a directory.
       생성하려는 디렉토리의 부모 디렉토리의 inode를 읽어 block주소 위치를 알아내어 
       해당 block에 새로운 directory 관련 directory entry를 생성한다.
    Note that the mode argument my not have the type specification bits set, i.e. S_ISDIR(mode) can be false.
    To obtain the correct directory type bits use mode|S_IFDIR

    시스템콜 mkdir: path이름을 가지는 디렉토리를 만들려고 시도한다.
    mode는 사용할 수 있는 권한에 대한 허가권을 지정하며, 일반적으로 umask에 의해 수정된다.
*/
static int myfs_mkdir(const char *path, mode_t mode)
{
    int res;

    res = mkdir(path, mode);
    if(res == -1)
        return -errno;
    return 0;
}

/* 함수 원형: int (*unlink)(const char *) */
/*
    remove a file.

    시스템콜 unlink: path에서 지정한 파일을 삭제한다.
    hard link의 이름을 삭제하고 hard link가 참조하는 count를 1 감소시킨다. 
    hard link의 참조 count가 0이 되면 실제 파일의 내용이 저장되어 있는 disk space를 free하여
    OS가 다른 파일을 위해서 사용할 수 있도록 한다. 따라서 hard link를 생성하지 않은 파일은
    바로 disk space를 해제하여 사실상 파일을 삭제한다.
*/
static int myfs_unlink(const char *path)
{
    int res;
    res = unlink(path);
    // 리턴값 0: 정상적으로 파일 또는 link가 삭제됨
    // 리턴값 -1: 오류가 발생, 상세 내용은 errno에 저장됨.
    if(res == -1)
        return -errno;

    return 0;
}

/* 함수 원형: int (*unlink)(const char *) */
/*
    remove a directory.
*/
static int myfs_rmdir(const char *path)
{
    int res;
    res = rmdir(path);

    if(res == -1)
        return -errno;
    return 0;

}

/* 함수 원형: int (*symlink)(const char *, const char*) */
/*
    Create a symbolic link
    시스템콜 symlink: oldpath 파일에 대한 심볼릭 링크 newpath를 만든다.
    만약 심볼릭 링크 newpath가 이미 존재한다면 덮어쓰지 않는다.
*/
static int myfs_symlink(const char *from, const char *to)
{
    int res;
    res = symlink(from, to);
    if(res == -1)
        return -errno;
    return 0;
}


/*** 함수 원형: int (*rename)(const char *, const char*, unsigned int flags) ***/
/*
    Rename a file
    flags may be RENAME_EXCHANGE or RENAME_NOREPLACE.
    If RENAME_EXCHANGE is specified, the filesystem must atomically exchange the two files
    If RENAME_NOREPLACE is specified, the filesystem must not overwrite new name if it exists and
    return an error instead.

    시스템콜 rename: 파일의 이름을 바꾸거나 필요할 경우 파일을 이동시킨다. 하드링크 파일은
    영향을 받지 않는다.
*/
static int myfs_rename(const char *from, const char* to, unsigned int flags)
{
    int res;

    if(flags)
        return -EINVAL; //EINVAL: 
    res = rename(from, to);
    if(res == -1)
        return -errno;
    return 0;
}

/* 함수 원형: int (*link)(const char *, const char*) */
/*
    create a hard link to a file

    시스템콜 link: oldpath로 존재하는 파일에 대해 새로운 연결-하드링크 을 만든다
    만약 newpath가 존재하고 있다면 덮어쓰지 않는다. 2개의 파일은 같은 아이노드로 연결되어 있다.
    서로 다른 장치(파일시스템) 사이를 연결하려면 symlink를 사용해야 한다.
*/
static int myfs_link(const char *from, const char *to)
{
    int res;
    res = link(from, to);
    if(res == -1)
        return -errno;
    return 0;
}


/* 함수 원형: int (*chmod)(const char *, mode_t, struct fuse_file_info *fi) */
/*
    Change the permission bits of a file
    fi will always be NULL if the file is not currently open, but may also be NULL if the file is open.

    시스템콜 chmod: path로 주어진 파일의 모드를 mode모드로 변경한다.
*/
static int myfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    /* fuse_file_info: Information about an open file.
       File handles are created by the open, opendir, and create methods and closed
       by the release and releasedir methods. Multiple file handles may be concurrently open
       for the same file. Generally, a client will create one file handle per file descriptor,
       though in some cases multiple file descriptors can share a single file handle.
    */
    (void)fi;
    int res;

    res = chmod(path, mode);
    if(res == -1)
        return -errno;
    return 0;
}

/* 함수 원형: int (*chown)(const char *, uid_t, gid_t, struct fuse_file_info *fi) */
/*
    Change the owner and group of a file.
    fi will always be NULL if the file is not currently open, 
    but may also be NULL if the file is open.
    Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is expected 
    to reset the setuid and setgid bits.

    시스템콜 chown: path에 지정되어 있는 파일 혹은 fd가 가리키는 파일의 소유(및 그룹)
    권한을 변경하기 위해 사용된다. chown을 이용해서 파일의 권한을 변경하기 위해서는 
    해당 파일에 대한 권한을 가지고 있어야 한다. 슈퍼유저는 임의로 권한의 변경이 가능하다.

    시스템콜 lchown: 심볼릭 링크에 대한 소유 권한을 변경하기 위한 목적으로 사용되었다. 
    그러나 최근의 커널들은 심볼릭 링크에 대한 권한 변경을 허용하지 않는다. 
    lchown 역시 내부적으로 chown과 동일한 시스템 콜을 사용한다.
*/
static int myfs_chown(const char *path, uid_t uid, gid_t gid, 
            struct fuse_file_info *fi)
{
    (void)fi;
    int res;
    res = lchown(path, uid, gid);

    if(res == -1)
        return -errno;
    return 0;
}

/* 함수 원형: int (*truncate)(const char *, off_t, struct fuse_file_info *fi) */
/*
    Change the size of a file.
    fi will always be NULL if the file is not currently open, but may also be NULL if the file is open.
    Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is expected to reset the setuid and setgid bits.
*/
static int myfs_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
    int res;
    /*
        truncate/ftruncate는 path로 지정된 파일이나 fd로 참조되는 파일을 
        size 바이트 크기가 되도록 자른다.
    */
    if(fi !=NULL) //열려있다면
        res = ftruncate(fi->fh, size); //fuse_file_info {... fh ...}-> fh=file handle id.
    else
        res = truncate(path, size);
    if(res == -1)
        return -errno;
    return 0;
}

#ifdef HAVE_UTIMENSAT
/* 함수 원형: int(*utimens) (const char *, const struct timespec tv[2], struct fuse_file_info *fi) */
/*
    Change the access and modification times of a file with nanosecond resolution.
    파일의 최종 접근과 수정 타임스탬프를 설정하는데 확장된 기능을 제공한다.
        - utimes()가 제공하는 microsecond의 정확성보다 개선된 것으로 타임스탬프를 설정할 수 있다.
        - 타임스탬프를 개별적(한 번에 하나씩)으로 설정할 수 있다.
        - 타임스탬프 중 하나에 개별적으로 현재 시간을 설정할 수 있다.
    This supersedes the old utime() interface. New applications should use this.
    fi will always be NULL if the file is not currently open, but may also be NULL
    if the file is open.
    @parma timespec ts[2]-> 마지막 접근 시간, 마지막 변경 시간
    `` struct timespec {
                    time_t tv_sec;      // 초('time_t'는 정수형)
                    long tv_nsec;       // 나노초 
       }
*/
static int myfs_utimens(const char *path, const struct timespec ts[2], struct fuse_file_info *fi)
{
    (void) fi;
    int res;

    /* Don't use utime/utimes since they follow symlinks*/
    /*
        #define _XOPEN_SOURCE 700
        #include <sys/stat.h>
        int utimesat(int dirfd, const char *pathname, const struct timespec times[2], int flags);

        @param dirfd: pathname 인자가 utimes()를 위한 값으로 해석되는 경우 AT_FDCWD를 명시,
                      디렉토리를 명시하는 fd를 명시
        @param struct timespec times[2] 
         1) NULL로 정의: 2가지 파일 타임스탬프는 현재 시간으로 설정
         2) NULL이 아닌 경우: 새로운 최종 접근 타임스탬프는 times[0]에 명시되고, 새로운 
            최종 수정 타임스탬프는 times[1]에 명시된다. 
            `` struct timespec {
                    time_t tv_sec;      // 초('time_t'는 정수형)
                    long tv_nsec;       // 나노초 
               }

               타임스탬프 중의 하나를
                --1) 현재 시간으로 설정: 해당하는 tv_nsec에 UTIME_NOW 명시 
                --2) 변경 없이 남겨두기: 해당하는 tv_nsec에 UTIME_OMIT 명시
                    이 두가지 모두 tv_sec 값은 무시
        @param flags 0이나 AT_SYMLINK_NOFOLLOW가 될 수 있음. 후자는 심볼릭 링크인 경우 pathname은
                     역참조 될 수 없다는 뜻(심볼릭 링크 자체의 타임스탬프는 변경돼야만 함)
            
    */
    res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
    if(res == -1)
        return -errno;
    return 0;
}
#endif

/* 함수 원형: int (*create) (const char *, mode_t, struct fuse_file_info *) */
/*
    Create and open a file.
    If the file does not exist, first create it with the specified mode, and then open it.
    If this method is not implemented or under Linux Kernel versions earlier than 2.6.15,
    the mknod() and open() methods will be called instead.
*/
static int myfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int res;
    /* int open(const char *pathname, int flags, ...// mode_t mode); */
    res = open(path, fi->flags, mode); 
    /* struct fuse_file_info *fi
       Information about an open file. File Handles are created by the open, opendir, 
       and create methods and closed by the release and releasedir methods.

       fi->flags : Open flags. Available in open() and release()
    */
    if(res == -1)
        return -errno;
    fi->fh = res; //성공하면 fd 리턴
    /*
        struct fuse_file_info *fi-> fh: File Handle id. May be filled in by filesystem in 
                                        create, open, and opendir().
    */
    return 0;
}

/* 함수 원형: int (*open) (const char *, struct fuse_file_info *) */
/*
    Open a file
    Open flags are available in fi->flags.
        - Creation (O_CREAT, O_EXCL, O_NOCITTY) flags will be filtered out / handled by the kernel.
        - Access modes (O_RDONLY, O_WRONLY, O_RDWR, O_EXEC, O_SEARCH) should be used by the filesystem
          to check if the operation is permitted. If the -o default_permissions mount option is given,
          this check is already done by the kernel before calling open() and may be omitted by the fs.
        - When writeback caching is enabled, the kernel may send read requests even for files opened 
          with O_WRONLY. The filesystem should be prepared to handle this.
        - When writeback caching is disabled, the fs is expected to properly handle the O_APPEND flag and 
          ensure that each write is appending to the end of the file.
        - When writeback caching is enabled, the kernel will handle O_APPEND. However, unless all changes
          to the file come through the kernel this will not work reliably. The fs should thus either
          ignore the O_APPEND flag or return an error.
*/
static int myfs_open(const char *path, struct fuse_file_info *fi)
{
    int res;
    res = open(path, fi->flags);
    if(res == -1)
        return -errno;
    fi->fh = res;
    return 0;
}

/* 함수 원형: int (*read) (const char *, char *, size_t, off_t, struct fuse_file_info *) */
/*
    Read data from an open file.
    Read should return exactly the numaber of bytes requested except on EOF or error, otherwise
    the rest of the data will be substituted with zeroes. An exception to this is when the 
    'direct_io' mount option is specified, in which case the return value of the read syscall
    will reflect the return value of this operation.
*/
static int myfs_read(const char *path, char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi)
{
    int fd;
    int res;

    if(fi == NULL)
        fd = open(path, O_RDONLY);
    else
        fd = fi->fh;
    if(fd == -1)
        return -errno;
    /* 
    #include <unistd.h>
    ssize_t pread(int fd, void *buf, size_t count, off_t offset);
        읽은 바이트 수를 리턴한다. 파일 I/O가 현재 파일 오프셋이 아니라 
        offset으로 명시된 위치에서 수행된다는 점을 제외하고 read()와 
        똑같이 동작한다. 파일 오프셋은 이 시스템 호출에 의해 바뀌지 않는다.
    */
    res = pread(fd, buf, size, offset);
    if(res == -1)
        return -errno;
    if(fi == NULL)
        close(fd);
    return res;
}

/* 함수 원형: int (*write) (const char *, const char *, size_t, off_t, struct fuse_file_info *) */
/*
    Write data to an open file
    Write should return exactly the number of bytes requested except on errer. An exception to this is
    when the 'direct_io' mount option is specified.
    Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is expected to reset the setuid and setgid bits.
*/
static int myfs_write(const char *path, const char *buf, size_t size, 
            off_t offset, struct fuse_file_info *fi)
{
    int fd;
    int res;

    (void) fi;
    if(fi == NULL)
        fd = open(path, O_WRONLY);
    else
        fd = fi->fh;
    if(fd == -1)
        return -errno;
    /* 
    #include <unistd.h>
    ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
        쓴 바이트 수를 리턴한다. 파일 I/O가 현재 파일 오프셋이 아니라 
        offset으로 명시된 위치에서 수행된다는 점을 제외하고 write()와 
        똑같이 동작한다. 파일 오프셋은 이 시스템 호출에 의해 바뀌지 않는다.
    */
    res = pwrite(fd, buf, size, offset);
    if(res == -1)
        res = -errno;
    if(fi == NULL)
        close(fd);
    return res;
}

/* 함수 원형: int (*statfs) (const char *, struct statvfs *) */
/*
    Get file system statistics. The 'f_favail', 'f_fsid' and 'f_flag' fields are ignored.
*/
static int myfs_statfs(const char *path, struct statvfs *stbuf)
{
    int res;
    /*
        #include <sys/statvfs.h>
        int statvfs(const char *pathname, struct statvfs *statvfsbuf);
            마운트된 파일 시스템에 관한 정보를 획득한다. 
            ``struct statvfs {
                unsigned long f_bsize;  // 파일 시스템 블록 크기(바이트)
                unsigned long f_frsize; // 기본 파일 시스템 블록 크기(바이트)
                fsblkcnt_t f_blocks;    // 파일시스템 전체 블록 수('f_frsize' 단위)
                fsblkcnt_t f_bfree;     // 사용 중이지 않은 블록의 전체 수
                fsblkcnt_t f_bavail;    // 비특권 프로세스에 가용한 사용 중이지 않은 블록 수
                fsfillcnt_t f_files;    // 아이노드의 전체 수
                fsfilcnt_t f_ffree;     // 사용 중이지 않은 아이노드의 전체 수
                fsfilcnt_t f_favail;    // 비특권 프로세스에 가용한 사용 중이지 않은 아이노드 수
                unsigned long f_fsid;   // 파일시스템 id
                unsigned long f_flag;   // 마운트 플래그 
                unsigned long f_namemax;// 파일 시스템에서 파일 이름의 최대 길이
            }
    */
    res = statvfs(path, stbuf);
    if(res == -1)
        return -errno;
    return 0;
}

/* 함수 원형: int (*release) (const char *, struct fuse_file_info *) */
/*
    Release an open file
    Release is called when there are no more references to an open file: all fds are closed
    and all memory mappings are unmapped.

    For every open() call there will be exactly one release() call with the same flags and file handle.
*/
static int myfs_release(const char *path, struct fuse_file_info *fi)
{
    (void) path;
    close(fi->fh);
    return 0;
}

/* 함수 원형: int (*fsync) (const char *, int, struct fuse_file_info) */
/*
    Synchronize file contents
    If the datasync parameter is non-zero, then only the user data should be flushed, not the meta data.
*/
static int myfs_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    /* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}

/* 함수 원형: int (*fallocate) (const char *, int, off_t, off_t, struct fuse_file_info) */
/*
    Allocates space for an open file
    This function ensures that required space is allocated for specified file. If this function
    returns success then any subsequent write request to specified range is guaranteed not to fail
    because of lack of space on the file system media.
*/
#ifdef HAVE_POSIX_FALLOCATE
static int myfs_fallocate(const char *path, int mode, off_t offset, off_t length,
            struct fuse_file_info *fi)
{
    int fd;
    int res;

    (void) fi;

    if(mode)
        return -EOPNOTSUPP; // Operation not supported on transport endpoint
    if(fi == NULL)
        fd = open(path, O_WRONLY);
    else
        fd = fi->fh;
    if(fd == -1)
        return -errno;
    /*
        posix_fallocate(fd, offset, len) : fd가 가리키는 디스크 파일의 offset부터 len만큼의 범위에
                해당하는 디스크 공간이 할당되도록 보장한다.이렇게 하면 응용 프로그램이 나중에 파일에
                write()를 해도 디스크 공간이 부족해서 실패하는 일(파일의 구멍을 채우는 경우, 파일의 
                내용을 모두 쓰기 전에 다른 응용프로그램이 디스크 공간을 써버리면 발생할 수 있다)은 없다.
    */
    res = -posix_fallocate(fd, offset, length);
    
    if(fi == NULL)
        close(fd);
    return res;
}
#endif

/* 함수 원형: int (*setxattr) (const char *, const char *, const char *, size_t, int) */
/*
    Set extended attributes.
*/
#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int myfs_setxattr(const char *path, const char *name, const char *value,
            size_t size, int flags)
{
    // pathname으로 파일을 식별하지만, 심볼릭 링크를 역참조하지는 않는다.
    int res = lsetxattr(path, name, value, size, flags);
    if(res == -1)
        return -errno;
    return 0;
}


/* 함수 원형: int (*getxattr) (const char *, const char *, char *, size_t) */
/*
    Get extended attributes.
*/
static int myfs_getxattr(const char *path, const char *name, char *value, size_t size)
{
    int res = lgetxattr(path, name, value, size);
    if(res == -1)
        return -errno;
    return 0;
}

/* 함수 원형: int (*listxattr) (const char *, char *, size_t) */
/*
    List extended attributes.
*/
static int myfs_listxattr(const char *path, char *list, size_t size)
{
    int res = listxattr(path, list, size);
    if(res == -1)
        return -errno;
    return 0;
}

/* 함수 원형: int (*removexattr) (const char *, const char *) */
static int myfs_removexattr(const char *path, const char *name)
{
    int res = lremovexattr(path, name);
    if(res == -1)
        return -errno;
    return 0;
}
#endif /* HAVE_SETXATTR */

/* 함수 원형: ssize_t (*copy_file_range) (const char *path_in, struct fuse_file_info *fi_in,
                off_t offset_in, const char *path_out, struct fuse_file_info *fi_out, 
                off_t offset_out, size_t size, int flags) */
/*
    Copy a range of data from one file to another
    Performs an optimized copy between two file descriptors without the additional cost of transferring data
    through the FUSE kernel module to user space (glibc) and then back into the FUSE filesystem again.
    In canse this method is not implemented, applications are expected to fall back to a regular file copy.
    (Some glibc versions did this emulation automatically, but the emulation has been removed from
    all glibc release branches.)
*/
#ifdef HAVE_COPY_FILE_RANGE
static ssize_t myfs_copy_file_range(const char *path_in, 
                    struct fuse_file_info *fi_in,
                    off_t offset_in, const char *path_out,
                    struct fuse_file_info *fi_out,
                    off_t offset_out, size_t size, int flags)
{
    int fd_in, fd_out;
    ssize_t res;

    if(fi_in == NULL)
        fd_in = open(path_in, O_RDONLY);
    else
        fd_in = fi_in->fh;
    if(fi_in == -1)
        return -errno;
    if(fi_out == NULL)
        fd_out = open(path_out, O_WRONLY);
    else
        fd_out = fi_out->fh;
    if(fd_out == -1){
        close(fd_in);
        return -errno;
    }
    /*
        #define _GNU_SOURCE
        #include <unistd.h>

        ssize_t copy_file_range(int fd_in, loff_t *off_in,
                                int fd_out, loff_t *off_out,
                                size_t len, unsigned int flags);

            This function performs an in-kernel copy between two file descriptors without 
            the additional cost of transferring data from the kernel to user space and then back 
            into the kernel. It copies up to len bytes of data from the source file descriptor fd_in
            to the target file descriptor fd_out, overwriting any data that exists within 
            the requested range of the target file.

            @param off_in: if NULL, then bytes are read from fd_in starting from the file offset,
                           and the file offset is adjusted by the number of bytes copied.
                           if not NULL, then off_in must point to a buffer that specifies the starting 
                           offset of fd_in is not changed, but off_in is adjusted appropriately.
                    
    */
    res = copy_file_range(fd_in, &offset_in, fd_out, &offset_out, len, flags);
    if(res == -1)
        res = -errno;
    close(fd_in);
    close(fd_out);

    return res;
}
#endif

/* 함수 원형: off_t (*lseek) (const char *, off_t off, int whence, struct fuse_file_info *) */
/*
    Find next data or hole after the specified offset
*/
static off_t myfs_lseek(const char *path, off_t off, int whence, struct fuse_file_info *fi)
{
    int fd;
    off_t res;

    if(fi == NULL)
        fd = open(path, O_RDONLY);
    else
        fd = fi->fh;
    if(fd == -1)
        return -errno;
    res = lseek(fd, off, whence);
    if(res == -1)
        res = -errno;
    if(fi == NULL)
        close(fd);
    return res;
}

static const struct fuse_operations myfs_oper = {
    .init       = myfs_init,
    .getattr    = myfs_getattr,
    .access     = myfs_access,
    .readlink   = myfs_readlink,
    .readdir    = myfs_readdir,
    .mknod      = myfs_mknod,
    .mkdir      = myfs_mkdir,
    .symlink    = myfs_symlink,
    .unlink     = myfs_unlink,
    .rmdir      = myfs_rmdir,
    .rename     = myfs_rename,
    .link       = myfs_link,
    .chmod      = myfs_chmod,
    .chown      = myfs_chown,
    .truncate   = myfs_truncate,
#ifdef HAVE_UTIMENSAT
    .utimens    = myfs_utimens,
#endif
    .open       = myfs_open,
    .create     = myfs_create,
    .read       = myfs_read,
    .write      = myfs_write,
    .statfs     = myfs_statfs,
    .release    = myfs_release,
    .fsync      = myfs_fsync,
#ifdef HAVE_POSIX_FALLOCATE
    .fallocate  = myfs_fallocate,
#endif
#ifdef HAVE_SETXATTR
    .setxattr   = myfs_setxattr,
    .getxattr   = myfs_getxattr,
    .listxattr  =myfs_listxattr,
    .removexattr=myfs_removexattr,
#endif
#ifdef HAVE_COPY_FILE_RANGE
    .copy_file_range =  myfs_copy_file_range,
#endif
    .lseek      = myfs_lseek,
};

int main(int argc, char *argv[]){
    umask(0);
    return fuse_main(argc, argv, &myfs_oper, NULL);
}
