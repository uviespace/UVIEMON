/**
 * @file    leon3_dsu.c
 * @ingroup leon3_dsu
 * @author Armin Luntzer (armin.luntzer@univie.ac.at),
 * @date   February, 2016
 *
 * @copyright GPLv2
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * @defgroup leon3_dsu LEON3 DSU interface
 *
 * @brief Access to the DSU of the GR712RC LEON3FT (and possibly others)
 *
 *
 * ## Overview
 *
 * This component implements access to the debug support unit of the GR712RC
 * LEON3 processor.
 *
 * ## Mode of Operation
 *
 * @see _GR712RC user manual v2.7 chapter 9_ for information on the DSU
 *
 * ## Error Handling
 *
 * None
 *
 * ## Notes
 *
 * - functionality is added as needed
 * - this can be the basis for a grmon replacement
 *
 */

#include <stddef.h>	// size_t
#include <stdlib.h> // malloc, free
#include "leon3_dsu.h"

#include "ftdi_device.hpp"

#define offset_of(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)


/**
 * @brief calculate address of output register %on for a window
 * @see GR712-UM v2.3 pp. 81
 *
 * @param cpu the cpu number
 * @param n   the index of the output register %on
 * @param cwp the index of the register window
 *
 * @return 0 if error, otherwise address of register
 */

__attribute__((unused))
static uint32_t dsu_get_output_reg_addr(uint32_t cpu, uint32_t n, uint32_t cwp)
{
	if (cwp > NWINDOWS) {
		pr_err("ERR_DSU_CWP_INVALID\n");
		return 0;
	}

	return (DSU_BASE(cpu) + 0x300000
		+ (((cwp * 64) + 32 + (n * 4)) % (NWINDOWS * 64)));
}


/**
 * @brief calculate address of local register %ln for a window
 * @see GR712-UM v2.3 pp. 81
 *
 * @param cpu the cpu number
 * @param n   the index of the local register %ln
 * @param cwp the index of the register window
 *
 * @return 0 if error, otherwise address of register
 */

__attribute__((unused))
static uint32_t dsu_get_local_reg_addr(uint32_t cpu, uint32_t n, uint32_t cwp)
{
	if (cwp > NWINDOWS) {
		pr_err("ERR_DSU_CWP_INVALID\n");
		return 0;
	}

	return (DSU_BASE(cpu) + 0x300000
		+ (((cwp * 64) + 64 + (n * 4)) % (NWINDOWS * 64)));
}





/**
 * @brief calculate address of input register %in for a window
 * @see GR712-UM v2.3 pp. 81
 *
 * @param cpu the cpu number
 * @param n   the index of the input register %in
 * @param cwp the index of the register window
 *
 * @return 0 if error, otherwise address of register
 */

__attribute__((unused))
static uint32_t dsu_get_input_reg_addr(uint32_t cpu, uint32_t n, uint32_t cwp)
{
	if (cwp > NWINDOWS) {
		pr_err("ERR_DSU_CWP_INVALID\n");
		return 0;
	}

	return (DSU_BASE(cpu) + 0x300000
		+ (((cwp * 64) + 96 + (n * 4)) % (NWINDOWS * 64)));
}


/**
 * @brief calculate address of global register %gn
 * @see GR712-UM v2.3 pp. 81
 *
 * @param cpu the cpu number
 * @param n   the index of the global register %gn
 *
 * @return address of register
 */

__attribute__((unused))
static uint32_t dsu_get_global_reg_addr(uint32_t cpu, uint32_t n)
{
    return (DSU_BASE(cpu) + 0x300000 + NWINDOWS * 64 + n * 4);
}


/**
 * @brief calculate address of floating point register %fn
 * @see GR712-UM v2.3 pp. 81
 *
 * @param cpu the cpu number
 * @param n   the index of the floating-point register %fn
 *
 * @return address of register
 */

__attribute__((unused))
static uint32_t dsu_get_fpu_reg_addr(uint32_t cpu, uint32_t n)
{
	return (DSU_BASE(cpu) + 0x301000 + n * 4);
}


/**
 * @brief get content of the global register %gn
 * @see GR712-UM v2.3 pp. 81
 *
 * @param cpu the cpu number
 * @param n   the index of the global register %gn
 *
 * @return content of register
 */

uint32_t dsu_get_global_reg(uint32_t cpu, uint32_t n)
{
	return ioread32((uint32_t) dsu_get_global_reg_addr(cpu, n));
}


