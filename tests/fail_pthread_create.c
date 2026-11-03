/* Inyección controlada de errores para probar el desenrollado de recursos.
 * Solo se carga explícitamente en las pruebas mediante LD_PRELOAD. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef int (*create_fn)(pthread_t *, const pthread_attr_t *,
                         void *(*)(void *), void *);

static _Atomic unsigned long calls;
static pthread_once_t resolve_once = PTHREAD_ONCE_INIT;
static create_fn real_create;

static void resolve_create(void)
{
    void *symbol = dlsym(RTLD_NEXT, "pthread_create");
    _Static_assert(sizeof(real_create) == sizeof(symbol),
                   "La ABI POSIX necesita punteros compatibles");
    memcpy(&real_create, &symbol, sizeof(real_create));
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*routine)(void *), void *arg)
{
    const char *value = getenv("KOMIKAN_FAIL_CREATE_AT");
    unsigned long current = atomic_fetch_add(&calls, 1UL) + 1UL;
    if (value != NULL && strtoul(value, NULL, 10) == current) {
        return EAGAIN;
    }
    if (pthread_once(&resolve_once, resolve_create) != 0 || real_create == NULL) {
        return EAGAIN;
    }
    return real_create(thread, attr, routine, arg);
}
