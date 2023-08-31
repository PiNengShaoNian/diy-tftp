#include "tftp_server.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *server_path;
static uint16_t server_port;
static tftp_t tftp;

static int wait_req(tftp_t *tftp, tftp_req_t *req) {
  tftp_packet_t *pkt = &tftp->rx_packet;
  size_t pkt_size;
  int err = tftp_wait_packet(tftp, TFTP_PKT_REQ, 0, &pkt_size);

  req->op = ntohs(pkt->opcode);
  req->option = 0;
  req->blksize = TFTP_DEF_BLKSIZE;
  req->filesize = 0;
  memset(req->filename, 0, sizeof(req->filename));
  memset(&req->tftp, 0, sizeof(req->tftp));
  memcpy(&req->tftp.remote, &tftp->remote, sizeof(tftp->remote));

  struct sockaddr_in *addr = (struct sockaddr_in *)&tftp->remote;
  printf("tftp: recv req %s from %s %d\n",
         req->op == TFTP_PKT_RRQ ? "get" : "put", inet_ntoa(addr->sin_addr),
         ntohs(addr->sin_port));

  char *buf = (char *)pkt->req.args;
  char *end = (char *)pkt + pkt_size;
  strncpy(req->filename, buf, sizeof(req->filename));
  buf += strlen(req->filename) + 1;

  if (strcmp(buf, "octet") != 0) {
    tftp_send_error(tftp, TFTP_ERR_OP);
    printf("tftp: unknown transfer mode %s\n", buf);
    return -1;
  }

  buf += strlen("octet") + 1;

  while ((buf < end) && (*buf)) {
    req->option = 1;
    if (strcmp(buf, "blksize") == 0) {
      buf += strlen("blksize") + 1;
      int blksize = atoi(buf);

      if (blksize <= 0) {
        tftp_send_error(tftp, TFTP_ERR_OP);
        return -1;
      } else if (blksize > TFTP_BLK_SIZE) {
        printf("blk size %d too long, set to %d\n", blksize, TFTP_DEF_BLKSIZE);
        blksize = TFTP_DEF_BLKSIZE;
      }

      req->blksize = blksize;
      buf += strlen(buf) + 1;
    } else if (strcmp(buf, "tsize") == 0) {
      buf += strlen(buf) + 1;
      req->filesize = atoi(buf);
      if (req->filesize <= 0) {
        tftp_send_error(tftp, TFTP_ERR_OP);
        return -1;
      }
      buf += strlen(buf) + 1;
    } else {
      buf += strlen(buf) + 1;
    }
  }
}

static void *tftp_working_thread(void *arg) {
  tftp_req_t *req = (tftp_req_t *)arg;
  tftp_t *tftp = &req->tftp;

  int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sockfd < 0) {
    printf("tftp: create working socket failed.\n");
    goto init_error;
  }

  tftp->socket = sockfd;
  tftp->tmo_retry = TFTP_MAX_RETRY;
  tftp->tmo_sec = TFTP_TMO_SEC;
  tftp->file_size = req->filesize;
  tftp->block_size = req->blksize;

  if (req->op == TFTP_PKT_WRQ) {
  } else {
  }

  struct timeval tmo;
  tmo.tv_sec = tftp->tmo_sec;
  tmo.tv_usec = 0;
  setsockopt(tftp->socket, SOL_SOCKET, SO_RCVTIMEO, (const void *)&tmo,
             sizeof(tmo));

init_error:
  if (sockfd >= 0) {
    close(sockfd);
  }
  free(req);
  return NULL;
}

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

  while (1) {
    tftp_req_t *req = (tftp_req_t *)malloc(sizeof(tftp_req_t));
    if (req == NULL) {
      continue;
    }

    int err = wait_req(&tftp, req);
    if (err < 0) {
      free(req);
      continue;
    }

    pthread_t thread;
    err = pthread_create(&thread, NULL, tftp_working_thread, req);
    if (err != 0) {
      printf("tftpd: create working thread failed.\n");
      free(req);
      continue;
    }
  }
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