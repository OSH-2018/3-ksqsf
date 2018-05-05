//
// Created by ksqsf on 18-5-3.
//

#include <stdlib.h>
#include <memory.h>
#include "bitmap.h"

bitmap_t bm_new(size_t nbits)
{
    size_t nbuckets = (nbits + 7) / 8;
    unsigned char *bkts = malloc(nbuckets);
    memset(bkts, 0, nbuckets);
    bitmap_t bm;
    bm.nbits = nbits;
    bm.bkts = bkts;
    return bm;
}

int bm_set(bitmap_t bm, size_t n, int v)
{
    bm.bkts[n/8] = bm.bkts[n/8] | ((unsigned char) 1 << (n % 8));
    return v;
}

int bm_get(bitmap_t bm, size_t n)
{
    return (bm.bkts[n/8] >> (n%8)) & 1;
}
