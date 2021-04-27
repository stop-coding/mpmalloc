
#include "mpmalloc.h"
#include "mpmalloc_hash_imp.h"


#ifndef mp_pri_calloc
#define mp_pri_calloc(N,Z) calloc(N,Z)
#endif
#ifndef mp_pri_free
#define mp_pri_free(P) free(P)
#endif

#define MP_LOG_ERROR(format, arg...) printf("ERROR [%s,%d]:  "format"\n", __FUNCTION__, __LINE__, ##arg) 
#define MP_LOG_DEBUG(format, arg...)

struct mp_handle {
    int method_id;     /* 方法id，对应g_methods数组的偏移值 */
    void *method_imp;  /* 内存管理方法句柄 */
};

/*内存分配算法实现的回调函数*/
typedef void *(*mp_create_fn)(const struct mp_unit *arr, int arr_num);
typedef void *(*mp_alloc_fn)(void * mh, size_t  size);
typedef void (*mp_free_fn)(void * mh, void *mem);
typedef void (*mp_destroy_fn)(void * mh);


struct mp_method
{
    mp_method_t name;
    mp_create_fn create;
    mp_alloc_fn alloc;
    mp_free_fn free;
    mp_destroy_fn destroy;
};

static const struct mp_method g_methods[] = 
{
    {MP_METHOD_E_DEFAULT,
    mp_hash_create_imp,
    mp_hash_alloc_imp,
    mp_hash_free_imp,
    mp_hash_destroy_imp
    },             /* default*/
};

struct mp_handle* mp_create(const struct mp_unit *arr, int arr_num, mp_method_t m)
{
    struct mp_handle* mh;

    if (!arr) {
        MP_LOG_ERROR("arr is null.");
        return NULL;
    }

    if (arr_num <= 0) {
        MP_LOG_ERROR("type num of param invalid, [%d].", arr_num);
        return NULL;
    }

    if (m < MP_METHOD_E_DEFAULT || m > MP_METHOD_E_MAX) {
        MP_LOG_ERROR("method[%d] of param invalid.", m);
        return NULL;
    }

    if ( (int) m >= (int)(sizeof(g_methods) / sizeof(struct mp_method) )) {
        MP_LOG_ERROR("method[%d] of param invalid.", m);
        return NULL;
    }

    mh = mp_pri_calloc(1, sizeof(struct mp_handle));
    if (!mh) {
        MP_LOG_ERROR("mp_pri_calloc fail.");
        return NULL;
    }
    mh->method_id = m;
    mh->method_imp = g_methods[mh->method_id].create(arr, arr_num);
    if (!mh->method_imp) {
        MP_LOG_ERROR("create methods object fail.");
        goto fail;
    }

    return mh;
fail:
    mp_destroy(mh);
    return NULL;
}

void mp_destroy(struct mp_handle* mh)
{
    if (mh) {
        if ( mh->method_id >= (int)(sizeof(g_methods) / sizeof(struct mp_method))) {
            MP_LOG_ERROR("method[%d] of param invalid.", mh->method_id);
            return;
        }
        if (mh->method_imp) {
            g_methods[mh->method_id].destroy(mh->method_imp);
        }
        mp_pri_free(mh);
    }
}

void *mp_malloc(struct mp_handle* mh, size_t size)
{
    void *ptr;
    if (!mh) {
        MP_LOG_ERROR("mh null.");
        return NULL;
    }

    if (size <= 0) {
        MP_LOG_DEBUG("size[%ld] invalid.", size);
        return NULL;
    }
    ptr =  g_methods[mh->method_id].alloc(mh->method_imp, size);
    return ptr;
}
void *mp_calloc(struct mp_handle* mh, size_t nitems, size_t size)
{
    void *ptr;

    ptr = mp_malloc(mh, nitems * size);
    if (!ptr) {
        MP_LOG_DEBUG("mp_malloc fail.");
        return NULL;
    }
    memset(ptr, 0, nitems * size);
    return ptr;
}
void *mp_realloc(struct mp_handle* mh, void *p, size_t size)
{
    if (p) {
        mp_free(mh, p);
    }
    return mp_malloc(mh, size);
}

void mp_free(struct mp_handle* mh, void *p)
{
    
    if (!p || !mh) {
        MP_LOG_DEBUG("mh[%p] invalid, %p.", mh, p);
        return;
    }
    g_methods[mh->method_id].free(mh->method_imp, p);
}
