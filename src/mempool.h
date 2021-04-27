#ifndef MEM_POOL_H_
#define MEM_POOL_H_

#include <stdio.h>

/* 内存池适配*/
#define MP_HASH_IMP_NAME            "zkc_pool"
#define MP_HASH_POOL_CACHE_SIZE     0 /* cache数量，大小等于前端scsi task的pool cache数量? */

struct mempool_imp;
struct mempool_imp *mempool_create_imp(int id, size_t count, size_t ele_size);
void mempool_free_imp(struct mempool_imp *mp);
void *mempool_get_imp(struct mempool_imp *mp);
void mempool_put_imp(struct mempool_imp *mp, void *ele);
size_t mempool_use_count_imp(struct mempool_imp *mp);
size_t mempool_avail_count_imp(struct mempool_imp *mp);

#endif /* PDN_MEM */
