
#include "mempool.h"
#include "queue.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* 内存池适配*/
#define MEMPOOL_MAGIC           0xb5

struct mempool_imp{
    int mempool_id;
    QUEUE q_idle;
	QUEUE q_used;
    size_t used_cnt;
    size_t  count;
    size_t ele_size;
    pthread_mutex_t lck;
};

struct mempool_slice{
    QUEUE       q;
    char        id;
    char        magic;
    char        reserved[2];
    char    data[0];     
};

struct mempool_imp *mempool_create_imp(int id, size_t count, size_t ele_size)
{
    int rc;
    int i;
    char *page;
    size_t memsize;
    size_t slice_size;
    struct mempool_imp *handle;
    struct mempool_slice *slices;

    if (!count || !ele_size) {
        return NULL;
    }
    slice_size = (sizeof(struct mempool_slice) + ele_size);
    memsize = sizeof(struct mempool_imp) + count * slice_size;
    page = mmap(NULL, memsize, PROT_READ | PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB, -1, 0);
    if (!page) {
        return NULL;
    }

    handle = (struct mempool_imp *)page;
    memset(handle, 0, sizeof(struct mempool_imp));

    handle->count = count;
    handle->ele_size = ele_size;
    handle->mempool_id = id;
    handle->used_cnt = 0;

    rc = pthread_mutex_init(&handle->lck, NULL);
    if (rc != 0) {
        mempool_free_imp(handle);
        return NULL;
    }

    QUEUE_INIT(&handle->q_idle);
	QUEUE_INIT(&handle->q_used);

    for (i = 0; i < count; i++) {
        slices = (struct mempool_slice *)(page + sizeof(struct mempool_imp) + i * slice_size);
        QUEUE_INIT(&slices->q);
        slices->id = i;
        slices->magic = MEMPOOL_MAGIC;
        QUEUE_INSERT_TAIL(&handle->q_idle, &slices->q);
    }

    return handle;
}
void mempool_free_imp(struct mempool_imp *mp)
{
    int rc;
    size_t memsize;

    if (!mp) {
        return;
    }

    rc = pthread_mutex_lock(&mp->lck);
    if (rc != 0) {
        return;
    }
    
    memsize = (sizeof(struct mempool_slice) + mp->ele_size);
    memsize = sizeof(struct mempool_imp) + mp->count * memsize;
    pthread_mutex_unlock(&mp->lck);
    
    assert(mp->used_cnt == 0);

    pthread_mutex_destroy(&mp->lck);

    munmap(mp, memsize);
    return;
}

void *mempool_get_imp(struct mempool_imp *mp)
{
    int rc;
    QUEUE* iter;
    struct mempool_slice *slice;
    
    if (!mp) {
        return NULL;
    }

    rc = pthread_mutex_lock(&mp->lck);
    if (rc != 0) {
        return NULL;
    }

    if (QUEUE_EMPTY(&mp->q_idle)){
        pthread_mutex_unlock(&mp->lck);
        return NULL;
    }

    iter = QUEUE_HEAD(&mp->q_idle);
	QUEUE_REMOVE(iter);
	QUEUE_INIT(iter);
    QUEUE_INSERT_TAIL(&mp->q_used, iter);
    mp->used_cnt++;
    slice = QUEUE_DATA(iter, struct mempool_slice, q);
    pthread_mutex_unlock(&mp->lck);
    
    if (!slice) {
        return NULL;
    }

    return slice->data;
}

void mempool_put_imp(struct mempool_imp *mp, void *ele)
{
    int rc;
    QUEUE* iter;
    struct mempool_slice *slice;

    if (!mp || !ele) {
        return;
    }

    slice = (struct mempool_slice*)(((char*)ele - sizeof(struct mempool_slice)));
    if (slice->magic != MEMPOOL_MAGIC) {
        return;
    }

    rc = pthread_mutex_lock(&mp->lck);
    if (rc != 0) {
        return;
    }
    
	QUEUE_REMOVE(&slice->q);
	QUEUE_INIT(&slice->q);
    QUEUE_INSERT_TAIL(&mp->q_idle, &slice->q);
    mp->used_cnt--;
    pthread_mutex_unlock(&mp->lck);
    return;
}

size_t mempool_use_count_imp(struct mempool_imp *mp)
{
    
    if (!mp) {
        return 0;
    }
    return mp->count;
}

size_t mempool_avail_count_imp(struct mempool_imp *mp)
{
    if (!mp) {
        return 0;
    }
    return (mp->count - mp->used_cnt);
}
