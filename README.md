# OSHFS

OSHFS is an in-memory filesystem written for FUSE.

File operations:

* Create
* Read
* Write
* Delete
* Truncate

All of them are random accesses in $O(n)$ time.  Trailing blocks are automatically merged.

## Design

OSHFS is a simple file system implemented completely in linked lists.

[TODO]

## Further improvements

Further improvements to be made in the future?

* Balanced binary tree (or B-tree?) for $O(\log n)$ random accesses.
* Directory?
