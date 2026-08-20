#ifndef VELA_PYTHON_H
#define VELA_PYTHON_H

#include "vela/config.h"
#include "vela/http.h"
#include "vela/conn.h"

int my_py_init(const my_config_t *cfg);
void my_py_fini(void);
int my_py_load_app(const char *spec);
int my_py_handle_http(my_conn_t *c);
int my_py_ready(void);

#endif
