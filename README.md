# OSHFS

OSHFS is an in-memory filesystem written for FUSE, with absolutely
**ZERO** heap allocation (except `mmap`).

File operations:

* Create
* Read
* Write
* Delete
* Truncate
* Move (rename)

Directory operations:

* Create
* Remove
* Move (rename)

Other:

* Device files
* Statistics (as shown by `df`)
* Symbolic links

All of them are random accesses in O(n) time.  Trailing data blocks
are automatically merged.

## Design

OSHFS is a simple file system implemented completely in linked lists.

### Blocks

To closely mimic a hard disk, I divide the memory space into evenly
spaced 4-KiB blocks.  Each block is occupied by either a file entry
(metadata) or a data node (file data).

### Block Allocation

A static free list is maintained.  A new free block can be allocated
in O(1) time.

### Read / Write

A file consists of a file entry and a data list.  The file entry
points to the first and the last data node to accerlate sequential
reads and appends.

### Directory

A directory is a normal file entry, but utilizes the `child` field.

The children are linked by `next` field.  Because of this, the files
can't be linked from arbitrary locations, so hard links are impossible
unless a new layer of indirection is introduced.

## Limitations

Since the memory space is evenly divided and aligned, it's not so easy
to make everything flexible and fast, so I can only achieve 20 MiB/s
in sequential writes.

## Further improvements

Further improvements to be made in the future?

* Advanced data structures (skip list? balanced binary tree? B-tree?)
  for more efficient random accesses.
