
#include "mpmalloc.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

static const struct mp_unit g_mem_size_type[] = 
{
    {8, 1024},
    {16, 1024},
    {32, 1024},
    {64, 1024},
    {128, 1024},
    {256, 768},
    {512, 256},
    {768, 256},
    {1024, 128},
};

#define TEST_RUN_TIMES 1000

int main(void)
{
    struct mp_handle* mp = NULL;
    int i;
    int j;
    void *ptr;
    void *parr[TEST_RUN_TIMES];

    mp = mp_create(g_mem_size_type, (sizeof(g_mem_size_type)/sizeof(struct mp_unit)), MP_METHOD_E_DEFAULT);
    if (!mp) {
        printf("mp_create pdn mempool fail, regist type num of size:%d, method[%d]!\n",
        (int)(sizeof(g_mem_size_type)/sizeof(struct mp_unit)), MP_METHOD_E_DEFAULT);
        return -1;
    }

    ptr = mp_malloc(mp, 128);
    if (ptr == NULL) {
        printf("mp_malloc fail.");
        goto exit_tst;
    }

    mp_free(mp, ptr);

    j = 0;
    for (i = 0 ; i < TEST_RUN_TIMES; i++) {
        j = j % (sizeof(g_mem_size_type)/sizeof(struct mp_unit));
        parr[i] = mp_calloc(mp, 1, g_mem_size_type[j].size);
        j++;
        assert(parr[i] != NULL);
    }

    for (i = 0 ; i < TEST_RUN_TIMES; i++) {
        mp_free(mp, parr[i]);
    }


    mp_destroy(mp);

    return 0;
exit_tst:
    mp_destroy(mp);
    return -1;
}