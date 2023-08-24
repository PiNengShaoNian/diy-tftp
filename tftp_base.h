#ifndef TFTP_BASE_H
#define TFTP_BASE_H

#include <stdint.h>

typedef enum _tftp_op_t {
  TFTP_PKT_RRQ = 1,
  TFTP_PKT_WRQ,
  TFTP_PKT_DATA,
  TFTP_PKT_ACK,
  TFTP_PKT_ERROR,
  TFTP_PKT_OACK,
} tftp_op_t;

#define TFTP_BLK_SIZE 8192

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

#endif