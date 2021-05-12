
#include "mpmalloc.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

static const struct mp_unit g_mem_size_type[] = 
{
    {8, 500},
    {16, 500},
    {32, 500},
    {64, 500},
    {128, 500},
    {256, 500},
    {512, 500},
    {768, 500},
    {1024, 500},
};

#define TEST_RUN_TIMES 5000
#define THREAD_NUM     5
void *thread_pm_alloc_test(void *args)
{
    int i;
    int j;
    void *ptr;
    void *parr[TEST_RUN_TIMES];
    struct mp_handle* mp = (struct mp_handle*)args;

    j = 0;
    for (i = 0 ; i < TEST_RUN_TIMES; i++) {
        j = j % (sizeof(g_mem_size_type)/sizeof(struct mp_unit));
        //parr[i] = mp_calloc(mp, 1, g_mem_size_type[j].size);
        parr[i] = mp_calloc(mp, 1, (i/4*4)%1024 + 1024);
        j++;
        assert(parr[i] != NULL);
    }

    for (i = 0 ; i < TEST_RUN_TIMES; i++) {
        mp_free(mp, parr[i]);
    }
    printf("thread_test exit.");
}

void *thread_glibc_alloc_test(void *args)
{
    int i;
    int j;
    void *ptr;
    void *parr[TEST_RUN_TIMES];

    j = 0;
    for (i = 0 ; i < TEST_RUN_TIMES; i++) {
        j = j % (sizeof(g_mem_size_type)/sizeof(struct mp_unit));
        //parr[i] = calloc(1, g_mem_size_type[j].size);
        parr[i] = calloc(1, (i/4*4)%1024 + 1024);
        j++;
        assert(parr[i] != NULL);
    }

    for (i = 0 ; i < TEST_RUN_TIMES; i++) {
        free(parr[i]);
    }
    printf("thread_test exit.");
}

static const char type_str[2][64] = {"mempool", "glibc"};

struct mp_hash_node
{
    size_t               size;
};

#define MP_SIZE_SWAP(a, b) \
{                   \
    a = a ^ b;      \
    b = a ^ b;      \
    a = a ^ b;      \
}

static inline int mp_hash_find_mid(const struct mp_hash_node *nodes, size_t key, int *high, int *low, int *index)
{
    int mid_1, mid_2;

    if (*high > *low) {
        mid_1 = *low + (*high - *low)*(key - nodes[*low].size) / (nodes[*high].size - nodes[*low].size);
        mid_2 = (*high + *low) / 2;
        if (mid_1 > mid_2) {
            MP_SIZE_SWAP(mid_1, mid_2)
        }
    }else{
        mid_1 = mid_2 = *high;
    }
    if (key < nodes[mid_1].size) {
        *index = mid_1;
        *high = mid_1 - 1;
    }  else if (key == nodes[mid_1].size) {
        *index = mid_1;
        return -1;
    }else if (key > nodes[mid_2].size) {
        *low = mid_2 + 1;
    }else if (key == nodes[mid_2].size) {
        *index = mid_2;
        return -1;
    } else {
        *index = mid_2;
        *high = mid_2 - 1;
        *low = mid_1 + 1;
    }
    return 0;
}

static int mp_hash_mem_skip_search(struct mp_hash_node *nodes, int nodes_num, size_t key, size_t *loop)
{
	int low = 0;
	int high = nodes_num - 1;
    int index = high;

    /* 内部接口，避免重复校验，入参由调用者校验 */
	while (low <= high) {
        (*loop)++;
        if (key <= nodes[low].size) {
            return low;
        } else if(key > nodes[high].size) {
            return index;
        }else{
            if (mp_hash_find_mid(nodes, key, &high, &low, &index) != 0) {
                break;
            }
        }
        //printf("loop[%lu], high:[%d], low:[%d], index:[%d],\n", *loop, high, low, index);
	}

	return index;
}

static int mp_binary_search(struct mp_hash_node *nodes, int nodes_num, size_t key, size_t *loop)
{
	int low = 0;
	int high = nodes_num - 1;
    int index = high;
    int mid;
    /* 内部接口，避免重复校验，入参由调用者校验 */
    if (key >= nodes[high].size) {
        return high;
    }
    
    if (key <= nodes[low].size) {
        return low;
    }

	while (low <= high) {
       (*loop)++;
		mid = (high + low)/2;
        if (key < nodes[mid].size){
            high = mid -1;
            index = mid;
        } else if (key > nodes[mid].size) {
            low = mid + 1;
        } else {
            return mid;
        }
	}

    //MP_LOG_DEBUG("find: index[%d], val:[%lu]", index, nodes[index].size);
	return index;
}

static int mp_insert_search(struct mp_hash_node *nodes, int nodes_num, size_t key, size_t *loop)
{
	int low = 0;
	int high = nodes_num - 1;
    int index = high;
    int mid;
    
    if (key >= nodes[high].size) {
        return high;
    }
    
    if (key <= nodes[low].size) {
        return low;
    }

	while (low <= high) {
        (*loop)++;
        if (key <= nodes[low].size) {
            return low;
        }else if (key > nodes[high].size) {
            return index;
        }

        if (low == high) {
            mid = low;
        }else{
            mid = low + (high - low)*(key - nodes[low].size) / (nodes[high].size - nodes[low].size);
        }
		
        if (key < nodes[mid].size){
            high = mid -1;
            index = mid;
        } else if (key > nodes[mid].size) {
            low = mid + 1;
        } else {
            return mid;
        }
        //printf("loop[%lu], high:[%d], low:[%d], index:[%d],\n", *loop, high, low, index);
	}

	return index;
}

#define NODES_NUM 40
#define SEARCH_NUM 50000

