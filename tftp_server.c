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

static int do_send_file(tftp_req_t *req) {
  tftp_t *tftp = &req->tftp;

  char path_buf[256];
  if (server_path) {
    snprintf(path_buf, sizeof(path_buf), "%s/%s", server_path, req->filename);
  } else {
    snprintf(path_buf, sizeof(path_buf), "%s", req->filename);
  }

  FILE *file = fopen(path_buf, "rb");
  if (file == NULL) {
    printf("tftpd: file %s does not exist\n", path_buf);
    tftp_send_error(tftp, TFTP_ERR_NO_FILE);
    return -1;
  }

  printf("tftpd: sending file %s...\n", path_buf);

  fseek(file, 0, SEEK_END);
  long filesize = ftell(file);
  fseek(file, 0, SEEK_SET);
  tftp->file_size = filesize;

  if (req->option) {
    int err = tftp_send_oack(tftp);
    if (err < 0) {
      printf("tftpd: send oack failed.\n");
      goto send_failed;
    }

    size_t pkt_size;
    err = tftp_wait_packet(tftp, TFTP_PKT_ACK, 0, &pkt_size);
    if (err < 0) {
      printf("tftp: wait ack failed.\n");
      goto send_failed;
    }
  }

  uint16_t curr_blk = 1;
  int total_size = 0;
  int total_block = 0;
  while (1) {
    size_t size = fread(tftp->tx_packet.data.data, 1, tftp->block_size, file);
    if (size < 0) {
      printf("tftpd: read file %s failed.\n", path_buf);
      tftp_send_error(tftp, TFTP_ERR_ACC_VIO);
      goto send_failed;
    }

    int err = tftp_send_data(tftp, curr_blk, size);
    if (err < 0) {
      printf("tftp: send data block failed.\n");
      goto send_failed;
    }

    size_t pkt_size;
    err = tftp_wait_packet(tftp, TFTP_PKT_ACK, curr_blk++, &pkt_size);
    if (err < 0) {
      printf("tftp: wait ack failed.\n");
      goto send_failed;
    }

    total_size += (int)size;
    total_block++;

    if (size < tftp->block_size) {
      break;
    }
  }

  printf("tftpd: send %s %d bytes %d blocks\n", path_buf, total_size,
         total_block);
  fclose(file);
  return 0;
send_failed:
  printf("tftpd: send failed\n");
  fclose(file);
  return -1;
}

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

  struct timeval tmo;
  tmo.tv_sec = tftp->tmo_sec;
  tmo.tv_usec = 0;
  setsockopt(tftp->socket, SOL_SOCKET, SO_RCVTIMEO, (const void *)&tmo,
             sizeof(tmo));

  if (req->op == TFTP_PKT_WRQ) {
  } else {
    do_send_file(req);
  }

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