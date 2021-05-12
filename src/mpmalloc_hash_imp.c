#include "mpmalloc.h"
#include "mpmalloc_hash_imp.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <pthread.h>

#include "mempool.h"

#define MP_HASH_ASSERT   assert
#define MP_LOG_ERROR(format, arg...) printf("ERROR [%s,%d]:  "format"\n", __FUNCTION__, __LINE__, ##arg)
#define MP_LOG_WARN(format, arg...) printf("WARN [%s,%d]:  "format"\n", __FUNCTION__, __LINE__, ##arg)
//#define MP_LOG_DEBUG(format, arg...) printf("DEBUG [%s,%d]:  "format"\n", __FUNCTION__, __LINE__, ##arg)
#define MP_LOG_DEBUG(format, arg...)

#ifndef mp_hash_calloc
#define mp_hash_calloc(N,Z) calloc(N,Z)
#endif
#ifndef mp_hash_malloc
#define mp_hash_malloc(Z) malloc(Z)
#endif
#ifndef mp_hash_realloc
#define mp_hash_realloc(P,Z) realloc(P,Z)
#endif
#ifndef mp_hash_free
#define mp_hash_free(P) free(P)
#endif

#define kcalloc(N,Z) mp_hash_calloc(N,Z)
#define kmalloc(Z) mp_hash_malloc(Z)
#define krealloc(P,Z) mp_hash_realloc(P,Z)
#define kfree(P) mp_hash_free(P)

#include "khash.h"


/* 内存单元编解码*/
#define PACKED_MEMORY(__declaration__) \
		__declaration__ __attribute__((__packed__))

#define MP_UNIT_MAGIC  (0xa5)

struct mp_hash_slice
{
    int             node_id;
    int             mempool_id;
    void            *mempool_ptr;
    void            *alloc_mem;
};

PACKED_MEMORY(struct mp_mem_head
{
    unsigned char node_id;
    unsigned char mempool_id;
    unsigned char reserved;
    unsigned char magic;
});

static  inline void *mp_pack(const struct mp_hash_slice *slice)
{
    ((struct mp_mem_head *)slice->alloc_mem)->node_id = (unsigned char)slice->node_id;
    ((struct mp_mem_head *)slice->alloc_mem)->mempool_id = (unsigned char)slice->mempool_id;
    ((struct mp_mem_head *)slice->alloc_mem)->magic = MP_UNIT_MAGIC;
    return (char*)slice->alloc_mem + sizeof(struct mp_mem_head);
}

static  inline struct mp_mem_head *mp_unpack(char *mem, struct mp_hash_slice *slice)
{
    slice->alloc_mem = (struct mp_mem_head *)(mem - sizeof(struct mp_mem_head));
    if (((struct mp_mem_head *)slice->alloc_mem)->magic ^ MP_UNIT_MAGIC) {
        abort();
        return NULL;
    }
    slice->node_id = ((struct mp_mem_head *)slice->alloc_mem)->node_id;
    slice->mempool_id = ((struct mp_mem_head *)slice->alloc_mem)->mempool_id;
    return slice->alloc_mem;
}

/*默认使用哈希算法查找内存池*/

/* 读写锁适配 */
typedef pthread_rwlock_t  mp_rwlock_t;

static inline int mp_rwlock_init(mp_rwlock_t *rwlock)
{
    return pthread_rwlock_init(rwlock, NULL);
}
static inline int mp_rwlock_rdlock(mp_rwlock_t *rwlock)
{
    return pthread_rwlock_rdlock(rwlock);
}
static inline int mp_rwlock_wrlock(mp_rwlock_t *rwlock)
{
    return pthread_rwlock_wrlock(rwlock);
}
static inline int mp_rwlock_unlock(mp_rwlock_t *rwlock)
{
    return pthread_rwlock_unlock(rwlock);
}
static inline int mp_rwlock_destroy(mp_rwlock_t *rwlock)
{
    return pthread_rwlock_destroy(rwlock);
}

