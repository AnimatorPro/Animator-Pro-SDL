/*******************************************************************************
 * poco_lock.c - mutex creation and acquisition for the two platforms poco
 * builds on.
 ******************************************************************************/

#include "poco_lock.h"

#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION Poco_lock;
#else
#include <pthread.h>
typedef pthread_mutex_t Poco_lock;
#endif

void* po_lock_create(void)
{
	Poco_lock* lock = malloc(sizeof(*lock));

	if (lock == NULL) {
		return NULL;
	}
#ifdef _WIN32
	InitializeCriticalSection(lock);
#else
	if (pthread_mutex_init(lock, NULL) != 0) {
		free(lock);
		return NULL;
	}
#endif
	return lock;
}

void po_lock_destroy(void* opaque_lock)
{
	Poco_lock* lock = opaque_lock;

	if (lock == NULL) {
		return;
	}
#ifdef _WIN32
	DeleteCriticalSection(lock);
#else
	pthread_mutex_destroy(lock);
#endif
	free(lock);
}

void po_lock_acquire(void* opaque_lock)
{
	Poco_lock* lock = opaque_lock;

	if (lock == NULL) {
		return;
	}
#ifdef _WIN32
	EnterCriticalSection(lock);
#else
	pthread_mutex_lock(lock);
#endif
}

void po_lock_release(void* opaque_lock)
{
	Poco_lock* lock = opaque_lock;

	if (lock == NULL) {
		return;
	}
#ifdef _WIN32
	LeaveCriticalSection(lock);
#else
	pthread_mutex_unlock(lock);
#endif
}