/**
 * @brief set bits in the DSU control register
 *
 * @param cpu   the cpu number
 * @param flags the bitmask to set
 */

static void dsu_set_dsu_ctrl(uint32_t cpu, uint32_t flags)
{
	uint32_t tmp;


	tmp  = ioread32((uint32_t) (DSU_BASE(cpu)));
	tmp |= flags;
	iowrite32((uint32_t) (DSU_BASE(cpu)), tmp);
}


/**
 * @brief get the DSU control register
 *
 * @param cpu   the cpu number
 *
 * @return the contents of the DSU control register
 */

uint32_t dsu_get_dsu_ctrl(uint32_t cpu)
{
	return ioread32((uint32_t) (DSU_BASE(cpu)));
}


/**
 * @brief clear bits in the DSU control register
 *
 * @param cpu   the cpu number
 * @param flags the bitmask of flags to clear
 */

static void dsu_clear_dsu_ctrl(uint32_t cpu, uint32_t flags)
{
	uint32_t tmp;


	tmp  = ioread32((uint32_t) (DSU_BASE(cpu)));
	tmp &= ~flags;
	iowrite32((uint32_t) (DSU_BASE(cpu)), tmp);
}


/**
 * @brief clear the Integer Units register file
 *
 * @param cpu the cpu number
 */

void dsu_clear_iu_reg_file(uint32_t cpu)
{
	uint32_t i;
	/* (NWINDOWS * (%ln + %ion) + %gn) * 4 bytes */
	const uint32_t iu_reg_size = (NWINDOWS * (8 + 8) + 8) * 4;


	for (i = 0; i < iu_reg_size; i += 4)
		iowrite32((uint32_t) (DSU_BASE(cpu) + DSU_IU_REG + i), 0x0);
}


/**
 * @brief enable forcing processor to enter debug mode if any other processor
 *        in the system enters debug mode
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 83
 */

void dsu_set_force_enter_debug_mode(uint32_t cpu)
{
	uint16_t tmp;

	uint32_t addr = DSU_CTRL + DSU_MODE_MASK + offset_of(struct dsu_mode_mask, enter_debug);


	tmp  = ioread16(addr);
	tmp |= (1 << cpu);
	iowrite16(addr, tmp);
}


/**
 * @brief disable forcing processor to enter debug mode if any other processor
 *        in the system enters debug mode
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 83
 */

void dsu_clear_force_enter_debug_mode(uint32_t cpu)
{
	uint16_t tmp;

	uint32_t addr = DSU_CTRL + DSU_MODE_MASK + offset_of(struct dsu_mode_mask, enter_debug);


	tmp  = ioread16(addr);
	tmp &= ~(1 << cpu);
	iowrite16(addr, tmp);
}


/**
 * @brief do not allow a processor to force other processors into debug mode
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 83
 */

void dsu_set_noforce_debug_mode(uint32_t cpu)
{
	uint16_t tmp;

	uint32_t addr = DSU_CTRL + DSU_MODE_MASK + offset_of(struct dsu_mode_mask, debug_mode);


	tmp  = ioread16(addr);
	tmp |= (1 << cpu);
	iowrite16(addr, tmp);
}


/**
 * @brief allow a processor to force other processors into debug mode
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 83
 */

void dsu_clear_noforce_debug_mode(uint32_t cpu)
{
	uint16_t tmp;

	uint32_t addr = DSU_CTRL + DSU_MODE_MASK + offset_of(struct dsu_mode_mask, debug_mode);


	tmp  = ioread16(addr);
	tmp &= ~(1 << cpu);
	iowrite16(addr, tmp);
}


/**
 * @brief force a processors into debug mode if Break on Watchpoint (BW) bit
 *        in DSU control register is set
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 82
 */

void dsu_set_force_debug_on_watchpoint(uint32_t cpu)
{
	uint16_t tmp;

	uint32_t addr = DSU_CTRL + DSU_BREAK_STEP + offset_of(struct dsu_break_step, break_now);


	tmp  = ioread16(addr);
	tmp |= (1 << cpu);
	iowrite16(addr, tmp);
}


/**
 * @brief clear forcing debug mode if Break on
 *	  Watchpoint (BW) bit in the DSU control register is set;
 *	  resumes processor execution if in debug mode
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 83
 */

