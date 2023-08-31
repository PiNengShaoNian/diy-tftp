#include "tftp_server.h"

#include <pthread.h>
#include <stdio.h>

static const char *server_path;
static uint16_t server_port;
static void *tftp_server_thread(void *arg) {
  printf("tftp server is running...\n");
  return NULL;
}

int tftpd_start(const char *dir, uint16_t port) {
  pthread_t server_thread;
  server_path = dir;
  server_port = port ? port : TFTP_DEF_PORT;
  int err = pthread_create(&server_thread, NULL, tftp_server_thread, NULL);
  if (err != 0) {
    printf("tftpd: create server thread failed.\n");
    return -1;
  }

  return 0;
}