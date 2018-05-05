//
// Created by ksqsf on 18-5-3.
//

#ifndef INC_3_KSQSF_BITMAP_H
#define INC_3_KSQSF_BITMAP_H

#include <stddef.h>

typedef struct {
    size_t nbits;
    unsigned char *bkts;
} bitmap_t;

bitmap_t bm_new(size_t nbits);
int bm_set(bitmap_t bm, size_t n, int v);
int bm_get(bitmap_t bm, size_t n);

#endif //INC_3_KSQSF_BITMAP_H