void dsu_clear_force_debug_on_watchpoint(uint32_t cpu)
{
	uint16_t tmp;

	uint32_t addr = DSU_CTRL + DSU_BREAK_STEP + offset_of(struct dsu_break_step, break_now);


	tmp  = ioread16(addr);
	tmp &= ~(1 << cpu);
	iowrite16(addr, tmp);
}


/**
 * @brief check if cpu is in error mode
 *
 * @param cpu the cpu number
 * @return 1 if processor in error mode, else 0
 *
 * @see GR712-UM v2.3 pp. 82
 */

uint32_t dsu_get_cpu_in_error_mode(uint32_t cpu)
{
	return ((dsu_get_dsu_ctrl(cpu) & DSU_CTRL_PE) >> 9) & 1;
}


/**
 * @brief clear debug and halt flag of processor
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 82
 */

void dsu_clear_cpu_error_mode(uint32_t cpu)
{
	//dsu_clear_dsu_ctrl(cpu, DSU_CTRL_PE);
	dsu_set_dsu_ctrl(cpu, DSU_CTRL_PE);
}


/**
 * @brief read out the DSU trap register; ONLY WORKS FOR ONE PROCESSOR ATM!
 *
 * @param cpu the cpu number
 *
 * @return trap register content
 *
 * @see GR712-UM v2.3 pp. 82
 */

uint32_t dsu_get_reg_trap(uint32_t cpu)
{
	return ioread32((uint32_t) DSU_CTRL + DSU_REG_TRAP);
}



/**
 * @brief get input register of current window pointer
 *
 * @param cpu the cpu number
 *
 * @param buffer point to array where values will be written
 */
void dsu_get_input_reg(uint32_t cpu, uint32_t *buffer)
{
	uint32_t psr_reg = dsu_get_reg_psr(cpu);
	uint32_t cwp = psr_reg & 0x1F;

	ioread32(DSU_REG_IN(cpu, cwp), buffer, 8);
}


/**
 * @brief get local register of current window pointer
 *
 * @param cpu the cpu number
 *
 * @param buffer pointer to array where values will be written
 */
void dsu_get_local_reg(uint32_t cpu, uint32_t *buffer)
{
	uint32_t psr_reg = dsu_get_reg_psr(cpu);
	uint32_t cwp = psr_reg & 0x1F;

	ioread32(DSU_REG_LOCAL(cpu, cwp), buffer, 8);
}

/**
 * @brief get output register of current window pointer
 *
 * @param cpu the cpu number
 *
 * @param buffer point to array where values will be written
 */
void dsu_get_output_reg(uint32_t cpu, uint32_t *buffer)
{
	uint32_t psr_reg = dsu_get_reg_psr(cpu);
	uint32_t cwp = psr_reg & 0x1F;

	ioread32(DSU_REG_OUT(cpu, cwp), buffer, 8);
}

/**
 * @brief get gloabl register
 *
 * @param cpu the cpu number
 *
 * @param buffer point to array where values will be written
 */
void dsu_get_global_reg(uint32_t cpu, uint32_t *buffer)
{
	ioread32(DSU_REG_GLOBAL(cpu), buffer, 8);
}

void dsu_get_local_reg_window(uint32_t cpu, uint32_t window, uint32_t *buffer)
{
	ioread32(DSU_REG_LOCAL(cpu, window), buffer, 8);
}


void dsu_get_input_reg_window(uint32_t cpu, uint32_t window, uint32_t *buffer)
{
	ioread32(DSU_REG_IN(cpu, window), buffer, 8);
}


void dsu_get_output_reg_window(uint32_t cpu, uint32_t window, uint32_t *buffer)
{
	ioread32(DSU_REG_OUT(cpu, window), buffer, 8);
}


uint32_t dsu_get_local_reg_single(uint32_t cpu, uint32_t cwp, uint32_t reg_num)
{
	return ioread32(dsu_get_local_reg_addr(cpu, reg_num, cwp));
}

uint32_t dsu_get_input_reg_single(uint32_t cpu, uint32_t cwp, uint32_t reg_num)
{
	return ioread32(dsu_get_input_reg_addr(cpu, reg_num, cwp));
}

uint32_t dsu_get_output_reg_single(uint32_t cpu, uint32_t cwp, uint32_t reg_num)
{
	return ioread32(dsu_get_output_reg_addr(cpu, reg_num, cwp));
}

