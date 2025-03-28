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

#include "ftdi_device.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#define MAX_PARAMETERS 3
#define MAX_PARAM_LENGTH 50

#define OBJ_DUMP_PARAM_LENGTH 8
#define OBJ_DUMP_STRING_LENGTH 25
#define VMA_PARAM 5

typedef struct {
	const char *command_name;
	void (*function)(const char *, int, char [MAX_PARAMETERS][MAX_PARAM_LENGTH]);
} command;

struct tt_error {
	uint32_t error_code;
	const char * const error_desc;
};

int parse_input(char *input);
void print_help_text();

/* Command functions  */
void cli_help  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void cli_scan  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void cli_memx  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void cli_wmemx (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void cli_run   (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void cli_reset (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void cli_load  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void cli_verify(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void cli_bdump (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void cli_washc (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void cli_inst  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void cli_reg   (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void cli_cpu   (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);

void wmem(DWORD addr, DWORD data);
void wmemh(DWORD addr, WORD data);
void wmemb(DWORD addr, BYTE data);

void mem(DWORD startAddr, DWORD length);
void memh(DWORD startAddr, DWORD length);
void memb(DWORD startAddr, DWORD length);

void bdump(DWORD startAddr, DWORD length, const char * const path);

void wash(WORD size, DWORD addr, DWORD c);
//void load(std::string path);
//void verify(std::string path); // Really slow because sequential reads are dead slow


#endif /* UVIEMON_CLI_HPP */
