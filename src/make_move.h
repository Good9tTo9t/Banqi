#ifndef MAKE_MOVE_H
#define MAKE_MOVE_H
#include <string.h>

void get_role_color(const char* json, const char* role, char* out_color);

void make_move(const char* json, const char* my_role_ab);

#endif
