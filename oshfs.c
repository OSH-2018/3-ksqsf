//
// Created by ksqsf on 18-5-3.
//

#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <memory.h>
#include <stdlib.h>
#include "oshfs.h"

#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)>(b)?(b):(a))

void *blocks[OSHFS_NBLKS];
struct file_entry *root;

// TODO: Optimize.
static size_t find_free_block()
{
    for (size_t i = 0; i < OSHFS_NBLKS; ++i)
        if (!blocks[i])
            return i;
    return 0;
}

/// Allocate a new block in the memory.
/// \return address of the new block
static void *blkalloc()
{
    printf("    %s\n", __FUNCTION__);

    return mmap(NULL, OSHFS_BLKSIZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

/// Drop a block and free the memory.
/// \param n position
static void blkdrop(size_t n)
{
    printf("    %s %lu\n", __FUNCTION__, n);

    // Reclaim resources.
    munmap(blocks[n], OSHFS_BLKSIZ);
    blocks[n] = NULL;
}

// s[find_next(s, c)] == c, or s[find_next(s,c)] == 0
static size_t find_next(const char *s, char c)
{
    size_t i = 0;
    while (s[i] != c && s[i] != 0)
        i++;
    return i;
}

static struct file_entry *do_find_file_by_path(const char *pathname, struct file_entry *dir)
{
    printf("  %s: %s\n", __FUNCTION__, pathname);

    if (pathname[0] == 0)
        return dir;

    size_t current = dir->child;
    char fn[256];

    while (current != 0) {
        struct file_entry *fe = (struct file_entry *) blocks[current];

        // Copy file name.
        size_t l = find_next(pathname, '/');
        strncpy(fn, pathname, l);
        fn[l] = 0;

        // Compare and check.
        if (!strcmp(fn, fe->filename))
            if (pathname[l] == 0)
                return fe;
            else if (fe->mode & S_IFDIR) // Go to next level; current fe must be a directory.
                return do_find_file_by_path(pathname + l + 1, fe);
            else
                return NOTDIR;

        current = fe->next;
    }
    return NULL;
}

/// Find file entry by path.
/// \param pathname path name.
/// \return The file entry.
static struct file_entry *find_file_by_path(const char *pathname)
{
    return do_find_file_by_path(pathname, root);
}

/// Fill stbuf.
static void fill_stat(const struct file_entry *fe, struct stat *stbuf)
{
    stbuf->st_mode = fe->mode;
    stbuf->st_atim = fe->atime;
    stbuf->st_ctim = fe->ctime;
    stbuf->st_mtim = fe->mtime;
    stbuf->st_uid = fe->uid;
    stbuf->st_gid = fe->gid;
    stbuf->st_size = fe->size;
    stbuf->st_nlink = fe->nlink;
    stbuf->st_blocks = fe->blocks;
}

void *osh_init(struct fuse_conn_info *conn)
{
    (void) conn;
    printf("%s\n", __FUNCTION__);
    struct timespec now;

    // Prepare rootfs attributes.
    root = blocks[0] = blkalloc();
    clock_gettime(CLOCK_REALTIME, &now);
    root->mode = S_IFDIR | 0755;
    root->atime = now;
    root->ctime = now;
    root->mtime = now;
    root->uid = 0;
    root->gid = 0;
    root->size = OSHFS_BLKSIZ;
    root->nlink = 1;
    root->child = 0;
}

int osh_getattr(const char *path, struct stat *stbuf)
{
    printf("%s: %s\n", __FUNCTION__, path);

    struct file_entry *fe = find_file_by_path(path + 1);
    if (!fe)
        return -ENOENT;
    else if (fe == NOTDIR)
        return -ENOTDIR;

    fill_stat(fe, stbuf);
    return 0;
}

int osh_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;

    struct file_entry *dir = find_file_by_path(path + 1);
    if (!dir)
        return -ENOENT;
    else if (dir == NOTDIR)
        return -ENOTDIR;

    size_t current = dir->child;
    while (current != 0) {
        struct file_entry *fe = (struct file_entry *) blocks[current];
        struct stat stbuf;
        fill_stat(fe, &stbuf);
        if (filler(buf, fe->filename, &stbuf, 0))
            break;
        current = fe->next;
    }
    return 0;
}

static size_t parent_dir(const char *path, struct file_entry **dir) {
    char dirpath[4096];
    size_t j = strlen(path) - 1;
    while (path[j] != '/' && j >= 0)
        j--;
    strncpy(dirpath, path, j);
    dirpath[j] = 0;
    printf("%s: %s -> %s\n", __FUNCTION__, path, dirpath);
    *dir = find_file_by_path(dirpath + 1);
    if (*dir)
        printf("  Found parent dir: %s\n", (*dir)->filename);
    return j+1;
}

int osh_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    (void) fi;

    printf("%s: %s\n", __FUNCTION__, path);

    size_t mdblk, j;
    struct file_entry *fe;
    struct file_entry *dir;
    struct timespec now;

    // Find the containing directory
    j = parent_dir(path, &dir);
    if (dir == 0)
        return -ENOENT;
    else if (dir == NOTDIR)
        return -ENOTDIR;

    // Find a free block for metadata.
    mdblk = find_free_block();
    if (!mdblk)
        return -ENOSPC;
    fe = blocks[mdblk] = blkalloc();

    // Metadata.
    strncpy(fe->filename, path+j, sizeof(fe->filename));
    fe->head = 0;
    fe->next = dir->child;
    fe->blocks = 0;
    fe->size = 0;
    fe->mode = mode;
    fe->uid = getuid();
    fe->gid = getgid();
    fe->nlink = 1;
    clock_gettime(CLOCK_REALTIME, &now);
    fe->mtime = now;
    fe->atime = now;
    fe->ctime = now;

    dir->child = mdblk;

    return 0;
}

