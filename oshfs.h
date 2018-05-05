//
// Created by ksqsf on 18-5-3.
//

#ifndef INC_3_KSQSF_OSHFS_H
#define INC_3_KSQSF_OSHFS_H

#include "config.h"
#include <fuse.h>
#include <unistd.h>
#include <sys/stat.h>

struct file_entry {
    char  filename[256];    // File name
    size_t head;            // Points to the first data block
    size_t next;            // Next file entry
    size_t child;
    mode_t mode;            // Mode
    size_t size;            // File size
    blkcnt_t blocks;        // blocks
    uid_t uid;              // user ID of owner
    gid_t gid;              // group ID of owner
    nlink_t nlink;          // Number of hard links
    struct timespec atime;  // access time
    struct timespec mtime;  // modification time
    struct timespec ctime;  // change time
};

struct __attribute__((packed)) data_node {
    size_t next; // points to next data node
    size_t beg;
    size_t len;
    char body[OSHFS_BLKSIZ - sizeof(size_t)*3];
};

#define NOTDIR ((struct file_entry *) 1)

void *osh_init(struct fuse_conn_info *ci);
int osh_getattr(const char *path, struct stat *stbuf);
int osh_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int osh_readdir(const char *pathname, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int osh_access(const char *path, int mask);
int osh_utimens(const char *path, const struct timespec ts[2]);
int osh_open(const char *path, struct fuse_file_info *fi);
int osh_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int osh_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int osh_unlink(const char *path);
int osh_chmod(const char *path, mode_t mode);
int osh_chown(const char *path, uid_t user, gid_t group);
int osh_truncate(const char *path, off_t len);
int osh_fsync(const char *path, int isdatasync, struct fuse_file_info *fi);
int osh_mkdir(const char *path, mode_t mode);
int osh_rmdir(const char *path);

#endif //INC_3_KSQSF_OSHFS_H