/* 内存池适配*/
#define MP_HASH_IMP_NAME            "zkc_pool"
#define MP_HASH_POOL_CACHE_SIZE     0 /* cache数量，大小等于前端scsi task的pool cache数量? */

typedef struct mempool_imp  mp_mempool_t;

static inline mp_mempool_t *mp_hash_mempool_create_imp(int id, size_t count, size_t ele_size)
{
    return mempool_create_imp(id, count, ele_size);
}

static inline void mp_hash_mempool_free_imp(mp_mempool_t *mp)
{
    mempool_free_imp(mp);
}

static inline void *mp_hash_mempool_get_imp(mp_mempool_t *mp)
{
    return mempool_get_imp(mp);
}

static inline void mp_hash_mempool_put_imp(mp_mempool_t *mp, void *ele)
{
    mempool_put_imp(mp, ele);
}

static inline size_t mp_hash_mempool_use_count_imp(mp_mempool_t *mp)
{
    return mempool_use_count_imp(mp);
}

static inline size_t mp_hash_mempool_avail_count_imp(mp_mempool_t *mp)
{
    return mempool_avail_count_imp(mp);
}


/*内存分配实现方法*/

/*size类型最大数量 不超过254种类型，再多类型，不适合这种方式了*/
#define MP_HASH_SIZE_TYPE_MAX_NUM           64

/*每个size类型，支持动态拓展的最大内存池个数, 不超过254*/
#define MP_HASH_MAX_MEMPOOL_NUM             4

/*每个size类型，固定内存池个数，声明周期里不会被释放*/
#define MP_HASH_MAX_ACTIVE_MEMPOOL_NUM      1

/* 哈希表的NODE最小值，小于该值，则不需要哈希表查找*/
#define MP_HASH_NODE_MIN_NUM                128

#define MP_HASH_INVALID_MEMPOOL_ID          (MP_HASH_MAX_MEMPOOL_NUM + 1)

#define MP_HAHS_INVALID_NODE_ID             (MP_HASH_SIZE_TYPE_MAX_NUM + 1)

/*每个存储池默认元素最大个数*/
#define MP_HASH_MEMPOOL_CAPACITY            512


/* 结构体定义 */

struct mp_hash_mempool
{
    size_t          capacity;
    mp_mempool_t    *handle;
};

struct mp_hash_node
{
    int                     id;
    size_t                  size;
    size_t                  init_capacity;
    mp_rwlock_t             mempools_rwlock;
    struct mp_hash_mempool  *mempools;
    unsigned char           mempool_max_num;
    unsigned char           mempool_active;
    char                    padding[2];
};

KHASH_MAP_INIT_INT(hash_32, struct mp_hash_node*)

struct mp_hash_imp
{
    khash_t(hash_32) *h;
    int node_num;
    struct mp_hash_node *nodes;
};

/* 函数声明 */
static int mp_hash_node_init(struct mp_hash_node *node, size_t size, int capacity);
static void mp_hash_node_finish(struct mp_hash_node *node);

static int mp_hash_node_get_slice(struct mp_hash_node *node, struct mp_hash_slice *slice);
static void mp_hash_node_put_slice(struct mp_hash_node *node, const struct mp_hash_slice *slice);

static int mp_hash_any_alloc_imp(size_t alloc_size, struct mp_hash_slice *slice);
static void mp_hash_any_realloc_imp(size_t new_size, struct mp_hash_slice *slice);
static void mp_hash_any_free_imp(const struct mp_hash_slice *slice);

static void mp_hash_sort(struct mp_hash_node *nodes, int nodes_num);
static int mp_hash_mem_skip_search(struct mp_hash_node *nodes, int nodes_num, size_t key);

