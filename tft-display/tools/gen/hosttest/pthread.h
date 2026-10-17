/*
 * PC 自检用的 pthread 垫片（**不进固件**）。
 *
 * wsdata.c 里有 pthread_mutex_lock/unlock（真机上当然用真的），但 Windows 目标的
 * zig cc 不带 pthread.h ⇒ 把这个目录加到 -I 上顶掉它：
 *
 *   zig cc -I tools/gen/hosttest -I src/utils -o %TEMP%\modeparse.exe \
 *       tools/gen/modeparse_test.c src/utils/wsdata.c src/utils/tinyjson.c
 *
 * 单线程自检用空实现就够。
 */
#ifndef TFT_HOSTTEST_PTHREAD_H
#define TFT_HOSTTEST_PTHREAD_H

typedef int pthread_mutex_t;
typedef int pthread_t;

#define PTHREAD_MUTEX_INITIALIZER 0

static inline int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_init(pthread_mutex_t *m, const void *a) { (void)m; (void)a; return 0; }
static inline int pthread_create(pthread_t *t, const void *a, void *(*f)(void *), void *x)
{
    (void)t; (void)a; (void)f; (void)x;
    return 0;
}
static inline int pthread_join(pthread_t t, void **r) { (void)t; (void)r; return 0; }

#endif /* TFT_HOSTTEST_PTHREAD_H */
