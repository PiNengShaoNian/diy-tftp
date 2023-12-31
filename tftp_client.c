#include "tftp_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static tftp_t tftp;

static int tftp_open(const char *ip, uint16_t port, int block_size) {
  int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sockfd < 0) {
    printf("error: create socket failed.\n");
    return -1;
  }

  tftp.socket = sockfd;
  tftp.block_size = block_size;
  tftp.file_size = 0;
  tftp.tmo_retry = TFTP_MAX_RETRY;
  tftp.tmo_sec = TFTP_TMO_SEC;

  struct sockaddr_in *sockaddr = (struct sockaddr_in *)(&tftp.remote);
  memset(sockaddr, 0, sizeof(struct sockaddr_in));
  sockaddr->sin_family = AF_INET;
  sockaddr->sin_addr.s_addr = inet_addr(ip);
  sockaddr->sin_port = htons(port);

  struct timeval tmo;
  tmo.tv_sec = tftp.tmo_sec;
  tmo.tv_usec = 0;
  setsockopt(tftp.socket, SOL_SOCKET, SO_RCVTIMEO, (const void *)&tmo,
             sizeof(tmo));
}

static void tftp_close() { close(tftp.socket); }

static int do_tftp_get(int block_size, const char *ip, uint16_t port,
                       const char *filename, int option) {
  if (tftp_open(ip, port, block_size) < 0) {
    printf("tftp connect failed.\n");
    return -1;
  }

  int err = tftp_send_request(&tftp, 1, filename, 0, option);
  if (err < 0) {
    printf("tftp: send tftp request failed.\n");
    goto get_error;
  }

  if (option) {
    size_t recv_size = 0;
    err = tftp_wait_packet(&tftp, TFTP_PKT_OACK, 0, &recv_size);
    if (err < 0) {
      printf("tftp: wait oack error, file %s\n", filename);
      goto get_error;
    }

    err = tftp_send_ack(&tftp, 0);
    if (err < 0) {
      printf("tftp: send ack failed. file: %s\n", filename);
      goto get_error;
    }

    printf("tftp: file size %d bytes\n", tftp.file_size);
  }

  FILE *file = fopen(filename, "wb");
  if (file == NULL) {
    printf("tftp: create local file failed: %s\n", filename);
    goto get_error;
  }

  uint16_t next_block = 1;
  uint32_t total_size = 0;
  uint32_t total_block = 0;
  while (1) {
    size_t recv_size = 0;
    err = tftp_wait_packet(&tftp, TFTP_PKT_DATA, next_block, &recv_size);
    if (err < 0) {
      printf("tftp: wait error, block %d file %s\n", 0, filename);
      goto get_error;
    }

    size_t block_size = recv_size - 4;
    if (block_size) {
      size_t size = fwrite(tftp.rx_packet.data.data, 1, block_size, file);
      if (size < block_size) {
        printf("tftp: write file failed: %s\n", filename);
        goto get_error;
      }
    }

    err = tftp_send_ack(&tftp, next_block);

    if (err < 0) {
      printf("tftp: send ack failed. ack block=%d\n", next_block);
      goto get_error;
    }
    next_block++;

    total_size += (uint32_t)block_size;
    if (++total_block % 0x40 == 0) {
      printf(".");
      fflush(stdout);
    }
    if (block_size < tftp.block_size) {
      err = 0;
      break;
    }
  }

  printf("\n\ttftp: total recv: %d bytes, %d block\n", total_size, total_block);
  fclose(file);
  tftp_close();
  return 0;

get_error:
  if (file) {
    fclose(file);
  }
  tftp_close();
  return -1;
}

int tftp_get(const char *ip, uint16_t port, int block_size,
             const char *filename, int option) {
  printf("try to get file %s from %s\n", filename, ip);

  if (block_size > TFTP_BLK_SIZE) {
    block_size = TFTP_BLK_SIZE;
  }

  return do_tftp_get(block_size, ip, port, filename, option);
}