void *mp_hash_create_imp(const struct mp_unit *arr, int arr_num)
{
    int i;
    int rc;
    khiter_t k;
    int ret;
    size_t min_mem_size = 0;
    size_t max_mem_size = 0;
    struct mp_hash_imp *imp;

    if (!arr) {
        MP_LOG_ERROR("null ptr.");
        return NULL;
    }

    imp = mp_hash_calloc(1, sizeof(struct mp_hash_imp));
    if (!imp) {
        MP_LOG_ERROR("calloc fail.");
        return NULL;
    }

    imp->node_num = arr_num;
    imp->nodes = mp_hash_calloc(1, imp->node_num * sizeof(struct mp_hash_node));
    if (!imp->nodes) {
        MP_LOG_ERROR("calloc nodes fail.");
        goto fail;
    }

    if (imp->node_num <= MP_HASH_NODE_MIN_NUM) {
        imp->h = kh_init(hash_32);
        if (!imp->h) {
            MP_LOG_ERROR("kh_init fail.");
            goto fail;
        }
    } else {
        imp->h = NULL;
    }

    for (i = 0; i < imp->node_num; i++) {
        if (arr[i].size <= 0) {
            MP_LOG_ERROR("unit[%d] size[%ld] is invalid.", i, arr[i].size);
            goto fail;
        }
        rc = mp_hash_node_init(&imp->nodes[i], arr[i].size, arr[i].capacity);
        if (rc != MP_OK) {
            MP_LOG_ERROR("mp_hash_node_init fail.");
            goto fail;
        }
        min_mem_size += (arr[i].size *arr[i].capacity);
        max_mem_size += (arr[i].size *arr[i].capacity * imp->nodes[i].mempool_max_num);
        imp->nodes[i].id = i;
        if (!imp->h) {
            continue;
        }
        k = kh_put(hash_32, imp->h, imp->nodes[i].size, &ret);
        if (ret < 0) {
            MP_LOG_ERROR("kh_put fail, key[%ld], ret[%d].", imp->nodes[i].size, ret);
            goto fail;
        }
        MP_LOG_DEBUG("hash table[%p] map node[%d] on key[%lu].", imp->h, i, imp->nodes[i].size);
        kh_value(imp->h, k) = &(imp->nodes[i]);
    }
    mp_hash_sort(imp->nodes, imp->node_num); // 排个序
    MP_LOG_DEBUG("Register mempool size: Min [%luKB],  Max [%luKB].", min_mem_size/1024 + 1, max_mem_size/1024 + 1);
    return imp;
fail:
    mp_hash_destroy_imp(imp);
    return NULL;
}

void mp_hash_destroy_imp(void* mh)
{
    int i;
    struct mp_hash_imp *imp;

    if (!mh) {
        MP_LOG_ERROR("null ptr.");
        return;
    }

    imp = (struct mp_hash_imp *)mh;
    if (imp) {
        if (imp->h) {
            kh_destroy(hash_32, imp->h);
        }
        if (imp->nodes) {
            for (i = 0; i < imp->node_num; i++) {
                mp_hash_node_finish(&imp->nodes[i]);
            }
            mp_hash_free(imp->nodes);
        }
        mp_hash_free(imp);
    }
    return;
}

