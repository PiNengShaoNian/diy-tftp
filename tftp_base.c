#include "tftp_base.h"

#include <stdio.h>
#include <string.h>

static char *write_option(tftp_t *tftp, char *buf, const char *name,
                          int value) {
  char *buf_end = (char *)(&tftp->tx_packet + sizeof(tftp_packet_t));
  size_t len = strlen(name) + 1;
  if (buf + len >= buf_end) {
    printf("tftp: send buffer too small\n");
    return NULL;
  }

  strcpy(buf, name);
  buf += len;

  return buf;
}

int tftp_send_packet(tftp_t *tftp, tftp_packet_t *pkt, int size) {
  ssize_t snd_size = sendto(tftp->socket, (const void *)pkt, size, 0,
                            &tftp->remote, sizeof(tftp->remote));
  if (snd_size < 0) {
    printf("tftp: send error\n");
    return -1;
  }

  tftp->tx_size = size;
  return 0;
}

int tftp_send_request(tftp_t *tftp, int is_read, const char *filename,
                      uint32_t file_size) {
  tftp_packet_t *pkt = &tftp->tx_packet;

  pkt->opcode = htons(is_read ? TFTP_PKT_RRQ : TFTP_PKT_WRQ);

  char *buf = (char *)pkt->req.args;
  buf = write_option(tftp, buf, filename, -1);
  if (buf == NULL) {
    printf("tftp: filename too long: %s\n", filename);
    return -1;
  }

  buf = write_option(tftp, buf, "octet", -1);
  if (buf == NULL) {
    printf("tftp: filename too long: %s\n", filename);
    return -1;
  }

  int size = (int)(buf - (char *)pkt->req.args) + 2;
  int err = tftp_send_packet(tftp, pkt, size);
  if (err < 0) {
    printf("tftp: send req failed.\n");
    return -1;
  }

  return 0;
}

int tftp_send_ack(tftp_t *tftp, uint16_t block_num) {
  tftp_packet_t *pkt = &tftp->tx_packet;

  pkt->opcode = htons(TFTP_PKT_ACK);
  pkt->ack.block = htons(block_num);

  int err = tftp_send_packet(tftp, pkt, 4);
  if (err < 0) {
    printf("tftp: send ack failed. block num=%d\n", block_num);
    return -1;
  }

  return 0;
}

int tftp_send_data(tftp_t *tftp, uint16_t block_num, size_t size) {
  tftp_packet_t *pkt = &tftp->tx_packet;

  pkt->opcode = htons(TFTP_PKT_DATA);
  pkt->data.block = htons(block_num);

  int err = tftp_send_packet(tftp, pkt, 4 + (int)size);
  if (err < 0) {
    printf("tftp: send data failed. block num=%d\n", block_num);
    return -1;
  }

  return 0;
}
