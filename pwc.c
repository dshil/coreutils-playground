#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "macros.h"

/*
 * TODO: fix deadlock
 */

void* cw(void* a);

struct argset {
    char* fname;
    int bc;  /* bytes counter */
    int nlc; /* new lines counter */
    int wc;  /* words counter */
};

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t flag = PTHREAD_COND_INITIALIZER;
struct argset* mailbox;

int main(int ac, char* av[]) {
    char** fnames = av + 1;
    mailbox = NULL;

    int ret = 0;
    ret = pthread_mutex_lock(&lock);
    if (ret != 0)
        handle_error_en(ret, "pthread_mutex_lock");

    pthread_t thrds[ac - 1];
    struct argset args[ac - 1];

    for (int i = 0; i < ac - 1; i++) {
        args[i].fname = fnames[i];
        args[i].bc = 0;
        args[i].nlc = 0;
        args[i].wc = 0;

        ret = pthread_create(&thrds[i], NULL, cw, (void*)&args[i]);
        if (ret != 0)
            handle_error_en(ret, "pthread_create");
    }

    int msg_num = 0;
    while (msg_num < (ac - 1)) {
        ret = pthread_cond_wait(&flag, &lock);
        if (ret != 0)
            handle_error_en(ret, "pthread_cond_wait");

        printf("%d %d %d %s\n", mailbox->nlc, mailbox->wc, mailbox->bc, mailbox->fname);

        mailbox = NULL;
        msg_num++;

        ret = pthread_cond_signal(&flag);
        if (ret != 0)
            handle_error_en(ret, "pthread_cond_signal");

        ret = pthread_mutex_unlock(&lock);
        if (ret != 0)
            handle_error_en(ret, "pthread_mutex_unlock");
    }

    for (int i = 0; i < ac - 1; i++) {
        ret = pthread_join(thrds[i], NULL);
        if (ret != 0)
            handle_error_en(ret, "pthread_join");
    }

    exit(EXIT_SUCCESS);
}

void* cw(void* a) {
    struct argset* arg = (struct argset*)a;

    FILE* f = fopen(arg->fname, "r");
    if (f == NULL) {
        fprintf(stderr, "fopen(%s): %s\n", arg->fname, strerror(errno));
        return NULL;
    }

    static const int in_word_state = 0;
    static const int out_word_state = 1;

    int state = out_word_state;
    int c = 0;
    for (;;) {
        c = fgetc(f);

        if (ferror(f) != 0) {
            perror("fgetc");
            goto error;
        }
        if (feof(f)) {
            break;
        }

        if (c == '\n')
            arg->nlc++;
        if (c == ' ' || c == '\t' || c == '\n')
            state = out_word_state;
        if (state == out_word_state) {
            state = in_word_state;
            arg->wc++;
        }
        arg->bc++;
    }

    if (fclose(f) == EOF) {
        perror("fclose");
        return NULL;
    }

    int ret = 0;
    ret = pthread_mutex_lock(&lock);
    if (ret != 0) {
        goto error;
    }

    // It's possible that a thread takes a lock too fast and a main thread does
    // not handle value in mailbox that was left by last thread, need to wait
    // until main thread handles this value.
    if (mailbox != NULL) {
        pthread_cond_wait(&flag, &lock);
    }

    mailbox = arg;

    ret = pthread_cond_signal(&flag);
    if (ret != 0) {
        goto error;
    }

    ret = pthread_mutex_unlock(&lock);
    if (ret != 0) {
        goto error;
    }

    return NULL;

error:
    if (f != NULL) {
        if (fclose(f) == EOF) {
            perror("fclose");
        }
    }
    if (ret != 0) {
        fprintf(stderr, "thread failed(%s): %s\n", arg->fname, strerror(ret));
    }
    return NULL;
}
