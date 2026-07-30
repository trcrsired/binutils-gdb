/* seh pdata/xdata coff object file format
   Copyright (C) 2009-2026 Free Software Foundation, Inc.

   This file is part of GAS.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* Short overview:
  There are at the moment three different function entry formats preset.
  The first is the MIPS one. The second version
  is for ARM, PPC, SH3, and SH4 mainly for Windows CE.
  The third is the IA64 and x64 version. Note, the IA64 isn't implemented yet,
  but to find information about it, please see specification about IA64 on
  http://download.intel.com/design/Itanium/Downloads/245358.pdf file.
  The fourth is for AArch64 (ARM64) Windows, which uses the same pdata/xdata
  model as x64 but with different unwind codes and a 2-word pdata entry.

  The first version has just entries in the pdata section: BeginAddress,
  EndAddress, ExceptionHandler, HandlerData, and PrologueEndAddress. Each
  value is a pointer to the corresponding data and has size of 4 bytes.

  The second variant has the following entries in the pdata section.
  BeginAddress, PrologueLength (8 bits), EndAddress (22 bits),
  Use-32-bit-instruction (1 bit), and Exception-Handler-Exists (1 bit).
  If the FunctionLength is zero, or the Exception-Handler-Exists bit
  is true, a PDATA_EH block is placed directly before function entry.

  The third version has a function entry block of BeginAddress (RVA),
  EndAddress (RVA), and UnwindData (RVA). The description of the
  prologue, exception-handler, and additional SEH data is stored
  within the UNWIND_DATA field in the xdata section.

  The fourth (AArch64/ARM64) version has a 2-word pdata entry:
  BeginAddress (RVA) and UnwindData (RVA), with xdata using ARM64-specific
  unwind codes.

  The pseudos:
  .seh_proc <fct_name>
  .seh_endprologue
  .seh_handler <handler>[,@unwind][,@except]	(x64, aarch64)
  .seh_handler <handler>[,<handler_data>]	(others)
  .seh_handlerdata
  .seh_eh
  .seh_32/.seh_no32
  .seh_endproc
  .seh_setframe <reg>,<offset>
  .seh_stackalloc
  .seh_pushreg
  .seh_savereg
  .seh_savexmm
  .seh_pushframe
  .seh_code
  .seh_save_regp <reg1>,<reg2>,<offset>		(aarch64)
  .seh_save_fregp <reg1>,<reg2>,<offset>	(aarch64)
  .seh_save_reg <reg>,<offset>			(aarch64)
  .seh_save_freg <reg>,<offset>			(aarch64)
  .seh_save_fplr <offset>			(aarch64)
  .seh_save_fplr_x <offset>			(aarch64)
  .seh_save_lrpair <reg>,<offset>		(aarch64)
  .seh_set_fp					(aarch64)
  .seh_add_fp <offset>				(aarch64)
  .seh_nop					(aarch64)
  .seh_alloc_stack <size>			(aarch64)
*/

#ifndef OBJ_COFF_SEH_H
#define OBJ_COFF_SEH_H

/* architecture specific pdata/xdata handling.  */
#define SEH_CMDS \
        {"seh_proc", obj_coff_seh_proc, 0}, \
        {"seh_endproc", obj_coff_seh_endproc, 0}, \
        {"seh_pushreg", obj_coff_seh_pushreg, 0}, \
        {"seh_savereg", obj_coff_seh_save, 1}, \
        {"seh_savexmm", obj_coff_seh_save, 2}, \
        {"seh_pushframe", obj_coff_seh_pushframe, 0}, \
        {"seh_endprologue", obj_coff_seh_endprologue, 0}, \
        {"seh_setframe", obj_coff_seh_setframe, 0}, \
        {"seh_stackalloc", obj_coff_seh_stackalloc, 0}, \
	{"seh_eh", obj_coff_seh_eh, 0}, \
	{"seh_32", obj_coff_seh_32, 1}, \
	{"seh_no32", obj_coff_seh_32, 0}, \
	{"seh_handler", obj_coff_seh_handler, 0}, \
	{"seh_code", obj_coff_seh_code, 0}, \
	{"seh_handlerdata", obj_coff_seh_handlerdata, 0}, \
	{"seh_save_regp", obj_coff_seh_aarch64_save_regp, 0}, \
	{"seh_save_fregp", obj_coff_seh_aarch64_save_fregp, 0}, \
	{"seh_save_reg", obj_coff_seh_aarch64_save_reg, 0}, \
	{"seh_save_freg", obj_coff_seh_aarch64_save_freg, 0}, \
	{"seh_save_fplr", obj_coff_seh_aarch64_save_fplr, 0}, \
	{"seh_save_fplr_x", obj_coff_seh_aarch64_save_fplr_x, 0}, \
	{"seh_save_lrpair", obj_coff_seh_aarch64_save_lrpair, 0}, \
	{"seh_set_fp", obj_coff_seh_aarch64_set_fp, 0}, \
	{"seh_add_fp", obj_coff_seh_aarch64_add_fp, 0}, \
	{"seh_nop", obj_coff_seh_aarch64_nop, 0}, \
	{"seh_alloc_stack", obj_coff_seh_aarch64_alloc_stack, 0},

