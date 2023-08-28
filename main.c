#include <stdio.h>

#include "tftp_client.h"

int main(void) {
  const char *ip = "192.168.31.141";
  tftp_get(ip, TFTP_DEF_PORT, 512, "1.png");

  return 0;
}