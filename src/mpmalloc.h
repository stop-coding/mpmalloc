
/**
 * \brief 池化的内存分配算法
 *      用于预分配的内存大小基本能确定的场合，采用内存池方式减小碎片化问题；
 *      需要自行适配内存池和读写锁、普通内存分配实现
 *      依赖klibc的khash头文件，处理标准C库，无其它库依赖
 *      无法从内存池里获取时，则使用普通方式（默认glibc）获取内存
 *  注意：
 *      1.该方法最高效率时预分配场景基本确定，否则性能将下降；
 *      2.每次分配内存额外增加4字节元数据，对内存容量敏感的业务场景慎用；
 *      3.实测试性能，表明随机分配性能低于glibc...
 * 
 */
#ifndef MPMALLOC_H_
#define MPMALLOC_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MP_OK                   0
#define MP_ERR                  -1
#define MP_MEM_TYPE_NAME        "pdn_mp_mem"


typedef enum _mp_method{
    MP_METHOD_E_DEFAULT = 0,/* 默认基于内存池和哈希表的分配方法 */
    MP_METHOD_E_MAX,
}mp_method_t;

struct mp_unit{
    size_t size;            /* 分配单元的size 类型 */
    int    capacity;        /* 该size单元的内存池容量初始值 */
};

struct mp_handle;
/**
 * \brief 创建一个内存管理实例.
 *
 * \param arr 内存分配单元数组，用于描述业务场景所需要的内存分配大小的信息
 * \param arr_num 数组元素个数
 * \param m 实现方法类型，默认MP_METHOD_E_DEFAULT，用于场景拓展
 * \return 返回内存管理句柄，失败则为NULL
 */

struct mp_handle* mp_create(const struct mp_unit *arr, int arr_num, mp_method_t m);
/**
 * \brief 销毁一个内存管理实例.
 *  注意：由于尽可能使用无锁设计，释放时，业务自己需要确保申请的内存都已经归还，否则在执行删除时，并发free会dump
 * \param mh 内存分配单元数组，用于描述业务场景所需要的内存分配信息
 */
void mp_destroy(struct mp_handle* mh); 

void *mp_malloc(struct mp_handle* mh, size_t size);
void *mp_calloc(struct mp_handle* mh, size_t nitems, size_t size);
void *mp_realloc(struct mp_handle* mh, void *p, size_t size);
void mp_free(struct mp_handle* mh, void *p);

#ifdef __cplusplus
}
#endif

#endif