/* Type definitions.  */

typedef struct seh_prologue_element
{
  int code;
  int info;
  offsetT off;
  symbolS *pc_addr;
} seh_prologue_element;

typedef struct seh_context
{
  /* Initial code-segment.  */
  segT code_seg;
  /* Function name.  */
  char *func_name;
  /* BeginAddress.  */
  symbolS *start_addr;
  /* EndAddress.  */
  symbolS *end_addr;
  /* Unwind data.  */
  symbolS *xdata_addr;
  /* PrologueEnd.  */
  symbolS *endprologue_addr;
  /* ExceptionHandler.  */
  expressionS handler;
  /* ExceptionHandlerData. (arm, mips)  */
  expressionS handler_data;

  /* ARM .seh_eh directive seen.  */
  int handler_written;

  /* WinCE specific data.  */
  int use_instruction_32;
  /* Was record already processed.  */
  int done;

  /* x64 flags for the xdata header.  */
  int handler_flags;
  int subsection;

  /* x64 framereg and frame offset information.  */
  int framereg;
  int frameoff;

  /* Information about x64 specific unwind data fields.  */
  int elems_count;
  int elems_max;
  seh_prologue_element *elems;
} seh_context;

typedef enum seh_kind {
  seh_kind_unknown = 0,
  seh_kind_mips = 1,  /* Used for MIPS and x86 pdata generation.  */
  seh_kind_arm = 2,   /* Used for ARM, PPC, SH3, and SH4 pdata (PDATA_EH) generation.  */
  seh_kind_x64 = 3,   /* Used for IA64 and x64 pdata/xdata generation.  */
  seh_kind_aarch64 = 4 /* Used for AArch64 (ARM64) Windows pdata/xdata generation.  */
} seh_kind;

/* AArch64 unwind opcodes.  */
#define AARCH64_UOP_ALLOC_SMALL  0x00
#define AARCH64_UOP_ALLOC_MEDIUM 0xC0
#define AARCH64_UOP_ALLOC_LARGE  0xE0
#define AARCH64_UOP_SAVE_R19R20X 0x20
#define AARCH64_UOP_SAVE_FPLRX   0x80
#define AARCH64_UOP_SAVE_FPLR    0x40
#define AARCH64_UOP_SAVE_REG     0xD0
#define AARCH64_UOP_SAVE_REG_X   0xD4
#define AARCH64_UOP_SAVE_REG_P   0xC8
#define AARCH64_UOP_SAVE_REG_PX  0xCC
#define AARCH64_UOP_SAVE_LRPAIR  0xD6
#define AARCH64_UOP_SAVE_FREG    0xDC
#define AARCH64_UOP_SAVE_FREG_X  0xDE
#define AARCH64_UOP_SAVE_FREG_P  0xD8
#define AARCH64_UOP_SAVE_FREG_PX 0xDA
#define AARCH64_UOP_SET_FP       0xE1
#define AARCH64_UOP_ADD_FP       0xE2
#define AARCH64_UOP_NOP          0xE3
#define AARCH64_UOP_END          0xE4
#define AARCH64_UOP_SAVE_NEXT    0xE6
#define AARCH64_UOP_TRAP_FRAME   0xE8
#define AARCH64_UOP_PUSH_MACH    0xE9
#define AARCH64_UOP_CONTEXT      0xEA
#define AARCH64_UOP_EC_CONTEXT   0xEB
#define AARCH64_UOP_CLEAR_UNWOUND_TO_CALL 0xEC
#define AARCH64_UOP_PAC_SIGN_LR  0xFC
#define AARCH64_UOP_SAVE_ANY_REG_I   0xE7
#define AARCH64_UOP_SAVE_ANY_REG_IP  0xE7
#define AARCH64_UOP_SAVE_ANY_REG_D   0xE7
#define AARCH64_UOP_SAVE_ANY_REG_DP  0xE7
#define AARCH64_UOP_SAVE_ANY_REG_Q   0xE7
#define AARCH64_UOP_SAVE_ANY_REG_QP  0xE7

