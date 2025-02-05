#include <pthread.h>

/* Let's see if the host has semaphore.h */
#include <unistd.h>

#ifdef _POSIX_SEMAPHORES
#include <semaphore.h>
/* TODO(pscollins): We don't support fork() for now, but maybe one day
 * we will? */
#define SHARE_SEM 0
#endif /* _POSIX_SEMAPHORES */


struct lkl_mutex {
	pthread_mutex_t mutex;
};

struct lkl_sem {
#ifdef _POSIX_SEMAPHORES
	sem_t sem;
#else
	pthread_mutex_t lock;
	int count;
	pthread_cond_t cond;
#endif /* _POSIX_SEMAPHORES */
};

struct lkl_tls_key {
	pthread_key_t key;
};

#define WARN_UNLESS(exp) do {						\
		if (exp < 0)						\
			lkl_printf("%s: %s\n", #exp, strerror(errno));	\
	} while (0)

static int _warn_pthread(int ret, char *str_exp)
{
	if (ret > 0)
		lkl_printf("%s: %s\n", str_exp, strerror(ret));

	return ret;
}


/* pthread_* functions use the reverse convention */
#define WARN_PTHREAD(exp) _warn_pthread(exp, #exp)

static struct lkl_sem *sem_alloc(int count)
{
	struct lkl_sem *sem;

	sem = malloc(sizeof(*sem));
	if (!sem)
		return NULL;

#ifdef _POSIX_SEMAPHORES
	if (sem_init(&sem->sem, SHARE_SEM, count) < 0) {
		lkl_printf("sem_init: %s\n", strerror(errno));
		free(sem);
		return NULL;
	}
#else
	pthread_mutex_init(&sem->lock, NULL);
	sem->count = count;
	WARN_PTHREAD(pthread_cond_init(&sem->cond, NULL));
#endif /* _POSIX_SEMAPHORES */

	return sem;
}

static void sem_free(struct lkl_sem *sem)
{
#ifdef _POSIX_SEMAPHORES
	WARN_UNLESS(sem_destroy(&sem->sem));
#else
	WARN_PTHREAD(pthread_cond_destroy(&sem->cond));
	WARN_PTHREAD(pthread_mutex_destroy(&sem->lock));
#endif /* _POSIX_SEMAPHORES */
	free(sem);
}

static void sem_up(struct lkl_sem *sem)
{
#ifdef _POSIX_SEMAPHORES
	WARN_UNLESS(sem_post(&sem->sem));
#else
	WARN_PTHREAD(pthread_mutex_lock(&sem->lock));
	sem->count++;
	if (sem->count > 0)
		WARN_PTHREAD(pthread_cond_signal(&sem->cond));
	WARN_PTHREAD(pthread_mutex_unlock(&sem->lock));
#endif /* _POSIX_SEMAPHORES */

}

static void sem_down(struct lkl_sem *sem)
{
#ifdef _POSIX_SEMAPHORES
	int err;

	do {
		err = sem_wait(&sem->sem);
	} while (err < 0 && errno == EINTR);
	if (err < 0 && errno != EINTR)
		lkl_printf("sem_wait: %s\n", strerror(errno));
#else
	WARN_PTHREAD(pthread_mutex_lock(&sem->lock));
	while (sem->count <= 0)
		WARN_PTHREAD(pthread_cond_wait(&sem->cond, &sem->lock));
	sem->count--;
	WARN_PTHREAD(pthread_mutex_unlock(&sem->lock));
#endif /* _POSIX_SEMAPHORES */
}

static struct lkl_mutex *mutex_alloc(int recursive)
{
	struct lkl_mutex *_mutex = malloc(sizeof(struct lkl_mutex));
	pthread_mutex_t *mutex = NULL;
	pthread_mutexattr_t attr;

	if (!_mutex)
		return NULL;

	mutex = &_mutex->mutex;
	WARN_PTHREAD(pthread_mutexattr_init(&attr));

	/* PTHREAD_MUTEX_ERRORCHECK is *very* useful for debugging,
	 * but has some overhead, so we provide an option to turn it
	 * off. */
#ifdef DEBUG
	if (!recursive)
		WARN_PTHREAD(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
#endif /* DEBUG */

	if (recursive)
		WARN_PTHREAD(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE));

	WARN_PTHREAD(pthread_mutex_init(mutex, &attr));

	return _mutex;
}

static void mutex_lock(struct lkl_mutex *mutex)
{
	WARN_PTHREAD(pthread_mutex_lock(&mutex->mutex));
}

static void mutex_unlock(struct lkl_mutex *_mutex)
{
	pthread_mutex_t *mutex = &_mutex->mutex;
	WARN_PTHREAD(pthread_mutex_unlock(mutex));
}

static void mutex_free(struct lkl_mutex *_mutex)
{
	pthread_mutex_t *mutex = &_mutex->mutex;
	WARN_PTHREAD(pthread_mutex_destroy(mutex));
	free(_mutex);
}

static lkl_thread_t thread_create(void (*fn)(void *), void *arg)
{
	pthread_t thread;
	if (WARN_PTHREAD(pthread_create(&thread, NULL, (void* (*)(void *))fn, arg)))
		return 0;
	else
		return (lkl_thread_t) thread;
}

static void thread_detach(void)
{
	WARN_PTHREAD(pthread_detach(pthread_self()));
}

static void thread_exit(void)
{
	pthread_exit(NULL);
}

static int thread_join(lkl_thread_t tid)
{
	if (WARN_PTHREAD(pthread_join((pthread_t)tid, NULL)))
		return -1;
	else
		return 0;
}

static lkl_thread_t thread_self(void)
{
	return (lkl_thread_t)pthread_self();
}

static int thread_equal(lkl_thread_t a, lkl_thread_t b)
{
	return pthread_equal((pthread_t)a, (pthread_t)b);
}

static struct lkl_tls_key *tls_alloc(void (*destructor)(void *))
{
	struct lkl_tls_key *ret = malloc(sizeof(struct lkl_tls_key));

	if (WARN_PTHREAD(pthread_key_create(&ret->key, destructor))) {
		free(ret);
		return NULL;
	}
	return ret;
}

static void tls_free(struct lkl_tls_key *key)
{
	WARN_PTHREAD(pthread_key_delete(key->key));
	free(key);
}

static int tls_set(struct lkl_tls_key *key, void *data)
{
	if (WARN_PTHREAD(pthread_setspecific(key->key, data)))
		return -1;
	return 0;
}

static void *tls_get(struct lkl_tls_key *key)
{
	return pthread_getspecific(key->key);
}

static long _gettid(void)
{
#ifdef	__FreeBSD__
	return (long)pthread_self();
#else
	return syscall(SYS_gettid);
#endif
}
