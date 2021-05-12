
#ifndef MPMALLOC_HASH_IMP_H_
#define MPMALLOC_HASH_IMP_H_

#ifdef __cplusplus
extern "C" {
#endif

struct mp_unit;
void *mp_hash_create_imp(const struct mp_unit *arr, int arr_num);
void *mp_hash_alloc_imp(void* mh, size_t size);
void *mp_hash_realloc_imp(void* mh, void *mem, size_t newsize);
void mp_hash_free_imp(void* mh, void *mem);
void mp_hash_destroy_imp(void* mh);

#ifdef __cplusplus
}
#endif

#endif
