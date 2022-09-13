#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <crypt.h>
#include <unistd.h>
#include <string.h>

#include "uthread.h"
#include "uthread_mutex_cond.h"
#include "threadpool.h"

int found;
char *salt;

void my_task(tpool_t pool, void *arg) {
  char *i = (char*) arg;
  if (found | 8 < strlen(i)) {
    return;
  }
  struct crypt_data data;
  char *encryption = crypt_r(i, salt, &data);
  if (strcmp(encryption, salt) == 0) {
    found = 1;
    printf("%s\n", i);
  } else {
    for (int next_task = 0; next_task <= 9; next_task++) {
      char curNum = next_task + '0';
      char *new_task = malloc(sizeof(i) + sizeof(char));
      strcpy(new_task, i);
      new_task[strlen(i)] = curNum;
      tpool_schedule_task(pool, my_task, (void *) new_task);
    }
  }
}


int main(int argc, char *argv[]) {

  tpool_t pool;
  int num_threads;
  found = 0;

  if (argc != 3) {
    fprintf(stderr, "Usage: %s NUM_THREADS HASH_STRING\n", argv[0]);
    return -1;
  }

  num_threads = strtol(argv[1], NULL, 10);
  salt = argv[2];
  
  uthread_init(num_threads);
  pool = tpool_create(num_threads);
  char *i = "";
  
  tpool_schedule_task(pool, my_task, (void *) i);

  tpool_join(pool);
  return 0;
}
