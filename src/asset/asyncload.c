/*
#   clove
#
#   Copyright (C) 2016-2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include "../include/asyncload.h"

#include <stdlib.h>
#include <string.h>

#include "../3rdparty/SDL2/include/SDL.h"

#include "../include/svg.h"
#include "../include/system.h"
#include "../include/utils.h"

#define ASYNCLOAD_MAX_WORKERS 8

typedef struct {
    int               id;        /* 0 means the slot is free */
    char             *path;      /* owned: the caller's string may not outlive the job */
    image_ImageData  *data;      /* the decoded pixels, once ready */
    asyncload_Status  status;
    bool              claimed;   /* a worker has taken it off the queue */
} Job;

static struct {
    bool          started;
    SDL_Thread   *workers[ASYNCLOAD_MAX_WORKERS];
    int           workerCount;

    SDL_mutex    *lock;
    SDL_sem      *work;          /* posted once per queued job, and once per worker at shutdown */
    SDL_atomic_t  quit;

    Job          *jobs;
    int           jobCount;
    int           jobCapacity;
    int           nextId;
} moduleData;

/* Called with the lock held. */
static Job *findJob(int id) {
    if (id <= 0) {
        return NULL;
    }
    for (int i = 0; i < moduleData.jobCount; i++) {
        if (moduleData.jobs[i].id == id) {
            return &moduleData.jobs[i];
        }
    }
    return NULL;
}

/* Called with the lock held. */
static Job *nextUnclaimed(void) {
    for (int i = 0; i < moduleData.jobCount; i++) {
        Job *j = &moduleData.jobs[i];
        if (j->id != 0 && !j->claimed && j->status == asyncload_Status_pending) {
            return j;
        }
    }
    return NULL;
}

static void freeJob(Job *job) {
    free(job->path);
    if (job->data) {
        image_ImageData_free(job->data);
        free(job->data);
    }
    job->id = 0;
    job->path = NULL;
    job->data = NULL;
    job->claimed = false;
    job->status = asyncload_Status_unknown;
}

static int worker_main(void *unused) {
    (void) unused;

    for (;;) {
        SDL_SemWait(moduleData.work);

        if (SDL_AtomicGet(&moduleData.quit)) {
            return 0;
        }

        SDL_LockMutex(moduleData.lock);
        Job *job = nextUnclaimed();
        int id = 0;
        char *path = NULL;
        if (job) {
            job->claimed = true;
            id = job->id;
            /* The job's own copy could be freed by a shutdown while this
             * worker is decoding, so the worker takes its own. */
            path = malloc(strlen(job->path) + 1);
            if (path) {
                strcpy(path, job->path);
            }
        }
        SDL_UnlockMutex(moduleData.lock);

        /* `job` may have been moved by a realloc in requestImage() the moment
         * the lock was released, so only the id it yielded is safe to use. */
        if (id == 0 || !path) {
            free(path);
            continue;
        }

        /* The slow part, and the only part that runs here. */
        image_ImageData *data = malloc(sizeof(image_ImageData));
        bool ok = false;
        if (data) {
            image_ImageData_new_with_filename(data, path);
            ok = data->surface != NULL;
            if (ok) {
                /* new_with_filename() borrows the caller's pointer as the
                 * path, and this worker's copy is about to go away -- so the
                 * ImageData takes ownership of it instead. */
                data->path = path;
                data->ownsPath = true;
                path = NULL;
            }
        }

        SDL_LockMutex(moduleData.lock);
        Job *again = findJob(id);   /* it may have been dropped by a shutdown */
        if (again) {
            if (ok) {
                again->data = data;
                again->status = asyncload_Status_ready;
            } else {
                again->status = asyncload_Status_failed;
                if (data) {
                    free(data);
                }
            }
        } else if (data) {
            if (ok) {
                image_ImageData_free(data);
            }
            free(data);
        }
        SDL_UnlockMutex(moduleData.lock);

        free(path);
    }
}

void asyncload_init(int workers) {
    if (moduleData.started) {
        return;
    }

    if (workers <= 0) {
        /* One fewer than the machine has, so the frame still gets a core. */
        int cpus = system_getProcessorCount();
        workers = cpus > 2 ? (cpus - 1) : 1;
    }
    if (workers > ASYNCLOAD_MAX_WORKERS) {
        workers = ASYNCLOAD_MAX_WORKERS;
    }

    moduleData.lock = SDL_CreateMutex();
    moduleData.work = SDL_CreateSemaphore(0);
    if (!moduleData.lock || !moduleData.work) {
        clove_error("async loader: could not create its lock\n");
        return;
    }

    SDL_AtomicSet(&moduleData.quit, 0);
    moduleData.nextId = 1;
    moduleData.jobs = NULL;
    moduleData.jobCount = 0;
    moduleData.jobCapacity = 0;

    for (int i = 0; i < workers; i++) {
        moduleData.workers[i] = SDL_CreateThread(worker_main, "clove-asset", NULL);
        if (!moduleData.workers[i]) {
            break;
        }
        moduleData.workerCount++;
    }

    moduleData.started = true;
}