uint32_t dsu_get_global_reg_single(uint32_t cpu, uint32_t reg_num)
{
	return ioread32(dsu_get_global_reg_addr(cpu, reg_num));
}


union float_value dsu_get_float_reg(uint32_t cpu, uint32_t reg_num)
{
	union float_value val = {0};

	if (reg_num > 31)
		return val;
	
	val.u = ioread32(DSU_BASE(cpu) + DSU_FPU_REG + reg_num * 4);
	return val;
}


union double_value dsu_get_double_reg(uint32_t cpu, uint32_t reg_num)
{
	uint32_t address = DSU_BASE(cpu) + DSU_FPU_REG + 32 * 4 + reg_num * 8;
	union double_value val = {0};

	if (reg_num > 12)
		return val;

	val.u = (uint64_t)ioread32(address);
	val.u = val.u << 32;
	val.u += (uint64_t)ioread32(address + 4);
	
	return val;
}



/**
 * @brief check if cpu is in debug mode
 *
 * @param cpu the cpu number
 *
 * @return 1 if processor in debug mode, else 0
 *
 * @see GR712-UM v2.3 pp. 82
 */

uint32_t dsu_get_cpu_in_debug_mode(uint32_t cpu)
{
	return ((dsu_get_dsu_ctrl(cpu) & DSU_CTRL_DM) >> 6) & 1;
}


/**
 * @brief check if cpu is in halt mode
 *
 * @param cpu the cpu number
 *
 * @return 1 if processor in halt mode, else 0
 *
 * @see GR712-UM v2.3 pp. 82
 */

uint32_t dsu_get_cpu_in_halt_mode(uint32_t cpu)
{
	return ((dsu_get_dsu_ctrl(cpu) & DSU_CTRL_HL) >> 10) & 1;
}


/**
 * @brief clear cpu halt mode and resume core
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 82
 */

void dsu_clear_cpu_halt_mode(uint32_t cpu)
{
	dsu_clear_dsu_ctrl(cpu, DSU_CTRL_HL);
}


/**
 * @brief  put cpu in halt mode
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 82
 */

void dsu_set_cpu_halt_mode(uint32_t cpu)
{
	dsu_set_dsu_ctrl(cpu, DSU_CTRL_HL);
}


/**
 * @brief  wake up a cpu core
 *
 * @param cpu the cpu number
 */

void dsu_set_cpu_wake_up(uint32_t cpu)
{
	//dsu_set_dsu_ctrl(cpu, DSU_CTRL_PE);

	//iowrite32((uint32_t)0x80000210, 1 << cpu); // LEON3
	// iowrite32be(1 << cpu, (uint32_t)0xFF904010); // LEON4
	iowrite32((uint32_t)ADDRESSES[get_connected_cpu_type()][WAKE_STATE], 1 << cpu);
}


/**
 * @brief  get status of a cpu core
 *
 * @param cpu the cpu number
 * 
 * @return 1 = power down, 0 = running
 */

uint32_t dsu_get_cpu_state(uint32_t cpu)
{
	//return (ioread32((uint32_t)0x80000210) >> cpu) & 1;
	return (ioread32((uint32_t)ADDRESSES[get_connected_cpu_type()][WAKE_STATE]) >> cpu) & 1;
}


/**
 * @brief  enable debug mode on error
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 82
 */

void dsu_set_cpu_debug_on_error(uint32_t cpu)
{
	dsu_set_dsu_ctrl(cpu, DSU_CTRL_BE);
}


/**
 * @brief  disable debug mode on error
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 82
 */

void dsu_clear_cpu_debug_on_error(uint32_t cpu)
{
	dsu_clear_dsu_ctrl(cpu, DSU_CTRL_BE);
}


/**
 * @brief  enable debug mode on IU watchpoint
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 82
 */

void dsu_set_cpu_break_on_iu_watchpoint(uint32_t cpu)
{
	dsu_set_dsu_ctrl(cpu, DSU_CTRL_BW);
}


/**
 * @brief  disable debug mode on IU watchpoint
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 82
 */

void dsu_clear_cpu_break_on_iu_watchpoint(uint32_t cpu)
{
	dsu_clear_dsu_ctrl(cpu, DSU_CTRL_BW);
}


/**
 * @brief  enable debug mode on breakpoint instruction (ta 1)
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 82
 */

