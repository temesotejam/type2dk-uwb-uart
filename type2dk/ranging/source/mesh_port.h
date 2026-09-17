#ifndef MESH_PORT_H
#define MESH_PORT_H
#include <stdbool.h>
#include <stdint.h>
bool mesh_accel_init(uint8_t *who);
bool mesh_accel_sample(int16_t mg[3]);
#endif