/* Forward declarations.  */
static void obj_coff_seh_stackalloc (int);
static void obj_coff_seh_setframe (int);
static void obj_coff_seh_endprologue (int);
static void obj_coff_seh_save (int);
static void obj_coff_seh_pushreg (int);
static void obj_coff_seh_pushframe (int);
static void obj_coff_seh_endproc  (int);
static void obj_coff_seh_eh (int);
static void obj_coff_seh_32 (int);
static void obj_coff_seh_proc  (int);
static void obj_coff_seh_handler (int);
static void obj_coff_seh_handlerdata (int);
static void obj_coff_seh_code (int);
static void obj_coff_seh_aarch64_save_regp (int);
static void obj_coff_seh_aarch64_save_fregp (int);
static void obj_coff_seh_aarch64_save_reg (int);
static void obj_coff_seh_aarch64_save_freg (int);
static void obj_coff_seh_aarch64_save_fplr (int);
static void obj_coff_seh_aarch64_save_fplr_x (int);
static void obj_coff_seh_aarch64_save_lrpair (int);
static void obj_coff_seh_aarch64_set_fp (int);
static void obj_coff_seh_aarch64_add_fp (int);
static void obj_coff_seh_aarch64_nop (int);
static void obj_coff_seh_aarch64_alloc_stack (int);

#define UNDSEC bfd_und_section_ptr

/* Check if x64 UNW_... macros are already defined.  */
#ifndef PEX64_FLAG_NHANDLER
/* We can't include here coff/pe.h header. So we have to copy macros
   from coff/pe.h here.  */
#define PEX64_UNWCODE_CODE(VAL) ((VAL) & 0xf)
#define PEX64_UNWCODE_INFO(VAL) (((VAL) >> 4) & 0xf)

/* The unwind info.  */
#define UNW_FLAG_NHANDLER     0
#define UNW_FLAG_EHANDLER     1
#define UNW_FLAG_UHANDLER     2
#define UNW_FLAG_FHANDLER     3
#define UNW_FLAG_CHAININFO    4

#define UNW_FLAG_MASK         0x1f

/* The unwind codes.  */
#define UWOP_PUSH_NONVOL      0
#define UWOP_ALLOC_LARGE      1
#define UWOP_ALLOC_SMALL      2
#define UWOP_SET_FPREG        3
#define UWOP_SAVE_NONVOL      4
#define UWOP_SAVE_NONVOL_FAR  5
#define UWOP_SAVE_XMM         6
#define UWOP_SAVE_XMM_FAR     7
#define UWOP_SAVE_XMM128      8
#define UWOP_SAVE_XMM128_FAR  9
#define UWOP_PUSH_MACHFRAME   10

#define PEX64_UWI_VERSION(VAL)  ((VAL) & 7)
#define PEX64_UWI_FLAGS(VAL)    (((VAL) >> 3) & 0x1f)
#define PEX64_UWI_FRAMEREG(VAL) ((VAL) & 0xf)
#define PEX64_UWI_FRAMEOFF(VAL) (((VAL) >> 4) & 0xf)
#define PEX64_UWI_SIZEOF_UWCODE_ARRAY(VAL) \
  ((((VAL) + 1) & ~1) * 2)

#define PEX64_OFFSET_TO_UNWIND_CODE 0x4

#define PEX64_OFFSET_TO_HANDLER_RVA (COUNTOFUNWINDCODES) \
  (PEX64_OFFSET_TO_UNWIND_CODE + \
   PEX64_UWI_SIZEOF_UWCODE_ARRAY(COUNTOFUNWINDCODES))

#define PEX64_OFFSET_TO_SCOPE_COUNT(COUNTOFUNWINDCODES) \
  (PEX64_OFFSET_TO_HANDLER_RVA(COUNTOFUNWINDCODES) + 4)

#define PEX64_SCOPE_ENTRY(COUNTOFUNWINDCODES, IDX) \
  (PEX64_OFFSET_TO_SCOPE_COUNT(COUNTOFUNWINDCODES) + \
   PEX64_SCOPE_ENTRY_SIZE * (IDX))

#endif

#endif /* OBJ_COFF_SEH_H */