int test_time(size_t nodes_num, size_t search_times)
{

    size_t i, j;
    size_t loop = 0;
    size_t loop_i = 0;
    size_t loop_b = 0;
    int fast_fail = 0;
    size_t index = 0;
    size_t index_m = 0;
    long long inter = 0;
	struct timeval start_now;
	struct timeval end_now;
    struct mp_hash_node *nodes;
    //struct mp_hash_node *nodes2;
    size_t *keys =  (size_t *)malloc(nodes_num * sizeof(size_t));

    /*nodes = (struct mp_hash_node *)malloc(nodes_num * sizeof(struct mp_hash_node));
    for (i = 0; i < nodes_num; i++) {
        nodes[i].size = (i + 1)*2;
        keys[i] = nodes[i].size - 1;
    }*/

    nodes = (struct mp_hash_node *)malloc(nodes_num * sizeof(struct mp_hash_node));
    loop = 1;
    for (i = 0; i < nodes_num; i++) {
        if (i < nodes_num/3) {
            nodes[i].size = (i + 1)*2;
        }else if (i < nodes_num*2/3) {
            nodes[i].size =  nodes_num*3 + (i + 1)*4;
        }else{
            nodes[i].size = 2 * nodes_num*3 + (i + 1)*8;
        }
        keys[i] = nodes[i].size - 1;
        //printf("%lu\n", keys[i]);
    }

    loop = 0;
    gettimeofday(&start_now, NULL);	//
    for (i = 0; i < nodes_num; i++) {
        for (j = 0; j < search_times/nodes_num;j++){
            index = mp_insert_search(nodes, nodes_num, keys[i], &loop);
            assert((keys[i] + 1) == nodes[index].size);
        }
    }
    gettimeofday(&end_now, NULL);	//
    inter = (long long)(end_now.tv_sec*1000000 + end_now.tv_usec) - (long long)(start_now.tv_sec*1000000 + start_now.tv_usec);
    printf("插值查找- key[1 - %ld], search: %6lu, loop: %6ld, time: %lu us.\n", keys[i -1], search_times, loop, inter);

    loop = 0;
    gettimeofday(&start_now, NULL);	//
    for (i = 0; i < nodes_num; i++) {
        for (j = 0; j < search_times/nodes_num;j++){
            index = mp_binary_search(nodes, nodes_num, keys[i], &loop);
            assert((keys[i] + 1) == nodes[index].size);
        }
    }
    gettimeofday(&end_now, NULL);	//
    inter = (long long)(end_now.tv_sec*1000000 + end_now.tv_usec) - (long long)(start_now.tv_sec*1000000 + start_now.tv_usec);
    printf("折半查找- key[1 - %ld], search: %6lu, loop: %6ld, time: %lu us.\n", keys[i -1], search_times, loop, inter);

    loop = 0;
    gettimeofday(&start_now, NULL);	//
    for (i = 0; i < nodes_num; i++) {
        for (j = 0; j < search_times/nodes_num;j++){
            index = mp_hash_mem_skip_search(nodes, nodes_num, keys[i], &loop);
            assert((keys[i] + 1) == nodes[index].size);
        }
    }
    gettimeofday(&end_now, NULL);	//
    inter = (long long)(end_now.tv_sec*1000000 + end_now.tv_usec) - (long long)(start_now.tv_sec*1000000 + start_now.tv_usec);
    printf("混合查找- key[1 - %ld], search: %6lu, loop: %6ld, time: %lu us.\n", keys[i -1], search_times, loop, inter);

    if (nodes) {
        free(nodes);
    }
    if (keys) {
        free(keys);
    }

    return 0;
}

int main(int argc, char *argv[])
{
    struct mp_handle* mp = NULL;
    int i;
    int j;
    int type = 0;
    size_t interval = 0;
    pthread_t		thread_id[THREAD_NUM] = {0};
	struct timeval start_now;
	struct timeval end_now;

    mp = mp_create(g_mem_size_type, (sizeof(g_mem_size_type)/sizeof(struct mp_unit)), MP_METHOD_E_DEFAULT);
    if (!mp) {
        printf("mp_create pdn mempool fail, regist type num of size:%d, method[%d]!\n",
        (int)(sizeof(g_mem_size_type)/sizeof(struct mp_unit)), MP_METHOD_E_DEFAULT);
        return -1;
    }

    if (argc > 1 && argv[1]) {
        type = atoi(argv[1]);
        if (type > 1) {
            type = 0;
        }
    }

    gettimeofday(&start_now, NULL);	//
	for (i = 0; i < THREAD_NUM; i++) {
        if (type == 0) {
            pthread_create(&thread_id[i], NULL, thread_pm_alloc_test, mp);
        }else{
            pthread_create(&thread_id[i], NULL, thread_glibc_alloc_test, mp);
        }
	}

	for (i = 0; i < THREAD_NUM; i++)
		pthread_join(thread_id[i], NULL);
	gettimeofday(&end_now, NULL);	//

    interval = (long long)(end_now.tv_sec*1000000 + end_now.tv_usec) - (long long)(start_now.tv_sec*1000000 + start_now.tv_usec);
	printf("\n\n################################################\n");;
	printf("##### thread num:[%u].\n", THREAD_NUM);
	printf("##### %s alloc total times:[%lu us] .\n\n", type_str[type], interval);
	printf("\n\n################################################\n");

    mp_destroy(mp);

    for (i = 1; i <= NODES_NUM; i++) {
        //printf("%lu\n", 10*i*i);
        printf("nodes-- %lu\n", 10*i*i);
        test_time(10*i*i, SEARCH_NUM);
    }

    return 0;
exit_tst:
    mp_destroy(mp);
    return -1;
}