void *mp_hash_alloc_imp(void* mh, size_t size)
{
    int rc;
    int find_index;
    khiter_t k;
    struct mp_hash_imp *imp;
    struct mp_hash_node *node;
    struct mp_hash_slice slice = {0};
    size_t total_size;

    if (!mh) {
        MP_LOG_ERROR("null ptr.");
        return NULL;
    }

    total_size = size + sizeof(struct mp_mem_head);
    imp = (struct mp_hash_imp *)mh;
    node = NULL;
    if (imp->h) {
         k = kh_get(hash_32, imp->h, total_size);
         if (k != kh_end(imp->h)) {
             node = kh_value(imp->h, k);
         }
    }

    if (!node) {
        find_index = mp_hash_mem_skip_search(imp->nodes, imp->node_num, total_size);
        if (imp->nodes[find_index].size >= total_size) {
            node = &imp->nodes[find_index];
        }
    }

    if (node){
        rc = mp_hash_node_get_slice(node, &slice);
    }

    /*池分配失败，则尝试直接分配*/
    if (!slice.alloc_mem) {
        rc = mp_hash_any_alloc_imp(total_size, &slice);
        if (rc != MP_OK || !slice.alloc_mem) {
            MP_LOG_ERROR("get mem slice fail, size[%ld].", size);
            return NULL;
        }
    }

    MP_LOG_DEBUG("alloc ptr[%p] node_id[%d],mempool_id[%d].", slice.alloc_mem, slice.node_id, slice.mempool_id);
    return mp_pack(&slice);
}

void *mp_hash_realloc_imp(void* mh, void *mem, size_t newsize)
{
    struct mp_hash_imp *imp;
    size_t total_size;
    struct mp_hash_slice slice = {};
    struct mp_mem_head *mem_head;
    void *new_mem;

    if (!mh) {
        MP_LOG_ERROR("null ptr.");
        return NULL;
    }

    if (!mem) {
        return mp_hash_alloc_imp(mh, newsize);
    }

    if (newsize == 0) {
        mp_hash_free_imp(mh, mem);
        return NULL;
    }

    mem_head = mp_unpack((char *)mem, &slice);
    if (!mem_head) {
        MP_LOG_ERROR("mp_unpack ptr[%p] fail, maybe not valid memery for mp.", mem);
        return NULL;
    }
    imp = (struct mp_hash_imp *)mh;
    total_size = newsize + sizeof(struct mp_mem_head);
    if (slice.node_id < imp->node_num) {
        if (total_size <= imp->nodes[slice.node_id].size) {
            return mem;
        } 
        new_mem = mp_hash_alloc_imp(mh, total_size);
        if (!new_mem) {
            return NULL;
        }
        /* 拷贝数据 */
        memcpy(new_mem, mem, imp->nodes[slice.node_id].size - sizeof(struct mp_mem_head));
        /* 新内存分配成功 需要释放旧的*/
        mp_hash_node_put_slice(&imp->nodes[slice.node_id], &slice);
        return new_mem;
    } else {
        mp_hash_any_realloc_imp(total_size, &slice);
        if (!slice.alloc_mem) {
            return NULL;
        }
        return mp_pack(&slice);
    } 
}


void mp_hash_free_imp(void* mh, void *mem)
{
    struct mp_hash_imp *imp;
    struct mp_mem_head *mem_head;
    struct mp_hash_slice slice = {};

    if (!mh || !mem) {
        MP_LOG_ERROR("null ptr.");
        return;
    }
    imp = (struct mp_hash_imp *)mh;
    mem_head = mp_unpack((char *)mem, &slice);
    if (!mem_head) {
        MP_LOG_ERROR("mp_unpack ptr[%p] fail, maybe not valid memery for mp.", mem);
        return;
    }

    MP_LOG_DEBUG("free ptr[%p] node_id[%d],mempool_id[%d].", slice.alloc_mem, slice.node_id, slice.mempool_id);
    if (slice.node_id != MP_HAHS_INVALID_NODE_ID && slice.node_id < imp->node_num) {
        mp_hash_node_put_slice(&imp->nodes[slice.node_id], &slice);
    }else{
        /* 非hash表node，则采用独立方法实现 */
        mp_hash_any_free_imp(&slice);
    }
    
    return;
}

