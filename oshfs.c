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

#ifdef DEBUG
#define TRACE printf
#else
#define TRACE(...)
#endif

#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)>(b)?(b):(a))

void *blocks[OSHFS_NBLKS];
struct file_entry *root;
struct statvfs *statfs;

size_t first_free;
size_t next_free[OSHFS_NBLKS];

static size_t take_free_block()
{
    size_t ret = first_free;
    if (next_free[first_free] == 0)
        first_free = first_free + 1;
    else
        first_free = next_free[first_free];
    return ret;
}

static void *_blkalloc() {
    return mmap(NULL, OSHFS_BLKSIZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

/// Allocate a new block in the memory.
/// \return address of the new block
static void *blkalloc()
{
    statfs->f_bfree--;
    statfs->f_bavail--;
    return _blkalloc();
}

/// Drop a block and free the memory.
/// \param n position
static void blkdrop(size_t n)
{
    TRACE("    %s %lu\n", __FUNCTION__, n);

    // Be defensive about double free.
    if (!blocks[n]) {
        TRACE("** WARNING **: Double free on block %lu!\n", n);
        return;
    }

    // Reclaim resources.
    munmap(blocks[n], OSHFS_BLKSIZ);
    blocks[n] = NULL;

    // Add this node to free list
    next_free[n] = first_free;
    first_free = n;

    statfs->f_bfree++;
    statfs->f_bavail++;
}

// s[find_next(s, c)] == c, or s[find_next(s,c)] == 0
static size_t find_next(const char *s, char c)
{
    size_t i = 0;
    while (s[i] != c && s[i] != 0)
        i++;
    return i;
}

/// Find a path like 'a/b/c' in a directory.
/// \param pathname 'a/b/c'
/// \param dir directory
/// \param prev previous file entry to the found one; NULL for the first child in dir
/// \return the found file entry
static struct file_entry *do_find_file_by_path(const char *pathname, struct file_entry *dir,
                                               struct file_entry **prev, size_t *blk)
{
    TRACE("  %s: %s\n", __FUNCTION__, pathname);

    if (pathname[0] == 0)
        return dir;

    if (prev)
        *prev = NULL;
    size_t current = dir->child;
    char fn[MAX_FILENAME];

    while (current != 0) {
        struct file_entry *fe = (struct file_entry *) blocks[current];

        // Copy file name.
        size_t l = find_next(pathname, '/');
        strncpy(fn, pathname, l);
        fn[l] = 0;

        // Compare and check.
        if (!strcmp(fn, fe->filename)) {
            if (pathname[l] == 0) {
                if (blk)
                    *blk = current;
                return fe;
            }
            else if (fe->mode & S_IFDIR) // Go to next level; current fe must be a directory.
                return do_find_file_by_path(pathname + l + 1, fe, prev, blk);
            else
                return NOTDIR;
        }

        if (prev)
            *prev = fe;
        current = fe->next;
    }
    return NULL;
}

/// Find file entry by path.
/// \param pathname path name.
/// \return The file entry.
static struct file_entry *find_file_by_path(const char *pathname)
{
    return do_find_file_by_path(pathname, root, NULL, NULL);
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
    stbuf->st_dev = fe->dev;
}

void *osh_init(struct fuse_conn_info *conn)
{
    (void) conn;
    TRACE("%s\n", __FUNCTION__);
    struct timespec now;

    // Prepare rootfs attributes.
    root = blocks[0] = _blkalloc();
    clock_gettime(CLOCK_REALTIME, &now);
    root->mode = S_IFDIR | 0755;
    root->atime = now;
    root->ctime = now;
    root->mtime = now;
    root->uid = getuid();
    root->gid = getgid();
    root->size = OSHFS_BLKSIZ;
    root->nlink = 1;
    root->child = 0;

    // Prepare filesystem statistics.
    statfs = blocks[1] = _blkalloc();
    statfs->f_bsize = OSHFS_BLKSIZ;
    statfs->f_frsize = OSHFS_FRSIZ;
    statfs->f_blocks = OSHFS_NBLKS;
    statfs->f_bfree = OSHFS_NBLKS - 2;  // the first 2 blocks are preserved by the fs
    statfs->f_bavail = statfs->f_bfree;
    statfs->f_files = 0;
    statfs->f_flag = 0;
    statfs->f_namemax = MAX_FILENAME;

    // Set up free list
    first_free = 2;
    for (size_t i = 2; i < OSHFS_NBLKS-1; ++i)
        next_free[i] = i+1;

    return 0;
}

int osh_getattr(const char *path, struct stat *stbuf)
{
    TRACE("%s: %s\n", __FUNCTION__, path);

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

    struct stat stbuf;
    struct file_entry *dir = NULL;

    if (fi->fh)
        dir = (struct file_entry *) fi->fh;
    else
        dir = find_file_by_path(path + 1);

    if (!dir)
        return -ENOENT;
    else if (dir == NOTDIR)
        return -ENOTDIR;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    size_t current = dir->child;
    while (current != 0) {
        struct file_entry *fe = (struct file_entry *) blocks[current];
        fill_stat(fe, &stbuf);
        if (filler(buf, fe->filename, &stbuf, 0))
            break;
        current = fe->next;
    }
    return 0;
}

/// Get the parent directory (object) of path.
///
/// \param path path
/// \param dir [output] parent directory object
/// \return index of the beginning of filename part in path
static size_t parent_dir(const char *path, struct file_entry **dir) {
    char dirpath[4096];
    int j = strlen(path) - 1;
    while (path[j] != '/' && j >= 0)
        j--;
    strncpy(dirpath, path, j);
    dirpath[j] = 0;
    TRACE("%s: %s -> %s\n", __FUNCTION__, path, j == 0 ? "(root)" : dirpath);
    if (j == 0) {
        *dir = blocks[0];
        return 1;
    }
    *dir = find_file_by_path(dirpath + 1);
    if (*dir)
        TRACE("  Found parent dir: %s\n", (*dir)->filename);
    return j+1;
}

int osh_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    (void) fi;

    TRACE("%s: %s\n", __FUNCTION__, path);

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
    mdblk = take_free_block();
    if (!mdblk)
        return -ENOSPC;
    fe = blocks[mdblk] = blkalloc();

    // Metadata.
    strncpy(fe->filename, path+j, sizeof(fe->filename));
    fe->head = 0;
    fe->tail = 0;
    fe->next = dir->child;
    fe->blocks = 0;
    fe->size = 0;
    fe->mode = (mode & 0777) | S_IFREG;
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

    TRACE("%s: %s\n", __FUNCTION__, path);

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
    TRACE("%s: %s\n", __FUNCTION__, path);
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
    TRACE("%s: %s\n", __FUNCTION__, path);
    struct file_entry *fe = find_file_by_path(path + 1);
    if (!fe)
        return -ENOENT;
    fi->fh = (uint64_t) fe;
    clock_gettime(CLOCK_REALTIME, &fe->atime);
    return 0;
}

int do_read(struct file_entry *fe, char *buf, size_t size, off_t offset, int issymlink)
{
    TRACE("%s: %s (size %lu) (offset %ld)\n", __FUNCTION__, fe->filename, size, offset);

    if (fe->head == 0)
        return 0;
    if (issymlink && !S_ISREG(fe->mode))
        return 0;
    if (!issymlink && S_ISLNK(fe->mode))
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

int osh_read(const char *path, char *buf, size_t size, off_t offset,
             struct fuse_file_info *fi)
{
    struct file_entry *fe = NULL;

    if (fi->fh)
        fe = (struct file_entry *) fi->fh;
    else
        fe = find_file_by_path(path);
    if (!fe)
        return -ENOENT;

    return do_read(fe, buf, size, offset, 0);
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

    TRACE("  %s: size=%lu offset=%ld\n", __FUNCTION__, size, offset);

    size_t X = (size_t) offset, Y = offset + size;
    struct data_node *prev = prevblk ? (struct data_node *) blocks[prevblk] : NULL;
    struct data_node *cur = curblk ? (struct data_node *) blocks[curblk] : NULL;

    if (cur) {
        size_t A = cur->beg, B = cur->beg + cur->len;

        if (X < A) {
            // Append a block before the current one.
            size_t blk = take_free_block();
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
                if (cur) {
                    new->prev = cur->prev;
                    cur->prev = blk;
                }
            } else {
                new->next = fe->head;
                new->prev = 0;
                fe->head = blk;
                if (cur)
                    cur->prev = blk;
            }
            TRACE("New block beg=%lu len=%lu\n", new->beg, new->len);

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
    else {  // cur == 0
        // We've exceeded the file boundary... Allocate new blocks.
        // N.B. This only happens at the end.

        // If there's a block before, try to merge into it.
        if (prev && X < prev->beg + sizeof(prev->body)) {
            size_t A = prev->beg, B = prev->beg + prev->len;
            size_t M = sizeof(prev->body);
            size_t len = MIN(A + M - X, Y-X);

            TRACE("Address space: %lu ~ %lu\n", X, Y);
            TRACE("Merging to %lu, len=%lu\n", X-A, len);

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
        size_t blk = take_free_block();
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
            new->prev = prevblk;
        }
        else {
            new->next = fe->head;
            new->prev = 0;
            fe->head = blk;
        }
        fe->tail = blk; // fe->tail should always point to the last block.

        memcpy(new->body, buf, new->len);
        return do_write(buf+new->len, size-new->len, offset+new->len, fe, blk, new->next);
    }
}

int osh_write(const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi)
{
    (void) fi;
    TRACE("%s: %s (size %lu) (off %ld)\n", __FUNCTION__, path, size, offset);

    struct file_entry *fe = find_file_by_path(path + 1);
    if (!fe)
        return -ENOENT;

    // Nothing is changed.
    if (fe->head == 0 && size == 0)
        return 0;

    // Locate the appropriate block to start writing.
    size_t curblk = fe->tail;
    struct data_node *cur = NULL;
    if (fe->tail) {
        cur = blocks[fe->tail];
        while ((size_t) offset < cur->beg) {
            TRACE(" cur tail = %lu~%lu prev=%lu\n", cur->beg, cur->beg+cur->len, cur->prev);
            if (cur->prev != 0) {
                curblk = cur->prev;
                cur = blocks[cur->prev];
            }
            else {
                break;
            }
        }
        TRACE(" fin tail = %lu~%lu prev=%lu\n", cur->beg, cur->beg+cur->len, cur->prev);
    }

    // Do write. Expand the file on demand.
    if (do_write(buf, size, offset, fe, cur? cur->prev: 0, curblk) < 0)
        return -ENOSPC;

    fe->size = MAX(fe->size, size+offset);

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
    TRACE("%s: %s\n", __FUNCTION__, path);

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

        if (rmdir) {
            if (!S_ISDIR(fe->mode))
                return -ENOTDIR;
            else if (fe->child != 0)
                return -ENOTEMPTY;
        }

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

            if (rmdir) {
                if (!S_ISDIR(fe->mode))
                    return -ENOTDIR;
                else if (fe->child != 0)
                    return -ENOTEMPTY;
            }

            fe->next = next->next;
            do_unlink(nblk);
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
    TRACE("%s: %s %o\n", __FUNCTION__, path, mode);

    struct file_entry* fe = find_file_by_path(path + 1);
    fe->mode = mode;
    clock_gettime(CLOCK_REALTIME, &fe->ctime);
    return 0;
}

int osh_chown(const char *path, uid_t owner, gid_t group) {
    TRACE("%s: %s\n", __FUNCTION__, path);

    struct file_entry *fe = find_file_by_path(path + 1);
    clock_gettime(CLOCK_REALTIME, &fe->ctime);
    fe->uid = owner;
    fe->gid = group;
    return 0;
}

int osh_truncate(const char *path, off_t len)
{
    TRACE("%s: %s %ld\n", __FUNCTION__, path, len);

    struct file_entry *fe = find_file_by_path(path + 1);
    if (!fe)
        return -ENOENT;

    size_t cur = fe->head;
    size_t pblk = 0;
    struct data_node *node;
    while (cur) {
        node = (struct data_node *) blocks[cur];

        if ((size_t) len <= node->beg) {
            fe->tail = pblk;
            do_drop_data_blocks(cur, fe);
            if (pblk) {
                struct data_node *prev = (struct data_node *) blocks[pblk];
                prev->next = 0;
            } else {
                fe->head = 0;
                fe->tail = 0;
            }
            break;
        }
        else if ((size_t) len <= node->beg + node->len) {
            node->len = len - node->beg;
            if (node->next) {
                fe->tail = cur;
                node->next = 0;
                do_drop_data_blocks(node->next, fe);
            }
            break;
        }
        else {
            cur = node->next;
            pblk = cur;
        }
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

    TRACE("%s: %s\n", __FUNCTION__, path);

    if (find_file_by_path(path + 1))
        return 0;
    else
        return -ENOENT;
}

int osh_mkdir(const char *path, mode_t mode)
{
    TRACE("%s: %s\n", __FUNCTION__, path);

    struct file_entry *dir;
    size_t j = parent_dir(path, &dir);
    if (!dir)
        return -ENOENT;
    else if (dir == NOTDIR)
        return -ENOTDIR;

    // Create metadata.
    size_t blk = take_free_block();
    if (!blk)
        return -ENOSPC;

    struct file_entry *fe = blocks[blk] = blkalloc();
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    strncpy(fe->filename, path+j, MAX_FILENAME);
    fe->head = 0;
    fe->tail = 0;
    fe->child = 0;
    fe->mode = (mode & 0777) | S_IFDIR;
    fe->size = OSHFS_BLKSIZ;
    fe->blocks = 1;
    fe->uid = getuid();
    fe->gid = getgid();
    fe->nlink = 1;
    fe->atime = now;
    fe->mtime = now;
    fe->ctime = now;

    // Prepend to dir.
    fe->next = dir->child;
    dir->child = blk;

    return 0;
}

int osh_rename(const char *from, const char *to)
{
    TRACE("%s: %s -> %s\n", __FUNCTION__, from, to);

    if (!strcmp(from, to))
        return 0;

    struct file_entry *olddir, *newdir;
    struct file_entry *fe, *oldprev;
    size_t i, j;
    i = parent_dir(from, &olddir);
    TRACE("olddir = %s\n", olddir->filename);
    j = parent_dir(to, &newdir);
    TRACE("newdir = %s\n", newdir->filename);
    size_t mdblk;

    fe = do_find_file_by_path(from + i, olddir, &oldprev, &mdblk);

    TRACE("Found file %s in directory %s, moving to new directory %s\n", fe->filename, olddir->filename, newdir->filename);

    if (!olddir || !newdir)
        return -ENOENT;
    else if (olddir == NOTDIR || newdir == NOTDIR)
        return -ENOTDIR;

    // Detach old file from the old directory.
    if (oldprev) {
        oldprev->next = fe->next;
    }
    else {
        olddir->child = fe->next;
    }

    // Append new file to the new directory.
    fe->next = newdir->child;
    newdir->child = mdblk;

    strncpy(fe->filename, to + j, MAX_FILENAME);

    return 0;
}

int osh_symlink(const char *target, const char *linkpath)
{
    TRACE("%s: %s -> %s\n", __FUNCTION__, target, linkpath);

    size_t mdblk, j;
    struct file_entry *fe;
    struct file_entry *dir;
    struct timespec now;

    // Find the containing directory
    j = parent_dir(linkpath, &dir);
    if (dir == 0)
        return -ENOENT;
    else if (dir == NOTDIR)
        return -ENOTDIR;

    // Find a free block for metadata.
    mdblk = take_free_block();
    if (!mdblk)
        return -ENOSPC;
    fe = blocks[mdblk] = blkalloc();

    // Metadata.
    strncpy(fe->filename, linkpath+j, sizeof(fe->filename));
    fe->head = 0;
    fe->tail = 0;
    fe->next = dir->child;
    fe->blocks = 0;
    fe->size = strlen(target);
    fe->mode = 0777 | S_IFLNK;
    fe->uid = getuid();
    fe->gid = getgid();
    fe->nlink = 1;
    clock_gettime(CLOCK_REALTIME, &now);
    fe->mtime = now;
    fe->atime = now;
    fe->ctime = now;
    dir->child = mdblk;

    // Write link.
    do_write(target, strlen(target), 0, fe, 0, fe->head);

    return 0;
}

int osh_readlink(const char *path, char *buf, size_t size)
{
    struct file_entry *fe = find_file_by_path(path);
    int res = do_read(fe, buf, size, 0, 1);
    return MIN(res, 0);
}

int osh_release(const char *path, struct fuse_file_info *file)
{
    (void) path;
    (void) file;
    return 0;
}

int osh_mknod(const char *path, mode_t mode, dev_t dev)
{
    TRACE("%s: %s\n", __FUNCTION__, path);

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
    mdblk = take_free_block();
    if (!mdblk)
        return -ENOSPC;
    fe = blocks[mdblk] = blkalloc();

    // Metadata.
    strncpy(fe->filename, path+j, sizeof(fe->filename));
    fe->head = 0;
    fe->tail = 0;
    fe->next = dir->child;
    fe->blocks = 0;
    fe->size = 0;
    fe->mode = mode;
    fe->dev = dev;
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

int osh_statfs(const char *path, struct statvfs *stbuf)
{
    (void) path;
    memcpy(stbuf, statfs, sizeof(struct statvfs));
    return 0;
}
