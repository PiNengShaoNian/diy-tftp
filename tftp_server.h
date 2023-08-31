#ifndef TFTP_SERVER_H
#define TFTP_SERVER_H

#include "tftp_base.h"

int tftpd_start(const char *dir, uint16_t port);

#endif