static void mp_hash_node_finish(struct mp_hash_node *node)
{
    int i;
    int rc;

    /* 内部接口，避免重复校验，入参由调用者校验 */
    if (!node->mempools || node->size) {
        return;
    }
    rc = mp_rwlock_wrlock(&node->mempools_rwlock);
    if (rc != MP_OK) {
        MP_LOG_ERROR("mp_rwlock_wrlock fail");
        return;
    }
    for (i = 0; i < node->mempool_max_num; i++) {
        if (node->mempools[i].handle) {
            mp_hash_mempool_free_imp(node->mempools[i].handle);
            node->mempools[i].handle = NULL;
        }
    }
    mp_rwlock_unlock(&node->mempools_rwlock);
    rc = mp_rwlock_destroy(&node->mempools_rwlock);

    if (rc != MP_OK) {
        MP_LOG_ERROR("mp_rwlock_destroy fail");
        return;
    }

    mp_hash_free(node->mempools);
    node->mempools = NULL;
    node->size = 0;
}

static int mp_hash_node_init(struct mp_hash_node *node, size_t size, int capacity)
{
    int i;
    int rc;

    /* 内部接口，避免重复校验，入参由调用者校验 */

    rc = mp_rwlock_init(&node->mempools_rwlock);
    if (rc != MP_OK) {
        MP_LOG_ERROR("mp_rwlock_init fail");
        return MP_ERR;
    }
    node->size = sizeof(struct mp_mem_head) + size; /* 增加元数据头 */
    node->mempool_max_num = MP_HASH_MAX_MEMPOOL_NUM;
    node->mempool_active = MP_HASH_MAX_ACTIVE_MEMPOOL_NUM; /* 默认只启用一个池 */
    node->mempools = mp_hash_calloc(1, node->mempool_max_num * sizeof(struct mp_hash_mempool));
    if (!node->mempools) {
        MP_LOG_ERROR("calloc mempools fail");
        return MP_ERR;
    }
    node->init_capacity = (capacity > 0) ? capacity: MP_HASH_MEMPOOL_CAPACITY;
    /* 这里只申请一个内存池，后续按需要拓展 */
    for (i = 0; i < node->mempool_active; i++) {
        node->mempools[i].capacity = node->init_capacity;
        node->mempools[i].handle = mp_hash_mempool_create_imp(i, node->mempools[i].capacity, node->size);
        if (!node->mempools[i].handle) {
            MP_LOG_ERROR("p_mempool_create fail, pool capacity[%ld], size[%ld].", 
                        node->mempools[i].capacity, node->size);
            goto fail;
        }
    }
    
    return MP_OK;
fail:
    mp_hash_node_finish(node);
    return MP_ERR;
}

