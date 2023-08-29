#ifndef TFTP_CLIENT_H
#define TFTP_CLIENT_H

#include "tftp_base.h"

int tftp_get(const char *ip, uint16_t port, int block_size,
             const char *filename);
int tftp_put(const char *ip, uint16_t port, int block_size,
             const char *filename);

#endif