void dsu_set_cpu_break_on_breakpoint(uint32_t cpu)
{
	dsu_set_dsu_ctrl(cpu, DSU_CTRL_BS);
}


/**
 * @brief  disable debug mode on breakpoint instruction
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 82
 */

void dsu_clear_cpu_break_on_breakpoint(uint32_t cpu)
{
	dsu_clear_dsu_ctrl(cpu, DSU_CTRL_BS);
}


/**
 * @brief  enable debug mode on trap
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 82
 */

void dsu_set_cpu_break_on_trap(uint32_t cpu)
{
	dsu_set_dsu_ctrl(cpu, DSU_CTRL_BX);
}


/**
 * @brief  disable debug mode on trap
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 82
 */

void dsu_clear_cpu_break_on_trap(uint32_t cpu)
{
	dsu_clear_dsu_ctrl(cpu, DSU_CTRL_BX);
}


/**
 * @brief  enable debug mode on error trap (all except:
 *         (priviledged_instruction, fpu_disabled, window_overflow,
 *         window_underflow, asynchronous_interrupt, ticc_trap)
  * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 82
 */

void dsu_set_cpu_break_on_error_trap(uint32_t cpu)
{
	dsu_set_dsu_ctrl(cpu, DSU_CTRL_BZ);
}


/**
 * @brief  disable debug mode on error trap (all except:
 *         (priviledged_instruction, fpu_disabled, window_overflow,
 *         window_underflow, asynchronous_interrupt, ticc_trap)
 *
 * @param cpu the cpu number
 *
 * @see GR712-UM v2.3 pp. 82
 */

void dsu_clear_cpu_break_on_error_trap(uint32_t cpu)
{
	dsu_clear_dsu_ctrl(cpu, DSU_CTRL_BZ);
}

/**
 * @brief  get %y register of cpu
 *
 * @param cpu the cpu number
 *
 * @return contents of the %y register
 */

uint32_t dsu_get_reg_y(uint32_t cpu)
{
	return ioread32((uint32_t) (DSU_BASE(cpu) + DSU_REG_Y));
}


/**
 * @brief  set %y register of cpu
 *
 * @param cpu the cpu number
 * @param val the value to set
 */

void dsu_set_reg_y(uint32_t cpu, uint32_t val)
{
	iowrite32((uint32_t) (DSU_BASE(cpu) + DSU_REG_Y), val);
}


/**
 * @brief  get %psr register of cpu
 *
 * @param cpu the cpu number
 *
 * @return contents of the %psr register
 */

uint32_t dsu_get_reg_psr(uint32_t cpu)
{
	return ioread32((uint32_t) (DSU_BASE(cpu) + DSU_REG_PSR));
}


/**
 * @brief  set %psr register of cpu
 *
 * @param cpu the cpu number
 * @param val the value to set
 */

void dsu_set_reg_psr(uint32_t cpu, uint32_t val)
{
	iowrite32((uint32_t) (DSU_BASE(cpu) + DSU_REG_PSR), val);
}


/**
 * @brief  get %wim register of cpu
 *
 * @param cpu the cpu number
 *
 * @return contents of the %wim register
 */

uint32_t dsu_get_reg_wim(uint32_t cpu)
{
	return ioread32((uint32_t) (DSU_BASE(cpu) + DSU_REG_WIM));
}


/**
 * @brief  set %wim register of cpu
 *
 * @param cpu the cpu number
 * @param val the value to set
 */

void dsu_set_reg_wim(uint32_t cpu, uint32_t val)
{
	iowrite32((uint32_t) (DSU_BASE(cpu) + DSU_REG_WIM), val);
}


/**
 * @brief  get %tbr register of cpu
 *
 * @param cpu the cpu number
 *
 * @return contents of the %tbr register
 */

uint32_t dsu_get_reg_tbr(uint32_t cpu)
{
	return ioread32((uint32_t) (DSU_BASE(cpu) + DSU_REG_TBR));
}


/**
 * @brief  set %tbr register of cpu
 *
 * @param cpu the cpu number
 * @param val the value to set
 */

void dsu_set_reg_tbr(uint32_t cpu, uint32_t val)
{
	iowrite32((uint32_t) (DSU_BASE(cpu) + DSU_REG_TBR), val);
}


/**
 * @brief  get %pc register of cpu
 *
 * @param cpu the cpu number
 *
 * @return contents of the %pc register
 */

