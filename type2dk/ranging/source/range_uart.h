#ifndef RANGE_UART_H
#define RANGE_UART_H
#include "range_frame.h"
#ifndef MESH_UART_ENABLE
#define MESH_UART_ENABLE (MESH_NODE == 19)
#endif
void mesh_range_snapshot(range_sample_t *s);
void range_uart_start(void);
void range_uart_log(void);
#endif
