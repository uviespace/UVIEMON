#include "uviemon_reg.h"

#include <stdio.h>
#include <string.h>

static int parse_register_number_single(const char reg_num, uint32_t *dest);
static int parse_register_number(const char * const reg, uint32_t * const reg_num, uint32_t highest_register);

/* Special purpose registers */

static uint32_t get_reg_psr(struct register_desc desc);
static void set_reg_psr(struct register_desc desc, uint32_t value);

static uint32_t get_reg_tbr(struct register_desc desc);
static void set_reg_tbr(struct register_desc desc, uint32_t value);

static uint32_t get_reg_wim(struct register_desc desc);
static void set_reg_wim(struct register_desc desc, uint32_t value);

static uint32_t get_reg_y(struct register_desc desc);
static void set_reg_y(struct register_desc desc, uint32_t value);

static uint32_t get_reg_pc(struct register_desc desc);
static void set_reg_pc(struct register_desc desc, uint32_t value);

static uint32_t get_reg_npc(struct register_desc desc);
static void set_reg_npc(struct register_desc desc, uint32_t value);

static uint32_t get_reg_fsr(struct register_desc desc);
static void set_reg_fsr(struct register_desc desc, uint32_t value);

static uint32_t get_reg_sp(struct register_desc desc);
static void set_reg_sp(struct register_desc desc, uint32_t value);

static uint32_t get_reg_fp(struct register_desc desc);
static void set_reg_fp(struct register_desc desc, uint32_t value);

/* Window registers */

static uint32_t get_reg_global(struct register_desc desc);
static void set_reg_global(struct register_desc desc, uint32_t value);

static uint32_t get_reg_input(struct register_desc desc);
static void set_reg_input(struct register_desc desc, uint32_t value);

static uint32_t get_reg_output(struct register_desc desc);
static void set_reg_output(struct register_desc desc, uint32_t value);

static uint32_t get_reg_local(struct register_desc desc);
static void set_reg_local(struct register_desc desc, uint32_t value);

/* Floating point registers */
static union float_value get_reg_float(struct register_desc desc);
static void set_reg_float(struct register_desc desc, union float_value value);

static union double_value get_reg_double(struct register_desc desc);
static void set_reg_double(struct register_desc desc, union double_value value);


static const struct register_func_standard function_handler[] = {
	{ "inv", none,     NULL,            NULL },
	{ "psr", standard_reg, &get_reg_psr,    &set_reg_psr },
	{ "tbr", standard_reg, &get_reg_tbr,    &set_reg_tbr },
	{ "wim", standard_reg, &get_reg_wim,    &set_reg_wim },
	{ "y",   standard_reg, &get_reg_y,      &set_reg_y },
	{ "pc",  standard_reg, &get_reg_pc,     &set_reg_pc },
	{ "npc", standard_reg, &get_reg_npc,    &set_reg_npc },
	{ "fsr", standard_reg, &get_reg_fsr,    &set_reg_fsr },
	{ "sp",  standard_reg, &get_reg_sp,     &set_reg_sp },
	{ "fp",  standard_reg, &get_reg_fp,     &set_reg_fp },
	{ "g",   standard_reg, &get_reg_global, &set_reg_global },
	{ "i",   standard_reg, &get_reg_input,  &set_reg_input },
	{ "o",   standard_reg, &get_reg_output, &set_reg_output },
	{ "l",   standard_reg, &get_reg_local,  &set_reg_local }
};

static struct register_func_float function_handler_float = { "f", float_reg, &get_reg_float, &set_reg_float };
static struct register_func_double function_handler_double = { "d", double_reg, &get_reg_double, &set_reg_double };




struct register_desc parse_register(const char * const reg, uint32_t cpu)
{
	uint32_t param_length = strlen(reg);

	/* start name is invalid in case parsing doesn't work */
	struct register_desc desc = { "inv", standard_reg, cpu, 0, dsu_get_reg_psr(cpu) & 0x1F  };

	/* Special purpose registers */
	if (strcmp(reg, "psr") == 0) {
		strcpy(desc.name, "psr");
		return desc;
	}

	if (strcmp(reg, "tbr") == 0) {
		strcpy(desc.name, "tbr");
		return desc;
	}

	if (strcmp(reg, "wim") == 0) {
		strcpy(desc.name, "wim");
		return desc;
	}

	if (strcmp(reg, "y") == 0) {
		strcpy(desc.name, "y");
		return desc;
	}

	if (strcmp(reg, "pc") == 0) {
		strcpy(desc.name, "pc");
		return desc;
	}

	if (strcmp(reg, "npc") == 0) {
		strcpy(desc.name, "npc");
		return desc;
	}

