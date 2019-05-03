#include <pth.h>

#include <unistd.h>

struct lkl_mutex {
	pth_mutex_t mutex;
};

struct lkl_sem {
	pth_mutex_t lock;
	int count;
	pth_cond_t cond;
};

struct lkl_tls_key {
	pth_key_t key;
};

static int _warn_pth(int ret, char *str_exp)
{
	if (!ret)
		lkl_printf("%s: %s\n", str_exp, strerror(errno));

        return ret;
}

#define WARN_PTH(exp) _warn_pth(exp, #exp)

static struct lkl_sem *sem_alloc(int count)
{
	struct lkl_sem *sem;

	sem = malloc(sizeof(*sem));
	if (!sem)
		return NULL;

	WARN_PTH(pth_mutex_init(&sem->lock));
	sem->count = count;
	WARN_PTH(pth_cond_init(&sem->cond));

	return sem;
}

static void sem_free(struct lkl_sem *sem)
{
    // Pth does not have functions for freeing mutexes and conditionals
    free(sem);
}

static void sem_up(struct lkl_sem *sem)
{
	WARN_PTH(pth_mutex_acquire(&sem->lock, 0, NULL));
	sem->count++;
	if (sem->count > 0)
		WARN_PTH(pth_cond_notify(&sem->cond, 0));
	WARN_PTH(pth_mutex_release(&sem->lock));
}

static void sem_down(struct lkl_sem *sem)
{
	WARN_PTH(pth_mutex_acquire(&sem->lock, 0, NULL));
	while (sem->count <= 0)
		WARN_PTH(pth_cond_await(&sem->cond, &sem->lock, NULL));
	sem->count--;
	WARN_PTH(pth_mutex_release(&sem->lock));
}

static struct lkl_mutex *mutex_alloc(int recursive)
{
	struct lkl_mutex *_mutex = malloc(sizeof(struct lkl_mutex));
	if (!_mutex)
		return NULL;

        // Pth mutexes are always recursive
	WARN_PTH(pth_mutex_init(&_mutex->mutex));

	return _mutex;
}

static void mutex_lock(struct lkl_mutex *mutex)
{
	WARN_PTH(pth_mutex_acquire(&mutex->mutex, 0, NULL));
}

static void mutex_unlock(struct lkl_mutex *mutex)
{
	WARN_PTH(pth_mutex_release(&mutex->mutex));
}

static void mutex_free(struct lkl_mutex *_mutex)
{
    // Pth does not have functions for freeing mutexes and conditionals
    free(_mutex);
}

static lkl_thread_t thread_create(void (*fn)(void *), void *arg)
{
	return pth_spawn(PTH_ATTR_DEFAULT, (void* (*)(void *))fn, arg);
}

static void thread_detach(void)
{
  // TODO
//	WARN_PTH(pthread_detach(pthread_self()));
}

static void thread_exit(void)
{
	pth_exit(NULL);
}

static int thread_join(lkl_thread_t tid)
{
	if (WARN_PTH(pth_join((pth_t)tid, NULL)))
		return 0;
	else
		return -1;
}

static lkl_thread_t thread_self(void)
{
	return (lkl_thread_t)pth_self();
}

static int thread_equal(lkl_thread_t a, lkl_thread_t b)
{
	return a == b;
}

static struct lkl_tls_key *tls_alloc(void (*destructor)(void *))
{
	struct lkl_tls_key *ret = malloc(sizeof(struct lkl_tls_key));

	if (WARN_PTH(pth_key_create(&ret->key, destructor))) {
          return ret;
	}
	free(ret);
	return NULL;
}

static void tls_free(struct lkl_tls_key *key)
{
	WARN_PTH(pth_key_delete(key->key));
	free(key);
}

static int tls_set(struct lkl_tls_key *key, void *data)
{
	if (WARN_PTH(pth_key_setdata(key->key, data)))
		return 0;
	return -1;
}

static void *tls_get(struct lkl_tls_key *key)
{
	return pth_key_getdata(key->key);
}

static long _gettid(void)
{
	return (long)pth_self();
}
