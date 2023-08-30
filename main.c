#include <stdio.h>

#include "tftp_client.h"

int main(void) {
  const char *ip = "192.168.31.141";
  tftp_get(ip, TFTP_DEF_PORT, 1024, "1.pdf", 1);
  // tftp_put(ip, TFTP_DEF_PORT, 1024, "1.pdf", 1);
  return 0;
}