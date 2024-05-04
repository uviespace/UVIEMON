#ifndef UVIEMON_REG_H
#define UVIEMON_REG_H


#include "leon3_dsu.h"

enum register_type {
	none, standard_reg, float_reg, double_reg
};

struct register_desc {
	char name[4];
	enum register_type type;
	uint32_t cpu;
	uint32_t reg_num;
	uint32_t window;
	
};


struct register_func {
	char name[4];
	enum register_type type;
};

struct register_func_standard {
	char name[4];
	enum register_type type;
	uint32_t (*get_value)(struct register_desc);
	void (*set_value)(struct register_desc, uint32_t value);
};

struct register_func_float {
	char name[4];
	enum register_type type;
	union float_value (*get_value)(struct register_desc);
	void (*set_value)(struct register_desc, union float_value value);
};

struct register_func_double {
	char name[4];
	enum register_type type;
	union double_value (*get_value)(struct register_desc);
	void (*set_value)(struct register_desc, union double_value value);
};




void register_print_summary(uint32_t cpu, uint32_t cwp);

struct register_desc parse_register(const char * const reg, uint32_t cpu);
struct register_func *get_register_functions(const struct register_desc desc);


#endif
