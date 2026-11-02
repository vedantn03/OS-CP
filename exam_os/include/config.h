#ifndef CONFIG_H
#define CONFIG_H

#include "shared.h"

// Parse config file + override with CLI args
void config_load_defaults(Config *cfg);
int  config_parse_file(Config *cfg, const char *filepath);
int  config_parse_args(Config *cfg, int argc, char *argv[]);
int  config_validate(const Config *cfg, char *error, size_t error_size);
void config_print(Config *cfg);

#endif // CONFIG_H