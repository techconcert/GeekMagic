#pragma once
#include "ip_addr.h"
typedef void (*dns_found_callback)(const char* name, const ip_addr_t* ipaddr, void* callback_arg);
err_t dns_gethostbyname(const char* hostname, ip_addr_t* addr, dns_found_callback found, void* callback_arg);
