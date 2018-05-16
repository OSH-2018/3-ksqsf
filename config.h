//
// Created by ksqsf on 18-5-3.
//

#ifndef INC_3_KSQSF_CONFIG_H
#define INC_3_KSQSF_CONFIG_H

#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 26

#define OSHFS_SIZE (4 * 1024 * 1024 * (size_t)1024)
#define OSHFS_BLKSIZ 4096
#define OSHFS_NBLKS (OSHFS_SIZE / OSHFS_BLKSIZ)
#define MAX_FILENAME 256

#endif //INC_3_KSQSF_CONFIG_H