static int mp_hash_node_get_slice(struct mp_hash_node *node, struct mp_hash_slice *slice)
{
    int i;
    int rc;

    /* 内部接口，避免重复校验，入参由调用者校验 */

    slice->node_id = node->id;
    /* 为了避免锁性能，这里固定内存池是不会删减，只有这些内存池不足，才使用动态内存池 */
    for (i = 0; i < MP_HASH_MAX_ACTIVE_MEMPOOL_NUM; i++) {
        MP_HASH_ASSERT(node->mempools[i].handle != NULL);
        slice->alloc_mem = mp_hash_mempool_get_imp(node->mempools[i].handle);
        if (slice->alloc_mem) {
            slice->mempool_id = i;
            slice->mempool_ptr = node->mempools[i].handle;
            break;
        }
    }
    
    if (slice->alloc_mem) {
        return MP_OK;
    }

    /* 这里查找动态部分 读锁 性能影响还好*/
    rc = mp_rwlock_rdlock(&node->mempools_rwlock);
    if (rc != MP_OK) {
        MP_LOG_ERROR("mp_rwlock_rdlock fail");
        return MP_ERR;
    }

    for (i = MP_HASH_MAX_ACTIVE_MEMPOOL_NUM ; i < node->mempool_active; i++) {
        if (!node->mempools[i].handle) {
            continue;
        }
        slice->alloc_mem = mp_hash_mempool_get_imp(node->mempools[i].handle);
        if (slice->alloc_mem) {
            slice->mempool_id = i;
            slice->mempool_ptr = node->mempools[i].handle;
            break;
        }
    }
    mp_rwlock_unlock(&node->mempools_rwlock);

    if (slice->alloc_mem) {
        return MP_OK;
    }

    /* 这里需要考虑加写锁 需要动态拓展 性能影响较大*/
    rc = mp_rwlock_wrlock(&node->mempools_rwlock);
    if (rc != MP_OK) {
        MP_LOG_ERROR("mp_rwlock_wrlock fail");
        return MP_ERR;
    }

    for (i = MP_HASH_MAX_ACTIVE_MEMPOOL_NUM; i < node->mempool_max_num; i++) {
        if (node->mempools[i].handle) {
            continue;
        }
        node->mempools[i].capacity = (node->mempool_active + 1) * node->init_capacity;
        node->mempools[i].handle = mp_hash_mempool_create_imp(i, node->mempools[i].capacity, node->size);
        if (!node->mempools[i].handle) {
            MP_LOG_ERROR("mempool[%d] create fail, pool addr:%p", i, node->mempools[i].handle);
            break;
        }
        node->mempool_active++;
        slice->alloc_mem = mp_hash_mempool_get_imp(node->mempools[i].handle);
        if (slice->alloc_mem) {
            slice->mempool_id = i;
            slice->mempool_ptr = node->mempools[i].handle;
            MP_LOG_WARN("increase mempool id[%d], mempool_active[%d], mempool addr[%p], size[%lu]", 
                        slice->mempool_id, (int)node->mempool_active, node->mempools[i].handle, node->size);
        } else {
            MP_LOG_ERROR("mempool[%d] get fail, pool addr:%p", i, node->mempools[i].handle);
        }
        break;
    }
    mp_rwlock_unlock(&node->mempools_rwlock);

    return slice->alloc_mem ? MP_OK: MP_ERR;
}

static void mp_hash_node_put_slice(struct mp_hash_node *node, const struct mp_hash_slice *slice)
{
    int rc;
    int i;
    size_t left_capacity = 0;

    /* 内部接口，避免重复校验，入参由调用者校验 */
    if (!slice->alloc_mem) {
        MP_LOG_ERROR("alloc_mem is null");
        return;
    }

    if (slice->mempool_id >= node->mempool_max_num) {
        MP_LOG_ERROR("mempool_id[%d] is invalid, maybe this mem[%p] over write", slice->mempool_id, slice->alloc_mem);
        return;
    }
    MP_HASH_ASSERT(node->mempools[slice->mempool_id].handle != NULL);

    if (slice->mempool_id < MP_HASH_MAX_ACTIVE_MEMPOOL_NUM) {
        mp_hash_mempool_put_imp(node->mempools[slice->mempool_id].handle, slice->alloc_mem);
        return;
    }

    /* 这里需要加锁 */
    rc = mp_rwlock_rdlock(&node->mempools_rwlock);
    if (rc != MP_OK) {
        MP_LOG_ERROR("mp_rwlock_rdlock fail");
        return;
    }
    mp_hash_mempool_put_imp(node->mempools[slice->mempool_id].handle, slice->alloc_mem);

    /* 缩减内存池 */
    if (mp_hash_mempool_use_count_imp(node->mempools[slice->mempool_id].handle) == 0) {
        for (i = 0 ; i < node->mempool_active; i++) {
            if (i == slice->mempool_id) {
                continue;
            }
            if (!node->mempools[i].handle) {
                continue;
            }
            left_capacity += mp_hash_mempool_avail_count_imp(node->mempools[i].handle);
        }
        MP_LOG_WARN("node[%d] total mempools avail capacity: %ld.", node->id, left_capacity);
        MP_LOG_WARN("mempool_id[%d] capacity: %ld", slice->mempool_id, node->mempools[slice->mempool_id].capacity);
        if (left_capacity > node->mempools[slice->mempool_id].capacity/4) {
            mp_rwlock_unlock(&node->mempools_rwlock);
            mp_rwlock_wrlock(&node->mempools_rwlock);
            if (node->mempools[slice->mempool_id].handle) {
                mp_hash_mempool_free_imp(node->mempools[slice->mempool_id].handle);
                node->mempools[slice->mempool_id].capacity = 0;
                node->mempool_active--;
                MP_LOG_WARN("decrease mempool id[%d], mempool active[%d], mempool addr [%p]", 
                            slice->mempool_id, (int)node->mempool_active, node->mempools[slice->mempool_id].handle);
                node->mempools[slice->mempool_id].handle = NULL;
            }
        }
    }
    mp_rwlock_unlock(&node->mempools_rwlock);
    return;
}