int osh_access(const char *path, int mask)
{
    (void) mask;

    printf("%s: %s\n", __FUNCTION__, path);

    struct file_entry *fe = find_file_by_path(path + 1);
    if (!fe)
        return -ENOENT;
    else if (fe == NOTDIR)
        return -ENOTDIR;

    clock_gettime(CLOCK_REALTIME, &fe->atime);

    return 0;
}

int osh_utimens(const char *path, const struct timespec ts[2])
{
    printf("%s: %s\n", __FUNCTION__, path);
    struct file_entry *fe = find_file_by_path(path + 1);
    if (!fe)
        return -ENOENT;
    else if (fe == NOTDIR)
        return -ENOTDIR;

    fe->atime = ts[0];
    fe->mtime = ts[1];
    clock_gettime(CLOCK_REALTIME, &fe->ctime);
    return 0;
}

int osh_open(const char *path, struct fuse_file_info *fi)
{
    printf("%s: %s\n", __FUNCTION__, path);
    struct file_entry *fe = find_file_by_path(path + 1);
    if (!fe)
        return -ENOENT;
    fi->fh = (uint64_t) fe;
    clock_gettime(CLOCK_REALTIME, &fe->atime);
    return 0;
}

int osh_read(const char *path, char *buf, size_t size, off_t offset,
             struct fuse_file_info *fi)
{
    printf("%s: %s (size %lu) (offset %ld)\n", __FUNCTION__, path, size, offset);

    struct file_entry *fe = (struct file_entry *) fi->fh;
    if (!fe)
        fe = find_file_by_path(path + 1);

    if (fe->head == 0)
        return 0;

    memset(buf, 0, size);

    size_t curblk = fe->head;
    size_t X = (size_t) offset, Y = offset+size;
    while (curblk) {
        struct data_node *node = (struct data_node *) blocks[curblk];
        size_t A = node->beg, B = node->beg + node->len;

        // No data could be read.
        if (X >= B)
            goto next_blk;

        // No more data to read.
        if (Y <= A)
            break;

        // Copy bytes.
        size_t tx = MAX(A, X), ty = MIN(B, Y);
        memcpy(buf + tx - offset, node->body + tx - A, ty-tx);

next_blk:
        curblk = node->next;
    }

    clock_gettime(CLOCK_REALTIME, &fe->atime);

    return (int) (MIN(offset+size, fe->size) - offset);
}

