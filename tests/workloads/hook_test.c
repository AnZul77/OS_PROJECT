
#define _GNU_SOURCE

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

static pthread_mutex_t test_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *thread_work(void *arg)
{
    (void)arg;

    pthread_mutex_lock(&test_mutex);
    printf("Thread acquired mutex\n");
    pthread_mutex_unlock(&test_mutex);

    return NULL;
}

int main(void)
{
    printf("Crash Archaeologist hook test starting\n");

    /* Memory allocation and release */
    char *memory = malloc(64);
    if (memory != NULL) {
        strcpy(memory, "Testing memory hooks");
        memory = realloc(memory, 128);
        free(memory);
    }

    /* Thread creation, mutex operations, and joining */
    pthread_t thread;

    if (pthread_create(&thread, NULL, thread_work, NULL) == 0) {
        pthread_join(thread, NULL);
    }

    /* File operations */
    int pipe_fds[2];

    if (pipe(pipe_fds) == 0) {
        const char message[] = "pipe test";
        char received[32] = {0};

        if (write(pipe_fds[1], message, sizeof(message)) > 0) {
            (void)read(pipe_fds[0], received, sizeof(received));
            printf("Pipe received: %s\n", received);
        }

        close(pipe_fds[0]);
        close(pipe_fds[1]);
    }

    /* Socket send and receive */
    int sockets[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0) {
        const char message[] = "socket test";
        char received[32] = {0};

        if (send(sockets[0], message, sizeof(message), 0) > 0) {
            (void)recv(sockets[1], received, sizeof(received), 0);
            printf("Socket received: %s\n", received);
        }

        close(sockets[0]);
        close(sockets[1]);
    }

    pthread_mutex_destroy(&test_mutex);

    printf("Crash Archaeologist hook test finished\n");
    return 0;
}
