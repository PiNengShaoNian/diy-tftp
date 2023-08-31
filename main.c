#include <stdio.h>

#include "tftp_client.h"
#include "tftp_server.h"

int main(void) {
  const char *ip = "192.168.31.141";
  // tftp_get(ip, TFTP_DEF_PORT, 1024, "1.pdf", 1);
  // tftp_put(ip, TFTP_DEF_PORT, 1024, "1.pdf", 1);
  tftpd_start(".", 10000);

  tftp_start(ip, TFTP_DEF_PORT);

  return 0;
}