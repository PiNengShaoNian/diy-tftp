#include "tftp_base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *tftp_err_msg(tftp_err_t err) {
  static const char *msg[] = {
      [TFTP_ERR_OK] = "Unknown error",
      [TFTP_ERR_NO_FILE] = "File not found",
      [TFTP_ERR_ACC_VIO] = "Access violation",
      [TFTP_ERR_DISK_FULL] = "Disk full or allocation exceeded",
      [TFTP_ERR_OP] = "Illegal TFTP operation",
      [TFTP_ERR_UNKNOWN_TID] = "Unknown transfer ID",
      [TFTP_ERR_FILE_EXIST] = "File already exists",
      [TFTP_ERR_USER] = "No such user",
  };

  if (err >= TFTP_ERR_END || err < 0) {
    return msg[TFTP_ERR_OK];
  }

  return msg[err];
}

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

  if (value >= 0) {
    if (buf + 16 >= buf_end) {
      printf("tftp: send buffer too small");
      return NULL;
    }

    sprintf(buf, "%d", value);
    buf += strlen(buf) + 1;
  }

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
                      uint32_t file_size, int option) {
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

  if (option) {
    buf = write_option(tftp, buf, "blksize", tftp->block_size);
    if (buf == NULL) {
      return -1;
    }

    buf = write_option(tftp, buf, "tsize", file_size);
    if (buf == NULL) {
      return -1;
    }
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

int tftp_send_error(tftp_t *tftp, uint16_t code) {
  tftp_packet_t *pkt = &tftp->tx_packet;

  pkt->opcode = htons(TFTP_PKT_ERROR);
  pkt->err.code = htons(code);

  const char *msg = tftp_err_msg(code);
  strcpy(pkt->err.msg, msg);

  int err = tftp_send_packet(tftp, pkt, 4 + (int)strlen(msg) + 1);
  if (err < 0) {
    printf("tftp: send error failed. error code=%d\n", code);
    return -1;
  }

  return 0;
}

int tftp_resend(tftp_t *tftp) {
  tftp_packet_t *pkt = &tftp->tx_packet;

  if (tftp_send_packet(tftp, pkt, tftp->tx_size) < 0) {
    printf("tftp: resend error.\n");
    return -1;
  }

  return 0;
}

int tftp_wait_packet(tftp_t *tftp, tftp_op_t op, uint16_t block,
                     size_t *pkt_size) {
  tftp_packet_t *pkt = &tftp->rx_packet;
  tftp->tmo_retry = TFTP_MAX_RETRY;
  while (1) {
    socklen_t len = sizeof(struct sockaddr);
    ssize_t size = recvfrom(tftp->socket, (uint8_t *)pkt, sizeof(tftp_packet_t),
                            0, &tftp->remote, &len);
    if (size < 0) {
      printf("recv tmo\n");
      if (--tftp->tmo_retry == 0) {
        printf("tftp: wait tmo\n");
        return -1;
      } else {
        tftp_resend(tftp);
        continue;
      }
    }

    uint16_t opcode = ntohs(pkt->opcode);
    if (op == TFTP_PKT_REQ) {
      if (opcode != TFTP_PKT_RRQ && opcode != TFTP_PKT_WRQ) {
        continue;
      }
    } else if (opcode != op) {
      tftp_resend(tftp);
      continue;
    }

    *pkt_size = (size_t)size;

    switch (opcode) {
      case TFTP_PKT_DATA:
      case TFTP_PKT_ACK: {
        if (ntohs(pkt->data.block) != block) {
          tftp_resend(tftp);
          break;
        }
        return 0;
      }
      case TFTP_PKT_ERROR: {
        pkt->err.msg[tftp->block_size - 1] = '\0';
        printf("tftp: recv error = %d, reason: %s\n", ntohs(pkt->err.code),
               pkt->err.msg);
        return -1;
      }
      case TFTP_PKT_RRQ:
      case TFTP_PKT_WRQ: {
        return 0;
      }
      case TFTP_PKT_OACK: {
        tftp_parse_oack(tftp);
        return 0;
      }
      default: {
        tftp_resend(tftp);
        break;
      }
    }
  }
}

int tftp_parse_oack(tftp_t *tftp) {
  char *buf = (char *)tftp->rx_packet.oack.option;
  char *end = (char *)&tftp->rx_packet + sizeof(tftp_packet_t);

  while ((buf < end) && (*buf)) {
    if (strcmp(buf, "blksize") == 0) {
      buf += strlen(buf) + 1;
      int blksize = atoi(buf);
      if (blksize == 0) {
        printf("tftp: unknown blksize\n");
        tftp_send_error(tftp, TFTP_ERR_OP);
        return -1;
      } else if (blksize < tftp->block_size) {
        tftp->block_size = blksize;
        printf("tftp: use new blksize %d\n", blksize);
      } else if (blksize > tftp->block_size) {
        printf("tftp: block size %d\n", blksize);
        return -1;
      }
      buf += strlen(buf) + 1;
    } else if (strcmp(buf, "tsize") == 0) {
      buf += strlen(buf) + 1;
      tftp->file_size = atoi(buf);

      buf += strlen(buf) + 1;
    } else {
      buf += strlen(buf) + 1;
    }
  }
}

int tftp_send_oack(tftp_t *tftp) {
  tftp_packet_t *pkt = &tftp->tx_packet;

  pkt->opcode = htons(TFTP_PKT_OACK);
  char *buf = pkt->oack.option;
  buf = write_option(tftp, buf, "blksize", tftp->block_size);
  if (buf == NULL) {
    return -1;
  }
  buf = write_option(tftp, buf, "tsize", tftp->file_size);
  if (buf == NULL) {
    return -1;
  }

  int err = tftp_send_packet(tftp, pkt, buf - (char *)pkt);
  if (err < 0) {
    printf("tftp: send oack failed.\n");
    return -1;
  }

  return 0;
}