	if (strcmp(reg, "fsr") == 0) {
		strcpy(desc.name, "fsr");
		return desc;
	}

	if (strcmp(reg, "sp") == 0) {
		strcpy(desc.name, "sp");
		return desc;
	}

	if (strcmp(reg, "fp") == 0) {
		strcpy(desc.name, "fp");
		return desc;
	}

	/* length must be at least two from here on out */
	if (param_length < 2)
		return desc;

	/* Window registers */
	switch(reg[0]) {

	case 'g':
		if (param_length != 2)
			return desc;

		if (parse_register_number_single(reg[1], &desc.reg_num) == -1)
			return desc;

		strcpy(desc.name, "g");
		break;
		
	case 'i':
		if (param_length != 2)
			return desc;

		if (parse_register_number_single(reg[1], &desc.reg_num) == -1)
			return desc;

		strcpy(desc.name, "i");
		break;
		
	case 'l':
		if (param_length != 2)
			return desc;

		if (parse_register_number_single(reg[1], &desc.reg_num) == -1)
			return desc;

		strcpy(desc.name, "l");
		break;
		
	case 'o':
		if (param_length != 2)
			return desc;

		if (parse_register_number_single(reg[1], &desc.reg_num) == -1)
			return desc;

		strcpy(desc.name, "o");
		break;
		
	case 'f':
		if (param_length > 3)
			return desc;

		if (parse_register_number(reg + 1, &desc.reg_num, 31) == -1)
			return desc;

		desc.type = float_reg;
		strcpy(desc.name, "f");
		break;

	case 'd':
		if (param_length > 3)
			return desc;

		if (parse_register_number(reg + 1, &desc.reg_num, 12) == -1)
			return desc;

		desc.type = double_reg;
		strcpy(desc.name, "d");
		break;
	
	case 'w':
		/* TODO: needs a bit more checks here */
		if (parse_register_number_single(reg[1], &desc.window) == -1)
			return desc;

		if (param_length == 2) {
			strcpy(desc.name, "w");
		} else if (param_length == 4) {
			if (parse_register_number_single(reg[3], &desc.reg_num) == -1)
				return desc;

			desc.name[0] = reg[2];
			desc.name[1] = '\0';
		}
		
		break;
	}

	return desc;
}


struct register_func *get_register_functions(const struct register_desc desc)
{

	switch(desc.type) {
		

	case standard_reg:
		for (uint32_t i = 1; i < (sizeof(function_handler) / sizeof(function_handler[0])); i++) {
			if (strcmp(desc.name, function_handler[i].name) == 0)
				return (struct register_func*)&function_handler[i];
		}

		return (struct register_func*)&function_handler[0];

		break;

	case float_reg:
		return (struct register_func*)&function_handler_float;
		break;

	case double_reg:
		return (struct register_func*)&function_handler_double;
		break;

	case none:
		return (struct register_func*)&function_handler[0];
		break;
	}

	/* should never reach here, but removes warning */
	return (struct register_func*)&function_handler[0];
}


void register_print_summary(uint32_t cpu, uint32_t cwp)
{
	uint32_t ins[8], locals[8], outs[8], globals[8];
	
	dsu_get_input_reg_window(cpu, cwp, ins);
	dsu_get_local_reg_window(cpu, cwp, locals);
	dsu_get_output_reg_window(cpu, cwp, outs);
	dsu_get_global_reg(cpu, globals);

	printf("         %-8s  %-8s  %-8s  %-8s\n", "INS", "LOCALS", "OUTS", "GLOBALS");
	
	for(uint32_t i = 0; i < 8; i++) {
		printf("%6d:  %08X  %08X  %08X  %08X\n", i, ins[i], locals[i], outs[i], globals[i]);
	}
	printf("\n");

	printf("   psr: %08X   wim: %08X   tbr: %08X   y: %08X\n\n",
		   dsu_get_reg_psr(cpu), dsu_get_reg_wim(cpu),
		   dsu_get_reg_tbr(cpu), dsu_get_reg_y(cpu));

	printf("   pc:  %08X\n", dsu_get_reg_pc(cpu));
	printf("   npc: %08X\n", dsu_get_reg_npc(cpu));
	printf("\n\n\n");
}


static int parse_register_number_single(const char reg_num, uint32_t *dest)
{
	if (reg_num < '0' || reg_num > '7')
		return -1;

	*dest = reg_num - '0';
	return 0;
}