static int do_tftp_put(int block_size, const char *ip, uint16_t port,
                       const char *filename, int option) {
  if (!option) {
    block_size = TFTP_DEF_BLKSIZE;
  }

  if (tftp_open(ip, port, block_size) < 0) {
    printf("tftp connect failed.\n");
    return -1;
  }

  FILE *file = fopen(filename, "rb");
  if (file == NULL) {
    printf("tftp: create local file failed: %s\n", filename);
    goto put_error;
  }

  printf("tftp: try to put file: %s\n", filename);

  fseek(file, 0, SEEK_END);
  long filesize = ftell(file);
  fseek(file, 0, SEEK_SET);

  int err = tftp_send_request(&tftp, 0, filename, filesize, option);
  if (err < 0) {
    printf("tftp: send tftp request failed.\n");
    goto put_error;
  }

  size_t recv_size;
  err = tftp_wait_packet(&tftp, option ? TFTP_PKT_OACK : TFTP_PKT_ACK, 0,
                         &recv_size);
  if (err < 0) {
    printf("tftp: wait error, block %d file: %s.\n", 0, filename);
    goto put_error;
  }

  uint16_t curr_block = 1;
  uint32_t total_size = 0;
  uint32_t total_block = 0;
  while (1) {
    size_t block_size =
        fread(tftp.tx_packet.data.data, 1, tftp.block_size, file);
    if (!feof(file) && (block_size != tftp.block_size)) {
      err = -1;
      printf("tftp: read file failed. %s\n", filename);
      goto put_error;
    }

    err = tftp_send_data(&tftp, curr_block, block_size);
    if (err < 0) {
      printf("tftp: send data failed. block: %d\n", curr_block);
      goto put_error;
    }

    err = tftp_wait_packet(&tftp, TFTP_PKT_ACK, curr_block, &recv_size);
    if (err < 0) {
      printf("tftp: wait error. block: %d file: %s\n", curr_block, filename);
      goto put_error;
    }

    curr_block++;
    total_size += (uint32_t)block_size;
    if (++total_block % 0x40 == 0) {
      printf(".");
      fflush(stdout);
    }

    if (block_size < tftp.block_size) {
      err = 0;
      break;
    }
  }

  printf("\n\ttftp: total send: %d bytes, %d block\n", total_size, total_block);
  fclose(file);
  tftp_close();
  return 0;

put_error:
  if (file) {
    fclose(file);
  }
  tftp_close();
  return -1;
}

int tftp_put(const char *ip, uint16_t port, int block_size,
             const char *filename, int option) {
  printf("try to get file %s from %s\n", filename, ip);

  if (block_size > TFTP_BLK_SIZE) {
    block_size = TFTP_BLK_SIZE;
  }

  return do_tftp_put(block_size, ip, port, filename, option);
}

void show_cmd_list() {
  printf("usage: cmd arg0 arg1...\n");
  printf("    get filename -- download file from server\n");
  printf("    put filename -- upload file from server\n");
  printf("    blk size -- set block size\n");
  printf("    quit -- quit\n");
}

int tftp_start(const char *ip, uint16_t port) {
  char cmd_buf[TFTP_CMD_BUF_SIZE];
  int blksize = TFTP_DEF_BLKSIZE;
  if (port == 0) {
    port = TFTP_DEF_PORT;
  }

  printf("welcome to use tftp client\n");
  show_cmd_list();

  while (1) {
    printf("tftp >>");
    fflush(stdout);

    char *buf = fgets(cmd_buf, sizeof(cmd_buf), stdin);
    if (buf == NULL) {
      continue;
    }

    char *cr = strchr(buf, '\n');
    if (cr) {
      *cr = '\0';
    }

    cr = strchr(buf, '\r');
    if (cr) {
      *cr = '\0';
    }

    const char *split = " \t\n";
    char *cmd = strtok(buf, split);
    if (cmd) {
      if (strcmp(cmd, "get") == 0) {
        char *filename = strtok(NULL, split);
        if (filename) {
          do_tftp_get(blksize, ip, port, filename, 1);
        } else {
          printf("error: no file\n");
        }
      } else if (strcmp(cmd, "put") == 0) {
        char *filename = strtok(NULL, split);
        if (filename) {
          do_tftp_put(blksize, ip, port, filename, 1);
        } else {
          printf("error: no file\n");
        }
      } else if (strcmp(cmd, "blk") == 0) {
        char *blk = strtok(NULL, split);
        if (blk) {
          int size = atoi(blk);
          if (size <= 0) {
            printf("blk size %d error", size);
            size = TFTP_DEF_BLKSIZE;
          } else if (size > TFTP_BLK_SIZE) {
            printf("blk size %d too long, set to %d\n", size, blksize);
            size = TFTP_DEF_BLKSIZE;
          }

          blksize = size;
        } else {
          printf("error: no size\n");
        }
      } else if (strcmp(cmd, "quit") == 0) {
        printf("quit from tftp client\n");
        return 0;
      } else {
        printf("unknown cmd\n");
      }
    }
  }

  return 0;
}
