#ifndef _PTHREAD_H
#define _PTHREAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <time.h>
#include <sched.h>

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL

#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED 1

#define PTHREAD_BARRIER_SERIAL_THREAD -1

typedef unsigned long pthread_t;

typedef struct {
    int detachstate;
    size_t stacksize;
    void *stackaddr;
} pthread_attr_t;

typedef struct {
    volatile int lock;
    int type;
    volatile int owner;
    volatile int count;
} pthread_mutex_t;

typedef struct {
    int type;
    int pshared;
} pthread_mutexattr_t;

typedef struct {
    volatile int seq;
    volatile int waiters;
} pthread_cond_t;

typedef struct {
    int pshared;
} pthread_condattr_t;

typedef struct {
    volatile int lock;
    volatile int writers;
    volatile int readers;
    volatile int writer_tid;
} pthread_rwlock_t;

typedef struct {
    int pshared;
} pthread_rwlockattr_t;

typedef volatile int pthread_spinlock_t;

typedef struct {
    volatile int count;
    volatile int total;
    volatile int cycle;
} pthread_barrier_t;

typedef struct {
    int pshared;
} pthread_barrierattr_t;

typedef unsigned int pthread_key_t;
typedef int pthread_once_t;

#define PTHREAD_MUTEX_INITIALIZER {0, PTHREAD_MUTEX_NORMAL, 0, 0}
#define PTHREAD_COND_INITIALIZER {0, 0}
#define PTHREAD_RWLOCK_INITIALIZER {0, 0, 0, 0}
#define PTHREAD_ONCE_INIT 0

/* Thread Management */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);
void pthread_exit(void *retval);
int pthread_join(pthread_t thread, void **retval);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);
int pthread_yield(void);
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));

/* Attributes */
int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate);

/* Mutexes */
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abstime);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type);

/* Condition Variables */
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);

int pthread_condattr_init(pthread_condattr_t *attr);
int pthread_condattr_destroy(pthread_condattr_t *attr);

/* Read-Write Locks */
int pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr);
int pthread_rwlock_destroy(pthread_rwlock_t *rwlock);
int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_unlock(pthread_rwlock_t *rwlock);

int pthread_rwlockattr_init(pthread_rwlockattr_t *attr);
int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr);

/* Spinlocks */
int pthread_spin_init(pthread_spinlock_t *lock, int pshared);
int pthread_spin_destroy(pthread_spinlock_t *lock);
int pthread_spin_lock(pthread_spinlock_t *lock);
int pthread_spin_trylock(pthread_spinlock_t *lock);
int pthread_spin_unlock(pthread_spinlock_t *lock);

/* Barriers */
int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned int count);
int pthread_barrier_destroy(pthread_barrier_t *barrier);
int pthread_barrier_wait(pthread_barrier_t *barrier);

int pthread_barrierattr_init(pthread_barrierattr_t *attr);
int pthread_barrierattr_destroy(pthread_barrierattr_t *attr);

/* Thread-Specific Data */
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);

#define PTHREAD_CANCEL_ENABLE 0
#define PTHREAD_CANCEL_DISABLE 1
#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#define PTHREAD_CANCELED ((void *)-1)

int pthread_setcancelstate(int state, int *oldstate);
int pthread_setcanceltype(int type, int *oldtype);
int pthread_cancel(pthread_t thread);
void pthread_testcancel(void);

#include <signal.h>
int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset);

#ifdef __cplusplus
}
#endif

#endif /* _PTHREAD_H */
