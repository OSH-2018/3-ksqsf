# OSHFS

OSHFS is an in-memory filesystem written for FUSE, with absolutely **ZERO** heap allocation.

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

All of them are random accesses in O(n) time.  Trailing data blocks are automatically merged.

## Design

OSHFS is a simple file system implemented completely in linked lists.

[TODO]

## Further improvements

Further improvements to be made in the future?

* Advanced data structures (skip list? balanced binary tree? B-tree?) for more efficient random accesses.
