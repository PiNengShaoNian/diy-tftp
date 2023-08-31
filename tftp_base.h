#ifndef TFTP_BASE_H
#define TFTP_BASE_H

#include <arpa/inet.h>
#include <stdint.h>
#include <sys/socket.h>

typedef enum _tftp_err_t {
  TFTP_ERR_OK = 0,
  TFTP_ERR_NO_FILE,
  TFTP_ERR_ACC_VIO,
  TFTP_ERR_DISK_FULL,
  TFTP_ERR_OP,
  TFTP_ERR_UNKNOWN_TID,
  TFTP_ERR_FILE_EXIST,
  TFTP_ERR_USER,

  TFTP_ERR_END,
} tftp_err_t;

typedef enum _tftp_op_t {
  TFTP_PKT_RRQ = 1,
  TFTP_PKT_WRQ,
  TFTP_PKT_DATA,
  TFTP_PKT_ACK,
  TFTP_PKT_ERROR,
  TFTP_PKT_OACK,
} tftp_op_t;

#define TFTP_BLK_SIZE 8192
#define TFTP_DEF_BLKSIZE 512
#define TFTP_DEF_PORT 69
#define TFTP_MAX_RETRY 10
#define TFTP_TMO_SEC 3

#pragma pack(1)

typedef struct _tftp_packet_t {
  uint16_t opcode;

  union {
    struct {
      uint8_t args[1];
    } req;

    struct {
      uint16_t block;
      uint8_t data[TFTP_BLK_SIZE];
    } data;

    struct {
      uint16_t block;
    } ack;

    struct {
      char option[1];
    } oack;

    struct {
      uint16_t code;
      char msg[1];
    } err;
  };
} tftp_packet_t;

#pragma pack()

typedef struct _tftp_t {
  int socket;
  struct sockaddr remote;

  int tmo_sec;
  int tmo_retry;

  int tx_size;
  int block_size;
  int file_size;
  tftp_packet_t tx_packet;
  tftp_packet_t rx_packet;
} tftp_t;

#define TFTP_NAME_SIZE 128
typedef struct _tftp_req_t {
  tftp_t tftp;
  tftp_op_t op;
  int option;
  int blksize;
  int filesize;
  char filename[TFTP_NAME_SIZE];
} tftp_req_t;

int tftp_send_request(tftp_t *tftp, int is_read, const char *filename,
                      uint32_t file_size, int option);
int tftp_send_ack(tftp_t *tftp, uint16_t block_num);
int tftp_send_data(tftp_t *tftp, uint16_t block_num, size_t size);
int tftp_send_error(tftp_t *tftp, uint16_t code);
int tftp_wait_packet(tftp_t *tftp, tftp_op_t op, uint16_t block,
                     size_t *pkt_size);
int tftp_parse_oack(tftp_t *tftp);

#endif