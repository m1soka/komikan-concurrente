/* Inyección controlada de errores para probar el desenrollado de recursos.
 * Solo se carga explícitamente en las pruebas mediante LD_PRELOAD. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int (*create_fn)(pthread_t *, const pthread_attr_t *,
                         void *(*)(void *), void *);

static _Atomic unsigned long calls;
static pthread_once_t resolve_once = PTHREAD_ONCE_INIT;
static create_fn real_create;
typedef int (*mutex_init_fn)(pthread_mutex_t *, const pthread_mutexattr_t *);
static _Atomic unsigned long mutex_calls;
static pthread_once_t mutex_resolve_once = PTHREAD_ONCE_INIT;
static mutex_init_fn real_mutex_init;
typedef FILE *(*fdopen_fn)(int, const char *);
static _Atomic unsigned long fdopen_calls;
static pthread_once_t fdopen_resolve_once = PTHREAD_ONCE_INIT;
static fdopen_fn real_fdopen;

static int selected(const char *name, unsigned long current)
{
    const char *value = getenv(name);
    return value != NULL && strtoul(value, NULL, 10) == current;
}

/* La barrera es exclusivamente de pruebas. Una FIFO permite que el padre
 * envíe SIGINT en una fase conocida antes de liberar la operación interceptada.
 * No se retienen mutex de la aplicación y no se usa espera activa. */
static void gate(const char *selector, unsigned long current,
                 const char *marker, size_t marker_length)
{
    if (!selected(selector, current)) return;
    const char *path = getenv("KOMIKAN_GATE_FIFO");
    if (path == NULL) return;
    (void)write(STDERR_FILENO, marker, marker_length);
    int descriptor;
    do {
        descriptor = open(path, O_RDONLY);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor >= 0) {
        char released;
        ssize_t result;
        do {
            result = read(descriptor, &released, 1);
        } while (result < 0 && errno == EINTR);
        (void)close(descriptor);
    }
}

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
    unsigned long current = atomic_fetch_add(&calls, 1UL) + 1UL;
    static const char marker[] = "PRUEBA gate=pthread_create\n";
    gate("KOMIKAN_GATE_CREATE_AT", current, marker, sizeof(marker) - 1);
    if (selected("KOMIKAN_FAIL_CREATE_AT", current)) {
        return EAGAIN;
    }
    if (pthread_once(&resolve_once, resolve_create) != 0 || real_create == NULL) {
        return EAGAIN;
    }
    return real_create(thread, attr, routine, arg);
}

static void resolve_mutex_init(void)
{
    void *symbol = dlsym(RTLD_NEXT, "pthread_mutex_init");
    _Static_assert(sizeof(real_mutex_init) == sizeof(symbol),
                   "La ABI POSIX necesita punteros compatibles");
    memcpy(&real_mutex_init, &symbol, sizeof(real_mutex_init));
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    unsigned long current = atomic_fetch_add(&mutex_calls, 1UL) + 1UL;
    static const char marker[] = "PRUEBA gate=pthread_mutex_init\n";
    gate("KOMIKAN_GATE_MUTEX_AT", current, marker, sizeof(marker) - 1);
    if (selected("KOMIKAN_FAIL_MUTEX_AT", current)) return EAGAIN;
    if (pthread_once(&mutex_resolve_once, resolve_mutex_init) != 0 ||
        real_mutex_init == NULL) return EAGAIN;
    return real_mutex_init(mutex, attr);
}

static void resolve_fdopen(void)
{
    void *symbol = dlsym(RTLD_NEXT, "fdopen");
    _Static_assert(sizeof(real_fdopen) == sizeof(symbol),
                   "La ABI POSIX necesita punteros compatibles");
    memcpy(&real_fdopen, &symbol, sizeof(real_fdopen));
}

FILE *fdopen(int descriptor, const char *mode)
{
    unsigned long current = atomic_fetch_add(&fdopen_calls, 1UL) + 1UL;
    static const char marker[] = "PRUEBA gate=fdopen\n";
    gate("KOMIKAN_GATE_FDOPEN_AT", current, marker, sizeof(marker) - 1);
    if (pthread_once(&fdopen_resolve_once, resolve_fdopen) != 0 || real_fdopen == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    return real_fdopen(descriptor, mode);
}