uint32_t dsu_get_reg_pc(uint32_t cpu)
{
	return ioread32((uint32_t) (DSU_BASE(cpu) + DSU_REG_PC));
}


/**
 * @brief  set %npc register of cpu
 *
 * @param cpu the cpu number
 * @param val the value to set
 */

void dsu_set_reg_pc(uint32_t cpu, uint32_t val)
{
	iowrite32((uint32_t) (DSU_BASE(cpu) + DSU_REG_PC), val);
}


/**
 * @brief  get %npc register of cpu
 *
 * @param cpu the cpu number
 *
 * @return contents of the %npc register
 */

uint32_t dsu_get_reg_npc(uint32_t cpu)
{
	return ioread32((uint32_t) (DSU_BASE(cpu) + DSU_REG_NPC));
}


/**
 * @brief  set %npc register of cpu
 *
 * @param cpu the cpu number
 * @param val the value to set
 */

void dsu_set_reg_npc(uint32_t cpu, uint32_t val)
{
	iowrite32((uint32_t) (DSU_BASE(cpu) + DSU_REG_NPC), val);
}


/**
 * @brief  get %fsr register of cpu
 *
 * @param cpu the cpu number
 *
 * @return contents of the %fsr register
 */

uint32_t dsu_get_reg_fsr(uint32_t cpu)
{
	return ioread32((uint32_t) (DSU_BASE(cpu) + DSU_REG_FSR));
}


/**
 * @brief  set %fsr register of cpu
 *
 * @param cpu the cpu number
 * @param val the value to set
 */

void dsu_set_reg_fsr(uint32_t cpu, uint32_t val)
{
	iowrite32((uint32_t) (DSU_BASE(cpu) + DSU_REG_FSR), val);
}


/**
 * @brief  get %cpsr register of cpu
 *
 * @param cpu the cpu number
 *
 * @return contents of the %cpsr register
 */

uint32_t dsu_get_reg_cpsr(uint32_t cpu)
{
	return ioread32((uint32_t) (DSU_BASE(cpu) + DSU_REG_CPSR));
}

/**
 * @brief  set %cpsr register of cpu
 *
 * @param cpu the cpu number
 * @param val the value to set
 */

void dsu_set_reg_cpsr(uint32_t cpu, uint32_t val)
{
	iowrite32((uint32_t) (DSU_BASE(cpu) + DSU_REG_CPSR), val);
}

/**
 * @brief  set stack pointer register (%o6) in a window of a cpu
 *
 * @param cpu the cpu number
 * @param cwp the window number
 * @param val the value to set
 */

void dsu_set_reg_sp(uint32_t cpu, uint32_t cwp, uint32_t val)
{
	uint32_t reg;

	reg = dsu_get_output_reg_addr(cpu, 6, cwp);

	if (reg)
		iowrite32((uint32_t) reg, val);
}


/**
 * @brief  get stack pointer register (%o6) in a window of a cpu
 *
 * @param cpu the cpu number
 * @param cwp the window number
 * @return the value of the stack pointer register or 0 if window/cpu is invalid
 */

uint32_t dsu_get_reg_sp(uint32_t cpu, uint32_t cwp)
{
	uint32_t reg;

	reg = dsu_get_output_reg_addr(cpu, 6, cwp);

	if (reg)
		return ioread32((uint32_t) reg);
	else
		return 0;
}


/**
 * @brief  set frame pointer register (%i6) in a window of a cpu
 *
 * @param cpu the cpu number
 * @param cwp the window number
 * @param val the value to set
 */

void dsu_set_reg_fp(uint32_t cpu, uint32_t cwp, uint32_t val)
{
	uint32_t reg;

	reg = dsu_get_input_reg_addr(cpu, 6, cwp);

	if (reg)
		iowrite32((uint32_t) reg, val);
}


/**
 * @brief  get frame pointer register (%i6) in a window of a cpu
 *
 * @param cpu the cpu number
 * @param cwp the window number
 */   
uint32_t dsu_get_reg_fp(uint32_t cpu, uint32_t cwp)
{
	uint32_t reg = dsu_get_input_reg_addr(cpu, 6, cwp);

	return ioread32(reg);
}



/**
 * @brief get the last line_count lines from the instruction trace buffer
 *
 * @param cpu the cpu number
 * @param buffer the buffer to write the lines to
 * @param line_count the number of lines to read (instructions can be multiple lines)
 * @param line_start the number of lines to skip when going backwards
 */