static int parse_register_number(const char * const reg, uint32_t * const reg_num, uint32_t highest_register)
{
	uint32_t len = strlen(reg);

	*reg_num = 0;

	if (len > 4)
		return -1;

	for(uint32_t i = 1; i < len; i++) {
		if (reg[i] < '0' || reg[i] > '9')
			return -1;

		*reg_num += (reg[i] - '0') * 10;
	}

	*reg_num = *reg_num / 10;

	if (*reg_num > highest_register)
		return -1;
	else
		return 0;
}


/* Special purpose registers */

static uint32_t get_reg_psr(struct register_desc desc)
{
	return dsu_get_reg_psr(desc.cpu);
}


static void set_reg_psr(struct register_desc desc, uint32_t value)
{
	dsu_set_reg_psr(desc.cpu, value);
}

static uint32_t get_reg_tbr(struct register_desc desc)
{
	return dsu_get_reg_tbr(desc.cpu);
}


static void set_reg_tbr(struct register_desc desc, uint32_t value)
{
	dsu_set_reg_tbr(desc.cpu, value);
}

static uint32_t get_reg_wim(struct register_desc desc)
{
	return dsu_get_reg_wim(desc.cpu);
}


static void set_reg_wim(struct register_desc desc, uint32_t value)
{
	dsu_set_reg_wim(desc.cpu, value);
}

static uint32_t get_reg_y(struct register_desc desc)
{
	return dsu_get_reg_y(desc.cpu);
}


static void set_reg_y(struct register_desc desc, uint32_t value)
{
	dsu_set_reg_y(desc.cpu, value);
}

static uint32_t get_reg_pc(struct register_desc desc)
{
	return dsu_get_reg_pc(desc.cpu);
}


static void set_reg_pc(struct register_desc desc, uint32_t value)
{
	dsu_set_reg_pc(desc.cpu, value);
}

static uint32_t get_reg_npc(struct register_desc desc)
{
	return dsu_get_reg_npc(desc.cpu);
}


static void set_reg_npc(struct register_desc desc, uint32_t value)
{
	dsu_set_reg_npc(desc.cpu, value);
}

static uint32_t get_reg_fsr(struct register_desc desc)
{
	return dsu_get_reg_fsr(desc.cpu);
}


static void set_reg_fsr(struct register_desc desc, uint32_t value)
{
	dsu_set_reg_fsr(desc.cpu, value);
}


static uint32_t get_reg_sp(struct register_desc desc)
{
	return dsu_get_reg_sp(desc.cpu, desc.window);
}

	
static void set_reg_sp(struct register_desc desc, uint32_t value)
{
	dsu_set_reg_sp(desc.cpu, desc.window, value);
}

static uint32_t get_reg_fp(struct register_desc desc)
{
	return dsu_get_reg_fp(desc.cpu, desc.window);
}


static void set_reg_fp(struct register_desc desc, uint32_t value)
{
	dsu_set_reg_fp(desc.cpu, desc.window, value);
}
	
/* Window register */
static uint32_t get_reg_global(struct register_desc desc)
{
	return dsu_get_global_reg_single(desc.cpu, desc.reg_num);  
}

static void set_reg_global(struct register_desc desc, uint32_t value)
{
	dsu_set_global_reg(desc.cpu, desc.reg_num, value);
}

	
static uint32_t get_reg_input(struct register_desc desc)
{
	return dsu_get_input_reg_single(desc.cpu, desc.window, desc.reg_num);
}

static void set_reg_input(struct register_desc desc, uint32_t value)
{
	dsu_set_input_reg(desc.cpu, desc.window, desc.reg_num, value);
}

	
static uint32_t get_reg_output(struct register_desc desc)
{
	return dsu_get_output_reg_single(desc.cpu, desc.window, desc.reg_num);
}
	

static void set_reg_output(struct register_desc desc, uint32_t value)
{
	dsu_set_output_reg(desc.cpu, desc.window, desc.reg_num, value);
}

static uint32_t get_reg_local(struct register_desc desc)
{
	return dsu_get_local_reg_single(desc.cpu, desc.window, desc.reg_num);
}
	
static void set_reg_local(struct register_desc desc, uint32_t value)
{
	dsu_set_local_reg(desc.cpu, desc.window, desc.reg_num, value);
}

/* Floating point register */
	
static union float_value get_reg_float(struct register_desc desc)
{
	return dsu_get_float_reg(desc.cpu, desc.reg_num);
}
	
static void set_reg_float(struct register_desc desc, union float_value value)
{
	dsu_set_float_reg(desc.cpu, desc.reg_num, value);
}

	
static union double_value get_reg_double(struct register_desc desc)
{
	return dsu_get_double_reg(desc.cpu, desc.reg_num);
}
	
static void set_reg_double(struct register_desc desc, union double_value value)
{
	dsu_set_double_reg(desc.cpu, desc.reg_num, value);
}
