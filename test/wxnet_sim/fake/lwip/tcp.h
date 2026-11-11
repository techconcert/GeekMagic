#pragma once
#include "ip_addr.h"
#include "pbuf.h"
struct tcp_pcb;
typedef err_t (*tcp_recv_fn)(void* arg, tcp_pcb* pcb, pbuf* p, err_t err);
typedef err_t (*tcp_connected_fn)(void* arg, tcp_pcb* pcb, err_t err);
typedef void (*tcp_err_fn)(void* arg, err_t err);
typedef err_t (*tcp_sent_fn)(void* arg, tcp_pcb* pcb, u16_t len);
typedef err_t (*tcp_poll_fn)(void* arg, tcp_pcb* pcb);
struct tcp_pcb { void* arg; tcp_recv_fn recv; tcp_connected_fn connected; tcp_err_fn errf; tcp_sent_fn sent; tcp_poll_fn poll; int freed; const void* wdata; u16_t wlen; };
tcp_pcb* tcp_new();
void tcp_arg(tcp_pcb*, void*); void tcp_recv(tcp_pcb*, tcp_recv_fn); void tcp_err(tcp_pcb*, tcp_err_fn);
void tcp_sent(tcp_pcb*, tcp_sent_fn); void tcp_poll(tcp_pcb*, tcp_poll_fn, u8_t);
err_t tcp_connect(tcp_pcb*, const ip_addr_t*, u16_t, tcp_connected_fn);
err_t tcp_write(tcp_pcb*, const void*, u16_t, u8_t); err_t tcp_output(tcp_pcb*);
void tcp_recved(tcp_pcb*, u16_t); err_t tcp_close(tcp_pcb*); void tcp_abort(tcp_pcb*);