/// Recursively write into a file.
/// \param buf Buffer to be written.
/// \param size Size of buf.
/// \param offset Offset in the file.
/// \param fe File entry.
/// \param prevblk Previous block ID.
/// \param curblk Current block ID. prevblk == 0 iff curblk == fe->head.
static int do_write(const char *buf, size_t size, off_t offset, struct file_entry *fe, size_t prevblk, size_t curblk)
{
    if (size == 0)
        return 0;

    printf("  %s: size=%lu offset=%ld data=%s\n", __FUNCTION__, size, offset, buf);

    size_t X = (size_t) offset, Y = offset + size;
    struct data_node *prev = prevblk ? (struct data_node *) blocks[prevblk] : NULL;
    struct data_node *cur = curblk ? (struct data_node *) blocks[curblk] : NULL;

    if (cur) {
        size_t A = cur->beg, B = cur->beg + cur->len;

        if (X < A) {
            // Append a block before the current one.
            size_t blk = find_free_block();
            if (blk == 0)
                return -ENOSPC;

            blocks[blk] = blkalloc();
            struct data_node *new = (struct data_node *) blocks[blk];
            new->beg = X;
            new->len = MIN(size, MIN(sizeof(new->body), cur->beg - X));
            fe->blocks++;
            if (prev) {
                new->next = prev->next;
                prev->next = blk;
            } else {
                new->next = fe->head;
                fe->head = blk;
            }
            printf("New block beg=%lu len=%lu\n", new->beg, new->len);

            memcpy(new->body, buf, new->len);
            return do_write(buf + new->len, size - new->len, offset + new->len, fe, curblk, blk);
        } else if (X < B) {
            size_t len = MIN(Y - X, cur->len);
            memcpy(cur->body + X - A, buf, len);
            return do_write(buf + len, size - len, offset + len, fe, curblk, cur->next);
        } else {
            return do_write(buf, size, offset, fe, curblk, cur->next);
        }
    }
    else {
        // We've exceeded the file boundary... Allocate new blocks.
        // N.B. This only happens at the end.

        // If there's a block before, try to merge into it.
        if (prev && X < prev->beg + sizeof(prev->body)) {
            size_t A = prev->beg, B = prev->beg + prev->len;
            size_t M = sizeof(prev->body);
            size_t len = MIN(A + M - X, Y-X);

            printf("Address space: %lu ~ %lu\n", X, Y);
            printf("Merging to %lu, len=%lu\n", X-A, len);

            memset(prev->body + prev->len, 0, X - B);
            memcpy(prev->body + X - A, buf, len);

            buf += len;
            size -= len;
            offset += len;
            prev->len += X - B + len;
            X = (size_t) offset;
        }

        // Done. Return early.
        if (size == 0)
            return 0;

        // In case there's something left...
        size_t blk = find_free_block();
        if (blk == 0)
            return -ENOSPC;

        blocks[blk] = blkalloc();
        struct data_node *new = (struct data_node *) blocks[blk];
        new->beg = X;
        new->len = MIN(sizeof(new->body), size);
        fe->blocks++;
        if (prev) {
            new->next = prev->next;
            prev->next = blk;
        } else {
            new->next = fe->head;
            fe->head = blk;
        }

        memcpy(new->body, buf, new->len);
        return do_write(buf+new->len, size-new->len, offset+new->len, fe, blk, new->next);
    }
}

int osh_write(const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi)
{
    printf("%s: %s (size %lu) (off %ld) %s\n", __FUNCTION__, path, size, offset, buf);

    struct file_entry *fe = (struct file_entry *) fi->fh;
    if (!fe)
        fe = find_file_by_path(path + 1);
    size_t curblk = fe->head;

    // Nothing is changed.
    if (curblk == 0 && size == 0)
        return 0;

    // Do write. Expand the file on demand.
    if (do_write(buf, size, offset, fe, 0, fe->head) < 0)
        return -ENOSPC;

    fe->size = MAX(fe->size, size+offset);

    size_t cur = fe->head;
    int i = 0;
    while (cur) {
        struct data_node *node = (struct data_node *) blocks[cur];
        printf("Block %d: [%lu,%lu)\n", ++i, node->beg, node->beg + node->len);
        cur = node->next;
    }

    clock_gettime(CLOCK_REALTIME, &fe->mtime);

    return (int) size;
}

/// Drop data blocks starting from node (inclusive).
/// \param node starting point
/// \param fe file entry
static void do_drop_data_blocks(size_t node, struct file_entry *fe)
{
    while (node) {
        size_t t = node;
        node = ((struct data_node *) blocks[node])->next;
        blkdrop(t);
        fe->blocks--;
    }
}

/// Drop all data blocks of a file.
/// \param blk file entry block
static void do_unlink(size_t blk)
{
    struct file_entry *fe = (struct file_entry *) blocks[blk];
    size_t node = fe->head;
    do_drop_data_blocks(node, fe);
    blkdrop(blk);
}

