#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 26

#include <fuse.h>
#include "oshfs.h"

static const struct fuse_operations osh_oper = {
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
        .mkdir = osh_mkdir,
        .rmdir = osh_rmdir,
        .rename = osh_rename,
        .symlink = osh_symlink,
        .readlink = osh_readlink,
        .release = osh_release,
        .mknod = osh_mknod,
        .statfs = osh_statfs,

//        .link = xmp_link,
//        .fallocate = xmp_fallocate,
//        .setxattr = xmp_setxattr,
//        .getxattr = xmp_getxattr,
//        .listxattr = xmp_listxattr,
//        .removexattr = xmp_removexattr
};

int main(int argc, char *argv[])
{
    umask(0);
    return fuse_main(argc, argv, &osh_oper, NULL);
}
