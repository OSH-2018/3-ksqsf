#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include "oshfs.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/xattr.h>

static const struct fuse_operations xmp_oper = {
        .init = osh_init,
        .getattr = osh_getattr,
        .readdir = osh_readdir,
        .create = osh_create,
        .access = osh_access,
        .utimens = osh_utimens,
        .open = osh_open,
        .read = osh_read,
        .write = osh_write,
        .unlink = osh_unlink,
        .chmod = osh_chmod,
        .chown = osh_chown,
        .truncate = osh_truncate,
        .fsync = osh_fsync,
//        .readlink = xmp_readlink,
//        .mknod = xmp_mknod,
//        .mkdir = xmp_mkdir,
//        .symlink = xmp_symlink,
//        .rmdir = xmp_rmdir,
//        .rename = xmp_rename,
//        .link = xmp_link,
//        .read = xmp_read,
//        .write = xmp_write,
//        .statfs = xmp_statfs,
//        .release = xmp_release,

//        .fallocate = xmp_fallocate,
//        .setxattr = xmp_setxattr,
//        .getxattr = xmp_getxattr,
//        .listxattr = xmp_listxattr,
//        .removexattr = xmp_removexattr
};

int main(int argc, char *argv[])
{
    umask(0);
    return fuse_main(argc, argv, &xmp_oper, NULL);
}