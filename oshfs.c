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

struct superblock sb;
struct stat root_attrs;
void *blocks[OSHFS_NBLKS];

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
    printf("%s\n", __FUNCTION__);

    return mmap(NULL, OSHFS_BLKSIZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

/// Drop a block and free the memory.
/// \param n position
static void blkdrop(size_t n)
{
    printf("%s %lu\n", __FUNCTION__, n);

    // Reclaim resources.
    munmap(blocks[n], OSHFS_BLKSIZ);
    blocks[n] = NULL;
}

/// Write block back to disk.
/// \param n block number
/// \param data block data
static void blkdump(off_t n, void *data, size_t len)
{
    printf("%s: at %lu, len %lu\n", __FUNCTION__, n, len);

    char *blk = blocks[n];
    if (!blk) {
        blk = blocks[n] = blkalloc();
    }
    memcpy(blk, data, len > OSHFS_BLKSIZ ? OSHFS_BLKSIZ : len);
    if (len < OSHFS_BLKSIZ) {
        memset(blk + len, 0, OSHFS_BLKSIZ - len);
    }
}

/// Find file entry by name.
/// \param filename File name.
/// \return The file entry.
static struct file_entry *find_file_by_name(const char *filename)
{
    printf("%s: %s\n", __FUNCTION__, filename);

    off_t current = sb.first_entry;
    while (current != 0) {
        struct file_entry *fe = (struct file_entry *) blocks[current];
        if (!strcmp(filename, fe->filename))
            return fe;
        current = fe->next;
    }
    return NULL;
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

static void updatesb()
{
    blkdump(0, &sb, sizeof(sb));
}

void *osh_init(struct fuse_conn_info *conn)
{
    (void) conn;
    printf("%s\n", __FUNCTION__);
    struct timespec now;

    // Prepare rootfs attributes.
    clock_gettime(CLOCK_REALTIME, &now);
    root_attrs.st_mode = S_IFDIR | 0755;
    root_attrs.st_atim = now;
    root_attrs.st_ctim = now;
    root_attrs.st_mtim = now;
    root_attrs.st_uid = 0;
    root_attrs.st_gid = 0;
    root_attrs.st_size = OSHFS_BLKSIZ;
    root_attrs.st_nlink = 1;

    // Set up superblock and write it to the disk.
    sb.first_entry = 0;
    updatesb();
}

int osh_getattr(const char *path, struct stat *stbuf)
{
    printf("%s: %s\n", __FUNCTION__, path);

    struct file_entry *fe;

    if (!strcmp(path, "/")) {
        memcpy(stbuf, &root_attrs, sizeof(root_attrs));
        return 0;
    }
    else {
        fe = find_file_by_name(path + 1); // Skip '/'
        if (!fe)
            return -ENOENT;

        fill_stat(fe, stbuf);

        return 0;
    }
}

int osh_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                off_t offset, struct fuse_file_info *fi)
{
    (void) path;
    (void) offset;
    (void) fi;

    off_t current = sb.first_entry;
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

int osh_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    (void) fi;

    printf("%s: %s\n", __FUNCTION__, path);

    size_t mdblk;
    struct file_entry fe;
    struct timespec now;

    // Find a free block for metadata.
    mdblk = find_free_block();
    if (!mdblk)
        return -ENOSPC;

    // Metadata.
    strncpy(fe.filename, path+1, sizeof(fe.filename));
    fe.head = 0;
    fe.next = sb.first_entry;
    fe.blocks = 0;
    fe.size = 0;
    fe.mode = mode;
    fe.uid = getuid();
    fe.gid = getgid();
    fe.nlink = 1;
    clock_gettime(CLOCK_REALTIME, &now);
    fe.mtime = now;
    fe.atime = now;
    fe.ctime = now;

    // Write metadata onto disk.
    blkdump(mdblk, &fe, sizeof(fe));

    // Update superblock
    sb.first_entry = mdblk;
    updatesb();

    return 0;
}

int osh_access(const char *path, int mask)
{
    (void) mask;

    printf("%s: %s\n", __FUNCTION__, path);

    if (!strcmp("/", path))
        goto ok;

    struct file_entry *fe = find_file_by_name(path + 1);
    if (!fe)
        return -ENOENT;

ok:
    clock_gettime(CLOCK_REALTIME, &fe->atime);

    return 0;
}

int osh_utimens(const char *path, const struct timespec ts[2])
{
    printf("%s: %s\n", __FUNCTION__, path);
    struct file_entry *fe = find_file_by_name(path + 1);
    if (!fe)
        return -ENOENT;
    if (!strcmp(path, "/")) {
        root_attrs.st_atim = ts[0];
        root_attrs.st_mtim = ts[1];
    } else {
        fe->atime = ts[0];
        fe->mtime = ts[1];
    }
    clock_gettime(CLOCK_REALTIME, &fe->ctime);
    return 0;
}

int osh_open(const char *path, struct fuse_file_info *fi)
{
    printf("%s: %s\n", __FUNCTION__, path);
    struct file_entry *fe = find_file_by_name(path + 1);
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
        fe = find_file_by_name(path + 1);

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
static void do_write(const char *buf, size_t size, off_t offset, struct file_entry *fe, size_t prevblk, size_t curblk)
{
    if (size == 0)
        return;

    printf("%s: size=%lu offset=%ld data=%s\n", __FUNCTION__, size, offset, buf);

    size_t X = (size_t) offset, Y = offset + size;
    struct data_node *prev = prevblk ? (struct data_node *) blocks[prevblk] : NULL;
    struct data_node *cur = curblk ? (struct data_node *) blocks[curblk] : NULL;

    if (cur) {
        size_t A = cur->beg, B = cur->beg + cur->len;

        if (X < A) {
            // Append a block before the current one.
            size_t blk = find_free_block();
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
            do_write(buf + new->len, size - new->len, offset + new->len, fe, curblk, blk);
        } else if (X < B) {
            size_t len = MIN(Y - X, cur->len);
            memcpy(cur->body + X - A, buf, len);
            do_write(buf + len, size - len, offset + len, fe, curblk, cur->next);
        } else {
            do_write(buf, size, offset, fe, curblk, cur->next);
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
            return;

        // In case there's something left...
        size_t blk = find_free_block();
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
        do_write(buf+new->len, size-new->len, offset+new->len, fe, blk, new->next);
    }
}

int osh_write(const char *path, const char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi)
{
    printf("%s: %s (size %lu) (off %ld) %s\n", __FUNCTION__, path, size, offset, buf);

    struct file_entry *fe = (struct file_entry *) fi->fh;
    if (!fe)
        fe = find_file_by_name(path + 1);
    size_t curblk = fe->head;

    // Nothing is changed.
    if (curblk == 0 && size == 0)
        return 0;

    // Do write. Expand the file on demand.
    do_write(buf, size, offset, fe, 0, fe->head);
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

int osh_unlink(const char *path)
{
    printf("%s: %s\n", __FUNCTION__, path);

    size_t current;
    struct file_entry *fe = (struct file_entry *) blocks[sb.first_entry];

    printf("%s = %s\n", __FUNCTION__, fe->filename);
    if (!strcmp(fe->filename, path + 1)) {
        size_t t = fe->next;
        do_unlink(sb.first_entry);
        sb.first_entry = t;
        updatesb();
        return 0;
    }

    for (current = sb.first_entry; current; current = fe->next) {
        fe = (struct file_entry *) blocks[current];
        size_t next = fe->next;
        if (!next)
            break;
        if (!strcmp(((struct file_entry *) blocks[next])->filename, path + 1)) {
            fe->next = ((struct file_entry *) blocks[next])->next;
            blkdrop(next);
            return 0;
        }
    }

    return -ENOENT;
}

int osh_chmod(const char *path, mode_t mode) {
    printf("%s: %s %o\n", __FUNCTION__, path, mode);

    struct file_entry* fe = find_file_by_name(path+1);
    fe->mode = mode;
    clock_gettime(CLOCK_REALTIME, &fe->ctime);
    return 0;
}

int osh_chown(const char *path, uid_t owner, gid_t group) {
    printf("%s: %s\n", __FUNCTION__, path);

    struct file_entry *fe = find_file_by_name(path+1);
    clock_gettime(CLOCK_REALTIME, &fe->ctime);
    fe->uid = owner;
    fe->gid = group;
    return 0;
}

int osh_truncate(const char *path, off_t len)
{
    struct file_entry *fe = find_file_by_name(path+1);

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

    if (find_file_by_name(path + 1))
        return 0;
    else
        return -ENOENT;
}