static inline void mp_hash_any_realloc_imp(size_t new_size, struct mp_hash_slice *slice)
{
    /* 内部接口，避免重复校验，入参由调用者校验 */
    if (!slice->alloc_mem) {
        MP_LOG_ERROR("free memery null");
    }
    slice->alloc_mem =  mp_hash_realloc(slice->alloc_mem, new_size);
}

static inline int mp_hash_any_alloc_imp(size_t alloc_size, struct mp_hash_slice *slice)
{
    /* 内部接口，避免重复校验，入参由调用者校验 */
    slice->alloc_mem = mp_hash_malloc(alloc_size);
    MP_LOG_DEBUG("alloc memery [%p] by (default malloc)", slice->alloc_mem);
    slice->mempool_id = MP_HASH_INVALID_MEMPOOL_ID;
    slice->mempool_ptr = NULL;
    slice->node_id = MP_HAHS_INVALID_NODE_ID;
    if (!slice->alloc_mem) {
        MP_LOG_ERROR("mp_hash_malloc memery fail, size[%ld]", alloc_size);
        return MP_ERR;
    }
    return MP_OK;
}

static inline void mp_hash_any_free_imp(const struct mp_hash_slice *slice)
{
    /* 内部接口，避免重复校验，入参由调用者校验 */
    MP_HASH_ASSERT(slice->mempool_id == MP_HASH_INVALID_MEMPOOL_ID);
    if (slice->alloc_mem) {
        MP_LOG_DEBUG("free memery [%p] by (default free)", slice->alloc_mem);
        mp_hash_free(slice->alloc_mem);
    }
    return;
}

static inline void mp_hash_swap(struct mp_hash_node *a, struct mp_hash_node *b)
{
    struct mp_hash_node tmp;

    tmp = *a;
    *a = *b;
    a->id = tmp.id;
    tmp.id = b->id;
	*b = tmp;
}

static void mp_hash_sort(struct mp_hash_node *nodes, int nodes_num)
{
	int i, j;
    int flag = 0;

    /* 内部接口，避免重复校验，入参由调用者校验 */
    for (i = 0; i < nodes_num - 1; i++) {
        flag = 0;
		for (j = 0; j < nodes_num - i - 1; j++){
			if(nodes[j].size > nodes[j + 1].size){
                mp_hash_swap(&nodes[j], &nodes[j + 1]);
				flag = 1;
			}
        }
        if (flag == 0)
            break;
    }
}

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

static int mp_hash_mem_skip_search(struct mp_hash_node *nodes, int nodes_num, size_t key)
{
	int low = 0;
	int high = nodes_num - 1;
    int index = high;

    /* 内部接口，避免重复校验，入参由调用者校验 */
    MP_LOG_DEBUG("start low[%d] high[%d] index[%d], key[%lu]", low, high, index, key);
	while (low <= high) {
		if (key <= nodes[low].size) {
            return low;
        } else if(key > nodes[high].size) {
            return index;
        }else{
            if (mp_hash_find_mid(nodes, key, &high, &low, &index) != 0) {
                break;
            }
        }
	}
	return index;
}