void dsu_get_instr_trace_buffer(uint32_t cpu, struct instr_trace_buffer_line *buffer, uint32_t line_count, uint32_t line_start)
{
	
	DWORD *data = (DWORD*) malloc(DSU_INST_TRCE_BUF_LINE_SIZE * line_count);
	
	uint32_t inst_trce_ctrl_reg = ioread32((uint32_t) DSU_BASE(cpu) + DSU_INST_TRCE_CTRL);
	uint32_t inst_pointer = inst_trce_ctrl_reg & 0xFF;

	/* inst_pointer points next line that will be written
	 * ignore line_start lines
	 * go back line_count lines to read them
	 * this can very likely overflow, that is the reason for the modulo operation
	 */
	uint32_t first_line_to_read = inst_pointer - line_start - line_count;
	uint32_t offset_start = (first_line_to_read * DSU_INST_TRCE_BUF_LINE_SIZE) % DSU_INST_TRCE_BUF_SIZE;
	uint32_t read_size, line_ptr = 0;

	/* if an overflow in the instruction trace would occur read only until the
	 * end of the buffer in the first step
	 */
	if (offset_start + line_count * DSU_INST_TRCE_BUF_LINE_SIZE > DSU_INST_TRCE_BUF_SIZE)
		read_size = DSU_INST_TRCE_BUF_SIZE - offset_start;
	else
		read_size = line_count * DSU_INST_TRCE_BUF_LINE_SIZE;

	/* Read size in dwords */
	ioread32(DSU_BASE(cpu) + DSU_INST_TRCE_BUF_START + offset_start, data, read_size / 4);

	for(uint32_t i = 0; i < read_size / 4; i += 4) {
		buffer[line_ptr].field[0] = data[i + 0];
		buffer[line_ptr].field[1] = data[i + 1];
		buffer[line_ptr].field[2] = data[i + 2];
		buffer[line_ptr].field[3] = data[i + 3];
		line_ptr++;
	}

	/* Read remaining data in case of an overflow in the circular instruction trace buffer  */
	uint32_t remaining_size = line_count * DSU_INST_TRCE_BUF_LINE_SIZE - read_size;
	if (remaining_size > 0) {
		ioread32(DSU_BASE(cpu) + DSU_INST_TRCE_BUF_START, data, remaining_size / 4);
		
		for(uint32_t i = 0; i < remaining_size / 4; i += 4) {
			buffer[line_ptr].field[0] = data[i + 0];
			buffer[line_ptr].field[1] = data[i + 1];
			buffer[line_ptr].field[2] = data[i + 2];
			buffer[line_ptr].field[3] = data[i + 3];
			line_ptr++;
		}
	}

	free(data);
}


void dsu_set_local_reg(uint32_t cpu, uint32_t cwp, uint32_t reg_num, uint32_t value)
{
	uint32_t address = dsu_get_local_reg_addr(cpu, reg_num, cwp);

	iowrite32(address, value);
}


void dsu_set_input_reg(uint32_t cpu, uint32_t cwp, uint32_t reg_num, uint32_t value)
{
	uint32_t address = dsu_get_input_reg_addr(cpu, reg_num, cwp);

	iowrite32(address, value);
}


void dsu_set_output_reg(uint32_t cpu, uint32_t cwp, uint32_t reg_num, uint32_t value)
{
	uint32_t address = dsu_get_output_reg_addr(cpu, reg_num, cwp);

	iowrite32(address, value);
}


void dsu_set_global_reg(uint32_t cpu, uint32_t reg_num, uint32_t value)
{
	uint32_t address = dsu_get_global_reg_addr(cpu, reg_num);

	iowrite32(address, value);
}


void dsu_set_float_reg(uint32_t cpu, uint32_t reg_num, union float_value value)
{
	uint32_t address = DSU_BASE(cpu) + DSU_FPU_REG + reg_num * 4;
	if (reg_num > 31)
		return;

	iowrite32(address, value.u);	
}


void dsu_set_double_reg(uint32_t cpu, uint32_t reg_num, union double_value value)
{
	uint32_t address = DSU_BASE(cpu) + DSU_FPU_REG + 32 * 4 + reg_num * 8;

	if (reg_num > 12)
		return;

	iowrite32(address, (uint32_t)(value.u >> 32));
	iowrite32(address + 4, (uint32_t)(value.u & 0xFFFFFFFF));
}
