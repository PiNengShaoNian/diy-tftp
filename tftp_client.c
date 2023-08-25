#include "tftp_client.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static tftp_t tftp;

static int tftp_open(const char *ip, uint16_t port) {
  int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sockfd < 0) {
    printf("error: create socket failed.\n");
    return -1;
  }

  tftp.socket = sockfd;

  struct sockaddr_in *sockaddr = (struct sockaddr_in *)(&tftp.remote);
  memset(sockaddr, 0, sizeof(struct sockaddr_in));
  sockaddr->sin_family = AF_INET;
  sockaddr->sin_addr.s_addr = inet_addr(ip);
  sockaddr->sin_port = htons(port);
}

static void tftp_close() { close(tftp.socket); }

static int do_tftp_get(int block_size, const char *ip, uint16_t port,
                       const char *filename) {
  if (tftp_open(ip, port) < 0) {
    printf("tftp connect failed.\n");
    return -1;
  }

  tftp_send_request(&tftp, 1, filename, 0);

  tftp_close();
  return 0;
}

int tftp_get(const char *ip, uint16_t port, int block_size,
             const char *filename) {
  printf("try to get file %s from %s\n", filename, ip);

  if (block_size > TFTP_BLK_SIZE) {
    block_size = TFTP_BLK_SIZE;
  }

  return do_tftp_get(block_size, ip, port, filename);
}