/*
	============================================
	uviemon: free(TM) replacement for grmon

	Functions used in the command line interface
	of uviemon. All these functions corrrespond
	to their command in the console to be called
	by the user.
	============================================
*/

#ifndef UVIEMON_CLI_HPP
#define UVIEMON_CLI_HPP

#include "ftdi_device.hpp"

#include <string>
#include <unordered_map>

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#define MAX_PARAMETERS 3
#define MAX_PARAM_LENGTH 50

typedef struct {
	const char *command_name;
	void (*function)(const char *, int, char [MAX_PARAMETERS][MAX_PARAM_LENGTH]);
} command;


/*
std::string _hexToString(DWORD *data, size_t size);
std::string _hexToString(WORD *data, size_t size);
std::string _hexToString(BYTE *data, size_t size);
*/

int parse_input(char *input);
void print_help_text();
//void cleanup();

struct op_call_t {
	uint32_t disp30 :30;
	uint32_t op     :2;
};
struct op_sethi_t  {
	uint32_t imm22 :22;
	uint32_t op2   :3;
	uint32_t rd    :5;
	uint32_t op    :2;
};
struct op_branch_t {
	uint32_t disp22 :22;
	uint32_t op2    :3;
	uint32_t cond   :4;
	uint32_t a      :1;	
	uint32_t op     :2;
};
struct op_other_1_t {
	uint32_t rs2 :5;
	uint32_t asi :8;
	uint32_t i   :1;
	uint32_t rs1 :5;
	uint32_t op3 :6;	
	uint32_t rd  :5;	
	uint32_t op  :2;
};
struct op_other_2_t {
	uint32_t simm13 :13;
	uint32_t i      :1;	
	uint32_t rs1    :5;
	uint32_t op3    :6;
	uint32_t rd     :5;	
	uint32_t op     :2;
};
struct op_other_3_t {
	uint32_t rs2 :5;
	uint32_t opf :9;
	uint32_t rs1 :5;
	uint32_t op3 :6;
	uint32_t rd  :5;	
	uint32_t op  :2;
};


struct opcode {
	union {
		struct op_call_t op_call;
		struct op_sethi_t op_sethi;
		struct op_branch_t op_branch;
		struct op_other_1_t op_other_1;
		struct op_other_2_t op_other_2;
		struct op_other_3_t op_other_3;
		uint32_t opcode_raw;
	};
}__attribute__((packed));

 
struct tt_error {
	uint32_t error_code;
	const char * const error_desc;
};


/* Command functions  */
void help  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void scan  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void memx  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void wmemx (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void run   (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void reset (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void load  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void verify(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void bdump (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void washc (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void inst  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void reg   (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void cpu   (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);

void wmem(DWORD addr, DWORD data);
void wmemh(DWORD addr, WORD data);
void wmemb(DWORD addr, BYTE data);

void mem(DWORD startAddr, DWORD length);
void memh(DWORD startAddr, DWORD length);
void memb(DWORD startAddr, DWORD length);

void bdump(DWORD startAddr, DWORD length, std::string path);

void wash(WORD size, DWORD addr, DWORD c);
void load(std::string path);
void verify(std::string path); // Really slow because sequential reads are dead slow


#endif /* UVIEMON_CLI_HPP */
