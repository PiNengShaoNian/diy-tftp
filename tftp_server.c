#include "tftp_server.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *server_path;
static uint16_t server_port;
static tftp_t tftp;

static void *tftp_server_thread(void *arg) {
  printf("tftp server is running...\n");

  int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sockfd < 0) {
    printf("tftpd: create server socket failed.\n");
    return NULL;
  }

  struct sockaddr_in sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sin_addr.s_addr = INADDR_ANY;
  sockaddr.sin_port = htons(server_port);
  if (bind(sockfd, (const struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0) {
    printf("tftpd: bind error, port: %d\n", server_port);
    close(sockfd);
    return NULL;
  }

  tftp.socket = sockfd;
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