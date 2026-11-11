#include <Arduino.h>
namespace x {
void f() { tcp_new(); dns_gethostbyname("api.open-meteo.com", 0, 0, 0); pbuf_free(0); }
}