static int do_remove(const char *path, int rmdir)
{
    printf("%s: %s\n", __FUNCTION__, path);

    size_t current;
    struct file_entry *dir;
    size_t j = parent_dir(path, &dir);

    // Locate the directory.
    if (!dir)
        return -ENOENT;
    else if (dir == NOTDIR)
        return -ENOTDIR;

    // Locate the file.
    struct file_entry *fe = blocks[dir->child];

    if (!strcmp(fe->filename, path + j)) {
        if (!rmdir && S_ISDIR(fe->mode))
            return -EISDIR;

        if (rmdir)
            if (!S_ISDIR(fe->mode))
                return -ENOTDIR;
            else if (fe->child != 0)
                return -ENOTEMPTY;

        size_t t = fe->next;
        do_unlink(dir->child);
        dir->child = t;
        return 0;
    }

    for (current = dir->child; current; current = fe->next) {
        fe = (struct file_entry *) blocks[current];
        size_t nblk = fe->next;
        struct file_entry *next = blocks[nblk];

        if (!next)
            break;

        if (!strcmp(next->filename, path + j)) {
            if (!rmdir && fe->mode & S_IFDIR)
                return -EISDIR;

            if (rmdir)
                if (!S_ISDIR(fe->mode))
                    return -ENOTDIR;
                else if (fe->child != 0)
                    return -ENOTEMPTY;

            fe->next = next->next;
            do_unlink(nblk);
            blkdrop(nblk);
            return 0;
        }
    }

    return -ENOENT;
}

int osh_unlink(const char *path)
{
    return do_remove(path, 0);
}

int osh_rmdir(const char *path)
{
    return do_remove(path, 1);
}

int osh_chmod(const char *path, mode_t mode) {
    printf("%s: %s %o\n", __FUNCTION__, path, mode);

    struct file_entry* fe = find_file_by_path(path + 1);
    fe->mode = mode;
    clock_gettime(CLOCK_REALTIME, &fe->ctime);
    return 0;
}

int osh_chown(const char *path, uid_t owner, gid_t group) {
    printf("%s: %s\n", __FUNCTION__, path);

    struct file_entry *fe = find_file_by_path(path + 1);
    clock_gettime(CLOCK_REALTIME, &fe->ctime);
    fe->uid = owner;
    fe->gid = group;
    return 0;
}

int osh_truncate(const char *path, off_t len)
{
    struct file_entry *fe = find_file_by_path(path + 1);

    printf("%s: %s %ld\n", __FUNCTION__, path, len);

    size_t cur = fe->head;
    size_t pblk = 0;
    struct data_node *node = (struct data_node *) blocks[cur];
    while (cur) {
        if (len <= node->beg) {
            do_drop_data_blocks(cur, fe);
            if (pblk) {
                struct data_node *prev = (struct data_node *) blocks[pblk];
                prev->next = 0;
            } else {
                fe->head = 0;
            }
            break;
        }
        else if (len <= node->beg + node->len) {
            node->len = len - node->beg;
            if (node->next)
                do_drop_data_blocks(node->next, fe);
            break;
        }

        cur = node->next;
        pblk = cur;
    }
    fe->size = (size_t) len;
    clock_gettime(CLOCK_REALTIME, &fe->ctime);
    clock_gettime(CLOCK_REALTIME, &fe->atime);

    return 0;
}

int osh_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
    (void) isdatasync;
    (void) fi;

    printf("%s: %s\n", __FUNCTION__, path);

    if (find_file_by_path(path + 1))
        return 0;
    else
        return -ENOENT;
}

int osh_mkdir(const char *path, mode_t mode)
{
    printf("%s: %s\n", __FUNCTION__, path);

    struct file_entry *dir;
    size_t j = parent_dir(path, &dir);
    if (!dir)
        return -ENOENT;
    else if (dir == NOTDIR)
        return -ENOTDIR;

    // Create metadata.
    size_t blk = find_free_block();
    if (!blk)
        return -ENOSPC;

    struct file_entry *fe = blocks[blk] = blkalloc();
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    strncpy(fe->filename, path+j, 256);
    fe->head = 0;
    fe->next = dir->child;
    fe->child = 0;
    fe->mode = mode & 0777 | S_IFDIR;
    fe->size = OSHFS_BLKSIZ;
    fe->blocks = 1;
    fe->uid = 0;
    fe->gid = 0;
    fe->nlink = 1;
    fe->atime = now;
    fe->mtime = now;
    fe->ctime = now;

    // Prepend to dir.
    dir->child = blk;

    return 0;
}