void asyncload_shutdown(void) {
    if (!moduleData.started) {
        return;
    }

    SDL_AtomicSet(&moduleData.quit, 1);
    for (int i = 0; i < moduleData.workerCount; i++) {
        SDL_SemPost(moduleData.work);
    }
    for (int i = 0; i < moduleData.workerCount; i++) {
        SDL_WaitThread(moduleData.workers[i], NULL);
    }

    /* Anything nobody collected is ours to free. */
    for (int i = 0; i < moduleData.jobCount; i++) {
        if (moduleData.jobs[i].id != 0) {
            freeJob(&moduleData.jobs[i]);
        }
    }
    free(moduleData.jobs);

    SDL_DestroySemaphore(moduleData.work);
    SDL_DestroyMutex(moduleData.lock);

    memset(&moduleData, 0, sizeof(moduleData));
}

int asyncload_requestImage(char const *path) {
    if (!moduleData.started || path == NULL || path[0] == '\0') {
        return -1;
    }

    /* svg.c keeps one shared rasterizer, so vector art cannot be decoded on a
     * worker -- and at about a millisecond there is nothing to gain. */
    if (svg_isVectorPath(path)) {
        return -1;
    }

    SDL_LockMutex(moduleData.lock);

    Job *slot = NULL;
    for (int i = 0; i < moduleData.jobCount; i++) {
        if (moduleData.jobs[i].id == 0) {
            slot = &moduleData.jobs[i];
            break;
        }
    }

    if (!slot) {
        if (moduleData.jobCount == moduleData.jobCapacity) {
            int cap = moduleData.jobCapacity ? moduleData.jobCapacity * 2 : 16;
            Job *grown = realloc(moduleData.jobs, sizeof(Job) * (size_t) cap);
            if (!grown) {
                SDL_UnlockMutex(moduleData.lock);
                return -1;
            }
            moduleData.jobs = grown;
            moduleData.jobCapacity = cap;
        }
        slot = &moduleData.jobs[moduleData.jobCount++];
    }

    memset(slot, 0, sizeof(*slot));
    slot->path = malloc(strlen(path) + 1);
    if (!slot->path) {
        SDL_UnlockMutex(moduleData.lock);
        return -1;
    }
    strcpy(slot->path, path);

    slot->id = moduleData.nextId++;
    slot->status = asyncload_Status_pending;

    int id = slot->id;
    SDL_UnlockMutex(moduleData.lock);

    SDL_SemPost(moduleData.work);
    return id;
}

asyncload_Status asyncload_status(int id) {
    if (!moduleData.started) {
        return asyncload_Status_unknown;
    }

    SDL_LockMutex(moduleData.lock);
    Job *job = findJob(id);
    asyncload_Status status = job ? job->status : asyncload_Status_unknown;
    SDL_UnlockMutex(moduleData.lock);
    return status;
}

image_ImageData *asyncload_take(int id) {
    if (!moduleData.started) {
        return NULL;
    }

    SDL_LockMutex(moduleData.lock);
    Job *job = findJob(id);
    image_ImageData *data = NULL;

    if (job && job->status == asyncload_Status_ready) {
        data = job->data;
        job->data = NULL;    /* ownership moves out before the slot is freed */
        freeJob(job);
    }

    SDL_UnlockMutex(moduleData.lock);
    return data;
}

int asyncload_pending(void) {
    if (!moduleData.started) {
        return 0;
    }

    SDL_LockMutex(moduleData.lock);
    int n = 0;
    for (int i = 0; i < moduleData.jobCount; i++) {
        if (moduleData.jobs[i].id != 0 && moduleData.jobs[i].status == asyncload_Status_pending) {
            n++;
        }
    }
    SDL_UnlockMutex(moduleData.lock);
    return n;
}

char const *asyncload_path(int id) {
    if (!moduleData.started) {
        return NULL;
    }

    SDL_LockMutex(moduleData.lock);
    Job *job = findJob(id);
    char const *path = job ? job->path : NULL;
    SDL_UnlockMutex(moduleData.lock);
    return path;
}
