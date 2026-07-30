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

#include "obj-coff-seh.h"

static void write_function_xdata (seh_context *);
static void write_function_pdata (seh_context *);

/* Switch back to the code section, whatever that may be.  */
static void
obj_coff_seh_code (int ignored ATTRIBUTE_UNUSED)
{
  subseg_set (seh_ctx_cur->code_seg, 0);
}

/* Parsing routines.  */

/* Return the style of SEH unwind info to generate.  */

static seh_kind
seh_get_target_kind (void)
{
  if (!stdoutput)
    return seh_kind_unknown;

  switch (bfd_get_arch (stdoutput))
    {
    case bfd_arch_aarch64:
      return seh_kind_aarch64;

    case bfd_arch_arm:
    case bfd_arch_powerpc:
    case bfd_arch_sh:
      return seh_kind_arm;

    case bfd_arch_i386:
      switch (bfd_get_mach (stdoutput))
	{
	case bfd_mach_x86_64:
	case bfd_mach_x86_64_intel_syntax:
	  return seh_kind_x64;
	default:
	  break;
	}
      /* FALL THROUGH.  */
    case bfd_arch_mips:
      return seh_kind_mips;

    case bfd_arch_ia64:
      /* Should return seh_kind_x64.  But not implemented yet.  */
      return seh_kind_unknown;

    default:
      break;
    }
  return seh_kind_unknown;
}

/* Verify that seh directives are supported.  */

static bool
verify_target (const char *directive)
{
  if (seh_get_target_kind () == seh_kind_unknown)
    {
      as_warn (_("%s ignored for this target"), directive);
      ignore_rest_of_line ();
      return false;
    }
  return true;
}

/* Similar, except we also verify the appropriate target.  */

static int
verify_context_and_target (const char *directive, seh_kind target)
{
  if (seh_get_target_kind () != target)
    {
      as_warn (_("%s ignored for this target"), directive);
      ignore_rest_of_line ();
      return 0;
    }
  return verify_context (directive);
}

/* Mark current context to use 32-bit instruction (arm).  */

static void
obj_coff_seh_32 (int what)
{
  if (!verify_context_and_target ((what ? ".seh_32" : ".seh_no32"),
				  seh_kind_arm))
    return;

  seh_ctx_cur->use_instruction_32 = (what ? 1 : 0);
  demand_empty_rest_of_line ();
}

/* Set for current context the handler and optional data (arm).  */

static void
obj_coff_seh_eh (int what ATTRIBUTE_UNUSED)
{
  if (!verify_context_and_target (".seh_eh", seh_kind_arm))
    return;

  /* Write block to .text if exception handler is set.  */
  seh_ctx_cur->handler_written = 1;
  emit_expr (&seh_ctx_cur->handler, 4);
  emit_expr (&seh_ctx_cur->handler_data, 4);

  demand_empty_rest_of_line ();
}

/* Set for current context the default handler (x64).  */

static void
obj_coff_seh_handler (int what ATTRIBUTE_UNUSED)
{
  char *symbol_name;
  char name_end;

  if (!verify_target (".seh_handler")
      || !verify_context (".seh_handler"))
    return;

  if (*input_line_pointer == 0 || *input_line_pointer == '\n')
    {
      as_bad (_(".seh_handler requires a handler"));
      demand_empty_rest_of_line ();
      return;
    }

  SKIP_WHITESPACE ();

  if (*input_line_pointer == '@')
    {
      name_end = get_symbol_name (&symbol_name);

      seh_ctx_cur->handler.X_op = O_constant;
      seh_ctx_cur->handler.X_add_number = 0;

      if (strcasecmp (symbol_name, "@0") == 0
	  || strcasecmp (symbol_name, "@null") == 0)
	;
      else if (strcasecmp (symbol_name, "@1") == 0)
	seh_ctx_cur->handler.X_add_number = 1;
      else
	as_bad (_("unknown constant value '%s' for handler"), symbol_name);

      (void) restore_line_pointer (name_end);
    }
  else
    expression (&seh_ctx_cur->handler);

  seh_ctx_cur->handler_data.X_op = O_constant;
  seh_ctx_cur->handler_data.X_add_number = 0;
  seh_ctx_cur->handler_flags = 0;

  if (!skip_whitespace_and_comma (0))
    return;

  if (seh_get_target_kind () == seh_kind_x64
      || seh_get_target_kind () == seh_kind_aarch64)
    {
      do
	{
	  name_end = get_symbol_name (&symbol_name);

	  if (strcasecmp (symbol_name, "@unwind") == 0)
	    seh_ctx_cur->handler_flags |= UNW_FLAG_UHANDLER;
	  else if (strcasecmp (symbol_name, "@except") == 0)
	    seh_ctx_cur->handler_flags |= UNW_FLAG_EHANDLER;
	  else
	    as_bad (_(".seh_handler constant '%s' unknown"), symbol_name);

	  (void) restore_line_pointer (name_end);
	}
      while (skip_whitespace_and_comma (0));
    }
  else
    {
      expression (&seh_ctx_cur->handler_data);
      demand_empty_rest_of_line ();

      if (seh_ctx_cur->handler_written)
	as_warn (_(".seh_handler after .seh_eh is ignored"));
    }
}

/* Switch to subsection for handler data for exception region (x64).  */

static void
obj_coff_seh_handlerdata (int what ATTRIBUTE_UNUSED)
{
  if (!verify_context_and_target (".seh_handlerdata", seh_get_target_kind ()))
    return;
  demand_empty_rest_of_line ();

  switch_xdata (seh_ctx_cur->subsection + 1, seh_ctx_cur->code_seg);
}

/* Mark end of current context.  */

static void
do_seh_endproc (void)
{
  seh_ctx_cur->end_addr = symbol_temp_new_now ();

  write_function_xdata (seh_ctx_cur);
  write_function_pdata (seh_ctx_cur);
  free (seh_ctx_cur->elems);
  free (seh_ctx_cur->func_name);
  free (seh_ctx_cur);
  seh_ctx_cur = NULL;
}

static void
obj_coff_seh_endproc (int what ATTRIBUTE_UNUSED)
{
  if (!verify_target (".seh_endproc"))
    return;
  demand_empty_rest_of_line ();
  if (seh_ctx_cur == NULL)
    {
      as_bad (_(".seh_endproc used without .seh_proc"));
      return;
    }
  seh_validate_seg (".seh_endproc");
  do_seh_endproc ();
}

/* Mark begin of new context.  */

static void
obj_coff_seh_proc (int what ATTRIBUTE_UNUSED)
{
  char *symbol_name;
  char name_end;

  if (!verify_target (".seh_proc"))
    return;
  if (seh_ctx_cur != NULL)
    {
      as_bad (_("previous SEH entry not closed (missing .seh_endproc)"));
      do_seh_endproc ();
    }

  if (*input_line_pointer == 0 || *input_line_pointer == '\n')
    {
      as_bad (_(".seh_proc requires function label name"));
      demand_empty_rest_of_line ();
      return;
    }

  seh_ctx_cur = XCNEW (seh_context);

  seh_ctx_cur->code_seg = now_seg;

  if (seh_get_target_kind () == seh_kind_x64
      || seh_get_target_kind () == seh_kind_aarch64)
    {
      x_segcur = seh_hash_find_or_make (seh_ctx_cur->code_seg, ".xdata");
      seh_ctx_cur->subsection = x_segcur->subseg;
      x_segcur->subseg += 2;
    }

  SKIP_WHITESPACE ();

  name_end = get_symbol_name (&symbol_name);
  seh_ctx_cur->func_name = xstrdup (symbol_name);
  (void) restore_line_pointer (name_end);

  demand_empty_rest_of_line ();

  seh_ctx_cur->start_addr = symbol_temp_new_now ();
}

/* Mark end of prologue for current context.  */

static void
obj_coff_seh_endprologue (int what ATTRIBUTE_UNUSED)
{
  if (!verify_target (".seh_endprologue")
      || !verify_context (".seh_endprologue")
      || !seh_validate_seg (".seh_endprologue"))
    return;
  demand_empty_rest_of_line ();

  if (seh_ctx_cur->endprologue_addr != NULL)
    as_warn (_("duplicate .seh_endprologue in .seh_proc block"));
  else
    seh_ctx_cur->endprologue_addr = symbol_temp_new_now ();
}

/* End-of-file hook.  */

void
obj_coff_seh_do_final (void)
{
  if (seh_ctx_cur != NULL)
    as_bad (_("open SEH entry at end of file (missing .seh_endproc)"));
}

/* Enter a prologue element into current context (x64).  */

static void
seh_x64_make_prologue_element (int code, int info, offsetT off)
{
  seh_prologue_element *n;

  if (seh_ctx_cur == NULL)
    return;
  if (seh_ctx_cur->elems_count == seh_ctx_cur->elems_max)
    {
      seh_ctx_cur->elems_max += 8;
      seh_ctx_cur->elems = XRESIZEVEC (seh_prologue_element,
				       seh_ctx_cur->elems,
				       seh_ctx_cur->elems_max);
    }

  n = &seh_ctx_cur->elems[seh_ctx_cur->elems_count++];
  n->code = code;
  n->info = info;
  n->off = off;
  n->pc_addr = symbol_temp_new_now ();
}

/* Helper to read a register name from input stream (x64).  */

static int
seh_x64_read_reg (const char *directive, int kind)
{
  static const char * const int_regs[16] =
    { "rax", "rcx", "rdx", "rbx", "rsp", "rbp","rsi","rdi",
      "r8","r9","r10","r11","r12","r13","r14","r15" };
  static const char * const xmm_regs[16] =
    { "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
      "xmm8", "xmm9", "xmm10","xmm11","xmm12","xmm13","xmm14","xmm15" };

  const char * const *regs = NULL;
  char name_end;
  char *symbol_name = NULL;
  int i;

  switch (kind)
    {
    case 0:
    case 1:
      regs = int_regs;
      break;
    case 2:
      regs = xmm_regs;
      break;
    default:
      abort ();
    }

  SKIP_WHITESPACE ();
  if (*input_line_pointer == '%')
    ++input_line_pointer;
  name_end = get_symbol_name (& symbol_name);

  for (i = 0; i < 16; i++)
    if (! strcasecmp (regs[i], symbol_name))
      break;

  (void) restore_line_pointer (name_end);

  /* Error if register not found, or EAX used as a frame pointer.  */
  if (i == 16 || (kind == 0 && i == 0))
    {
      as_bad (_("invalid register for %s"), directive);
      return -1;
    }

  return i;
}

/* Add a register push-unwind token to the current context.  */

static void
obj_coff_seh_pushreg (int what ATTRIBUTE_UNUSED)
{
  int reg;

  if (!verify_context_and_target (".seh_pushreg", seh_kind_x64)
      || !seh_validate_seg (".seh_pushreg"))
    return;

  reg = seh_x64_read_reg (".seh_pushreg", 1);
  demand_empty_rest_of_line ();

  if (reg < 0)
    return;

  seh_x64_make_prologue_element (UWOP_PUSH_NONVOL, reg, 0);
}

/* Add a register frame-unwind token to the current context.  */

static void
obj_coff_seh_pushframe (int what ATTRIBUTE_UNUSED)
{
  int code = 0;
  
  if (!verify_context_and_target (".seh_pushframe", seh_kind_x64)
      || !seh_validate_seg (".seh_pushframe"))
    return;
  
  SKIP_WHITESPACE();
  
  if (is_name_beginner (*input_line_pointer))
    {
      char* identifier;

      get_symbol_name (&identifier);
      if (strcmp (identifier, "code") != 0)
	{
	  as_bad(_("invalid argument \"%s\" for .seh_pushframe. Expected \"code\" or nothing"),
		 identifier);
	  return;
	}
      code = 1;
    }
  
  demand_empty_rest_of_line ();

  seh_x64_make_prologue_element (UWOP_PUSH_MACHFRAME, code, 0);
}

/* Add a register save-unwind token to current context.  */

static void
obj_coff_seh_save (int what)
{
  const char *directive = (what == 1 ? ".seh_savereg" : ".seh_savexmm");
  int code, reg, scale;
  offsetT off;

  if (!verify_context_and_target (directive, seh_kind_x64)
      || !seh_validate_seg (directive))
    return;

  reg = seh_x64_read_reg (directive, what);

  if (!skip_whitespace_and_comma (1))
    return;

  off = get_absolute_expression ();
  demand_empty_rest_of_line ();

  if (reg < 0)
    return;
  if (off < 0)
    {
      as_bad (_("%s offset is negative"), directive);
      return;
    }

  scale = (what == 1 ? 8 : 16);

  if ((off & (scale - 1)) == 0 && off <= (offsetT) (0xffff * scale))
    {
      code = (what == 1 ? UWOP_SAVE_NONVOL : UWOP_SAVE_XMM128);
      off /= scale;
    }
  else if (off < (offsetT) 0xffffffff)
    code = (what == 1 ? UWOP_SAVE_NONVOL_FAR : UWOP_SAVE_XMM128_FAR);
  else
    {
      as_bad (_("%s offset out of range"), directive);
      return;
    }

  seh_x64_make_prologue_element (code, reg, off);
}

/* Add a stack-allocation token to current context.  */

static void
obj_coff_seh_stackalloc (int what ATTRIBUTE_UNUSED)
{
  offsetT off;
  int code, info;

  if (!verify_context_and_target (".seh_stackalloc", seh_kind_x64)
      || !seh_validate_seg (".seh_stackalloc"))
    return;

  off = get_absolute_expression ();
  demand_empty_rest_of_line ();

  if (off == 0)
    return;
  if (off < 0)
    {
      as_bad (_(".seh_stackalloc offset is negative"));
      return;
    }

  if ((off & 7) == 0 && off <= 128)
    code = UWOP_ALLOC_SMALL, info = (off - 8) >> 3, off = 0;
  else if ((off & 7) == 0 && off <= (offsetT) (0xffff * 8))
    code = UWOP_ALLOC_LARGE, info = 0, off >>= 3;
  else if (off <= (offsetT) 0xffffffff)
    code = UWOP_ALLOC_LARGE, info = 1;
  else
    {
      as_bad (_(".seh_stackalloc offset out of range"));
      return;
    }

  seh_x64_make_prologue_element (code, info, off);
}

/* Add a frame-pointer token to current context.  */

static void
obj_coff_seh_setframe (int what ATTRIBUTE_UNUSED)
{
  offsetT off;
  int reg;

  if (!verify_context_and_target (".seh_setframe", seh_kind_x64)
      || !seh_validate_seg (".seh_setframe"))
    return;

  reg = seh_x64_read_reg (".seh_setframe", 0);

  if (!skip_whitespace_and_comma (1))
    return;

  off = get_absolute_expression ();
  demand_empty_rest_of_line ();

  if (reg < 0)
    return;
  if (off < 0)
    as_bad (_(".seh_setframe offset is negative"));
  else if (off > 240)
    as_bad (_(".seh_setframe offset out of range"));
  else if (off & 15)
    as_bad (_(".seh_setframe offset not a multiple of 16"));
  else if (seh_ctx_cur->framereg != 0)
    as_bad (_("duplicate .seh_setframe in current .seh_proc"));
  else
    {
      seh_ctx_cur->framereg = reg;
      seh_ctx_cur->frameoff = off;
      seh_x64_make_prologue_element (UWOP_SET_FPREG, 0, 0);
    }
}

/* AArch64 support.  */

/* AArch64 register name tables.  */
static const char * const aarch64_int_regs[31] = {
  "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
  "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
  "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
  "x24", "x25", "x26", "x27", "x28", "x29", "x30"
};

static const char * const aarch64_fp_regs[32] = {
  "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
  "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15",
  "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23",
  "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31"
};

/* Read an AArch64 integer register from input stream.
   Returns the register number (0-30) or -1 on error.
   Also accepts "fp" (x29) and "lr" (x30).  */
static int
seh_aarch64_read_int_reg (const char *directive, int min_reg, int max_reg)
{
  char name_end;
  char *symbol_name;
  int i;

  SKIP_WHITESPACE ();
  name_end = get_symbol_name (&symbol_name);

  /* Check for special names.  */
  if (strcasecmp (symbol_name, "fp") == 0)
    i = 29;
  else if (strcasecmp (symbol_name, "lr") == 0)
    i = 30;
  else
    {
      for (i = 0; i < 31; i++)
	if (!strcasecmp (aarch64_int_regs[i], symbol_name))
	  break;
    }

  (void) restore_line_pointer (name_end);

  if (i > max_reg || i < min_reg)
    {
      as_bad (_("invalid register for %s"), directive);
      return -1;
    }

  return i;
}

/* Read an AArch64 FP/SIMD register from input stream.
   Returns the register number (0-31) or -1 on error.  */
static int
seh_aarch64_read_fp_reg (const char *directive)
{
  char name_end;
  char *symbol_name;
  int i;

  SKIP_WHITESPACE ();
  name_end = get_symbol_name (&symbol_name);

  for (i = 0; i < 32; i++)
    if (!strcasecmp (aarch64_fp_regs[i], symbol_name))
      break;

  (void) restore_line_pointer (name_end);

  if (i == 32)
    {
      as_bad (_("invalid floating-point register for %s"), directive);
      return -1;
    }

  return i;
}

/* Add a prologue element to the AArch64 SEH context.  */
static void
seh_aarch64_make_prologue_element (int code, int info, offsetT off)
{
  seh_prologue_element *n;

  if (seh_ctx_cur == NULL)
    return;
  if (seh_ctx_cur->elems_count == seh_ctx_cur->elems_max)
    {
      seh_ctx_cur->elems_max += 8;
      seh_ctx_cur->elems = XRESIZEVEC (seh_prologue_element,
				       seh_ctx_cur->elems,
				       seh_ctx_cur->elems_max);
    }

  n = &seh_ctx_cur->elems[seh_ctx_cur->elems_count++];
  n->code = code;
  n->info = info;
  n->off = off;
  n->pc_addr = symbol_temp_new_now ();
}

/* .seh_save_regp <reg1>, <reg2>, <offset> (aarch64)
   Save register pair at offset from SP.  */
static void
obj_coff_seh_aarch64_save_regp (int what ATTRIBUTE_UNUSED)
{
  int reg1, reg2;
  offsetT off;

  if (!verify_context_and_target (".seh_save_regp", seh_kind_aarch64)
      || !seh_validate_seg (".seh_save_regp"))
    return;

  reg1 = seh_aarch64_read_int_reg (".seh_save_regp", 0, 30);
  if (!skip_whitespace_and_comma (1))
    return;
  reg2 = seh_aarch64_read_int_reg (".seh_save_regp", 0, 30);
  if (!skip_whitespace_and_comma (1))
    return;
  off = get_absolute_expression ();
  demand_empty_rest_of_line ();

  if (reg1 < 0 || reg2 < 0)
    return;
  if (off < 0 || (off & 7))
    {
      as_bad (_(".seh_save_regp offset must be non-negative and 8-byte aligned"));
      return;
    }

  /* Check for special case: saving x29,x30 (FPLR pair).  */
  if (reg1 == 29 && reg2 == 30)
    {
      if (off <= 0x3F * 8)
	{
	  seh_aarch64_make_prologue_element (AARCH64_UOP_SAVE_FPLR, off >> 3, 0);
	  return;
	}
    }

  seh_aarch64_make_prologue_element (AARCH64_UOP_SAVE_REG_P, reg1, off);
}

/* .seh_save_fregp <reg1>, <reg2>, <offset> (aarch64)
   Save FP/SIMD register pair.  */
static void
obj_coff_seh_aarch64_save_fregp (int what ATTRIBUTE_UNUSED)
{
  int reg1, reg2;
  offsetT off;

  if (!verify_context_and_target (".seh_save_fregp", seh_kind_aarch64)
      || !seh_validate_seg (".seh_save_fregp"))
    return;

  reg1 = seh_aarch64_read_fp_reg (".seh_save_fregp");
  if (!skip_whitespace_and_comma (1))
    return;
  reg2 = seh_aarch64_read_fp_reg (".seh_save_fregp");
  if (!skip_whitespace_and_comma (1))
    return;
  off = get_absolute_expression ();
  demand_empty_rest_of_line ();

  if (reg1 < 0 || reg2 < 0)
    return;
  if (off < 0 || (off & 7))
    {
      as_bad (_(".seh_save_fregp offset must be non-negative and 8-byte aligned"));
      return;
    }

  seh_aarch64_make_prologue_element (AARCH64_UOP_SAVE_FREG_P, reg1, off);
}

/* .seh_save_reg <reg>, <offset> (aarch64)
   Save a single integer register.  */
static void
obj_coff_seh_aarch64_save_reg (int what ATTRIBUTE_UNUSED)
{
  int reg;
  offsetT off;

  if (!verify_context_and_target (".seh_save_reg", seh_kind_aarch64)
      || !seh_validate_seg (".seh_save_reg"))
    return;

  reg = seh_aarch64_read_int_reg (".seh_save_reg", 0, 30);
  if (!skip_whitespace_and_comma (1))
    return;
  off = get_absolute_expression ();
  demand_empty_rest_of_line ();

  if (reg < 0)
    return;
  if (off < 0 || (off & 7))
    {
      as_bad (_(".seh_save_reg offset must be non-negative and 8-byte aligned"));
      return;
    }

  seh_aarch64_make_prologue_element (AARCH64_UOP_SAVE_REG, reg, off);
}

/* .seh_save_freg <reg>, <offset> (aarch64)
   Save a single FP/SIMD register.  */
static void
obj_coff_seh_aarch64_save_freg (int what ATTRIBUTE_UNUSED)
{
  int reg;
  offsetT off;

  if (!verify_context_and_target (".seh_save_freg", seh_kind_aarch64)
      || !seh_validate_seg (".seh_save_freg"))
    return;

  reg = seh_aarch64_read_fp_reg (".seh_save_freg");
  if (!skip_whitespace_and_comma (1))
    return;
  off = get_absolute_expression ();
  demand_empty_rest_of_line ();

  if (reg < 0)
    return;
  if (off < 0 || (off & 7))
    {
      as_bad (_(".seh_save_freg offset must be non-negative and 8-byte aligned"));
      return;
    }

  seh_aarch64_make_prologue_element (AARCH64_UOP_SAVE_FREG, reg, off);
}

/* .seh_save_fplr <offset> (aarch64)
   Save x29 (FP) and x30 (LR) at a positive offset from SP.  */
static void
obj_coff_seh_aarch64_save_fplr (int what ATTRIBUTE_UNUSED)
{
  offsetT off;

  if (!verify_context_and_target (".seh_save_fplr", seh_kind_aarch64)
      || !seh_validate_seg (".seh_save_fplr"))
    return;

  off = get_absolute_expression ();
  demand_empty_rest_of_line ();

  if (off < 0 || (off & 7))
    {
      as_bad (_(".seh_save_fplr offset must be non-negative and 8-byte aligned"));
      return;
    }

  seh_aarch64_make_prologue_element (AARCH64_UOP_SAVE_FPLR, off >> 3, 0);
}

/* .seh_save_fplr_x <offset> (aarch64)
   Save x29 (FP) and x30 (LR) with predecrement (stp x29, x30, [sp, #-N]!).  */
static void
obj_coff_seh_aarch64_save_fplr_x (int what ATTRIBUTE_UNUSED)
{
  offsetT off;

  if (!verify_context_and_target (".seh_save_fplr_x", seh_kind_aarch64)
      || !seh_validate_seg (".seh_save_fplr_x"))
    return;

  off = get_absolute_expression ();
  demand_empty_rest_of_line ();

  if (off < 0 || (off & 15))
    {
      as_bad (_(".seh_save_fplr_x offset must be negative and 16-byte aligned"));
      return;
    }
  if (off > 0x3F * 8)
    {
      as_bad (_(".seh_save_fplr_x offset out of range (max 504)"));
      return;
    }

  seh_aarch64_make_prologue_element (AARCH64_UOP_SAVE_FPLRX, (off >> 3) - 1, 0);
}

/* .seh_save_lrpair <reg>, <offset> (aarch64)
   Save x30 (LR) and another register at offset from SP.  */
static void
obj_coff_seh_aarch64_save_lrpair (int what ATTRIBUTE_UNUSED)
{
  int reg;
  offsetT off;

  if (!verify_context_and_target (".seh_save_lrpair", seh_kind_aarch64)
      || !seh_validate_seg (".seh_save_lrpair"))
    return;

  reg = seh_aarch64_read_int_reg (".seh_save_lrpair", 0, 28);
  if (!skip_whitespace_and_comma (1))
    return;
  off = get_absolute_expression ();
  demand_empty_rest_of_line ();

  if (reg < 0)
    return;
  if (off < 0 || (off & 7))
    {
      as_bad (_(".seh_save_lrpair offset must be non-negative and 8-byte aligned"));
      return;
    }

  seh_aarch64_make_prologue_element (AARCH64_UOP_SAVE_LRPAIR, (reg - 19) >> 1, off >> 3);
}

/* .seh_set_fp (aarch64)
   Set frame pointer (mov x29, sp).  */
static void
obj_coff_seh_aarch64_set_fp (int what ATTRIBUTE_UNUSED)
{
  if (!verify_context_and_target (".seh_set_fp", seh_kind_aarch64)
      || !seh_validate_seg (".seh_set_fp"))
    return;
  demand_empty_rest_of_line ();

  seh_aarch64_make_prologue_element (AARCH64_UOP_SET_FP, 0, 0);
}

/* .seh_add_fp <offset> (aarch64)
   Add offset to frame pointer (add x29, sp, #N).  */
static void
obj_coff_seh_aarch64_add_fp (int what ATTRIBUTE_UNUSED)
{
  offsetT off;

  if (!verify_context_and_target (".seh_add_fp", seh_kind_aarch64)
      || !seh_validate_seg (".seh_add_fp"))
    return;

  off = get_absolute_expression ();
  demand_empty_rest_of_line ();

  seh_aarch64_make_prologue_element (AARCH64_UOP_ADD_FP, off >> 3, 0);
}

/* .seh_nop (aarch64)
   No-op padding in the unwind code.  */
static void
obj_coff_seh_aarch64_nop (int what ATTRIBUTE_UNUSED)
{
  if (!verify_context_and_target (".seh_nop", seh_kind_aarch64)
      || !seh_validate_seg (".seh_nop"))
    return;
  demand_empty_rest_of_line ();

  seh_aarch64_make_prologue_element (AARCH64_UOP_NOP, 0, 0);
}

/* .seh_alloc_stack <size> (aarch64)
   Allocate stack space.  */
static void
obj_coff_seh_aarch64_alloc_stack (int what ATTRIBUTE_UNUSED)
{
  offsetT off;
  int code, info;

  if (!verify_context_and_target (".seh_alloc_stack", seh_kind_aarch64)
      || !seh_validate_seg (".seh_alloc_stack"))
    return;

  off = get_absolute_expression ();
  demand_empty_rest_of_line ();

  if (off == 0)
    return;
  if (off < 0)
    {
      as_bad (_(".seh_alloc_stack offset is negative"));
      return;
    }

  if ((off & 15) == 0 && off <= 0x1F * 16)
    {
      code = AARCH64_UOP_ALLOC_SMALL;
      info = (off / 16) - 1;
    }
  else if ((off & 15) == 0 && off <= 0x7FF * 16)
    {
      code = AARCH64_UOP_ALLOC_MEDIUM;
      info = off / 16;
    }
  else if ((off & 15) == 0 && off <= (offsetT) 0xFFFFFF * 16)
    {
      code = AARCH64_UOP_ALLOC_LARGE;
      info = 0;
      off = off / 16;
    }
  else
    {
      as_bad (_(".seh_alloc_stack offset out of range"));
      return;
    }

  seh_aarch64_make_prologue_element (code, info, off);
}

/* AArch64 xdata writing.  */

/* Count the size of AArch64 unwind code data in bytes.  */
static int
seh_aarch64_size_prologue_data (const seh_context *c)
{
  int i, ret = 0;

  for (i = c->elems_count - 1; i >= 0; --i)
    {
      int code = c->elems[i].code;
      if (code == AARCH64_UOP_ALLOC_SMALL
	  || code == AARCH64_UOP_SAVE_R19R20X
	  || code == AARCH64_UOP_SAVE_FPLRX
	  || code == AARCH64_UOP_SAVE_FPLR
	  || code == AARCH64_UOP_SET_FP
	  || code == AARCH64_UOP_NOP
	  || code == AARCH64_UOP_END
	  || code == AARCH64_UOP_SAVE_NEXT
	  || code == AARCH64_UOP_TRAP_FRAME
	  || code == AARCH64_UOP_PUSH_MACH
	  || code == AARCH64_UOP_CONTEXT
	  || code == AARCH64_UOP_EC_CONTEXT
	  || code == AARCH64_UOP_CLEAR_UNWOUND_TO_CALL
	  || code == AARCH64_UOP_PAC_SIGN_LR)
	ret += 1;
      else if (code == AARCH64_UOP_ALLOC_MEDIUM
	       || code == AARCH64_UOP_SAVE_REG
	       || code == AARCH64_UOP_SAVE_REG_X
	       || code == AARCH64_UOP_SAVE_REG_P
	       || code == AARCH64_UOP_SAVE_REG_PX
	       || code == AARCH64_UOP_SAVE_LRPAIR
	       || code == AARCH64_UOP_SAVE_FREG
	       || code == AARCH64_UOP_SAVE_FREG_X
	       || code == AARCH64_UOP_SAVE_FREG_P
	       || code == AARCH64_UOP_SAVE_FREG_PX
	       || code == AARCH64_UOP_ADD_FP)
	ret += 2;
      else if (code == AARCH64_UOP_ALLOC_LARGE
	       || code == AARCH64_UOP_SAVE_ANY_REG_I
	       || code == AARCH64_UOP_SAVE_ANY_REG_IP
	       || code == AARCH64_UOP_SAVE_ANY_REG_D
	       || code == AARCH64_UOP_SAVE_ANY_REG_DP
	       || code == AARCH64_UOP_SAVE_ANY_REG_Q
	       || code == AARCH64_UOP_SAVE_ANY_REG_QP)
	ret += 3;
      else
	abort ();
    }

  return ret;
}

/* Write out AArch64 prologue unwind codes.  */
static void
seh_aarch64_write_prologue_data (const seh_context *c)
{
  int i;

  /* We have to store in reverse order.  */
  for (i = c->elems_count - 1; i >= 0; --i)
    {
      const seh_prologue_element *e = c->elems + i;
      expressionS exp;

      /* Offset in code (in bytes).  */
      exp.X_op = O_subtract;
      exp.X_add_symbol = e->pc_addr;
      exp.X_op_symbol = c->start_addr;
      exp.X_add_number = 0;
      emit_expr (&exp, 1);

      switch (e->code)
	{
	case AARCH64_UOP_ALLOC_SMALL:
	  out_one (AARCH64_UOP_ALLOC_SMALL | (e->info & 0x1f));
	  break;

	case AARCH64_UOP_ALLOC_MEDIUM:
	  out_one (AARCH64_UOP_ALLOC_MEDIUM | ((e->info >> 8) & 3));
	  out_one (e->info & 0xff);
	  break;

	case AARCH64_UOP_ALLOC_LARGE:
	  out_one (AARCH64_UOP_ALLOC_LARGE);
	  out_two (e->off);
	  break;

	case AARCH64_UOP_SAVE_R19R20X:
	  out_one (AARCH64_UOP_SAVE_R19R20X | (e->info & 0x1f));
	  break;

	case AARCH64_UOP_SAVE_FPLRX:
	  out_one (AARCH64_UOP_SAVE_FPLRX | (e->info & 0x3f));
	  break;

	case AARCH64_UOP_SAVE_FPLR:
	  out_one (AARCH64_UOP_SAVE_FPLR | (e->info & 0x3f));
	  break;

	case AARCH64_UOP_SAVE_REG:
	  out_one (AARCH64_UOP_SAVE_REG);
	  out_one ((e->info << 4) | (e->off & 0x7f));
	  break;

	case AARCH64_UOP_SAVE_REG_X:
	  out_one (AARCH64_UOP_SAVE_REG_X);
	  out_one ((e->info << 4) | (e->off & 0x7f));
	  break;

	case AARCH64_UOP_SAVE_REG_P:
	  if (e->info >= 19)
	    {
	      int r = e->info - 19;
	      if (r % 2 == 0 && (e->off & 7) == 0 && e->off <= 0x7F * 8)
		{
		  out_one (AARCH64_UOP_SAVE_REG_P | (r >> 1));
		  out_one (e->off >> 3);
		}
	      else
		{
		  out_one (AARCH64_UOP_SAVE_REG_P);
		  out_one ((e->info << 4) | (e->off & 0x7f));
		}
	    }
	  else
	    {
	      out_one (AARCH64_UOP_SAVE_REG_P);
	      out_one ((e->info << 4) | (e->off & 0x7f));
	    }
	  break;

	case AARCH64_UOP_SAVE_REG_PX:
	  out_one (AARCH64_UOP_SAVE_REG_PX);
	  out_one ((e->info << 4) | (e->off & 0x7f));
	  break;

	case AARCH64_UOP_SAVE_LRPAIR:
	  out_one (AARCH64_UOP_SAVE_LRPAIR);
	  out_one ((e->info << 4) | (e->off & 0x0f));
	  break;

	case AARCH64_UOP_SAVE_FREG:
	  out_one (AARCH64_UOP_SAVE_FREG);
	  out_one ((e->info << 4) | (e->off & 0x7f));
	  break;

	case AARCH64_UOP_SAVE_FREG_X:
	  out_one (AARCH64_UOP_SAVE_FREG_X);
	  out_one ((e->info << 4) | (e->off & 0x7f));
	  break;

	case AARCH64_UOP_SAVE_FREG_P:
	  out_one (AARCH64_UOP_SAVE_FREG_P);
	  out_one ((e->info << 4) | (e->off & 0x7f));
	  break;

	case AARCH64_UOP_SAVE_FREG_PX:
	  out_one (AARCH64_UOP_SAVE_FREG_PX);
	  out_one ((e->info << 4) | (e->off & 0x7f));
	  break;

	case AARCH64_UOP_SET_FP:
	case AARCH64_UOP_NOP:
	case AARCH64_UOP_END:
	case AARCH64_UOP_SAVE_NEXT:
	case AARCH64_UOP_TRAP_FRAME:
	case AARCH64_UOP_PUSH_MACH:
	case AARCH64_UOP_CONTEXT:
	case AARCH64_UOP_EC_CONTEXT:
	case AARCH64_UOP_CLEAR_UNWOUND_TO_CALL:
	case AARCH64_UOP_PAC_SIGN_LR:
	  out_one (e->code);
	  break;

	case AARCH64_UOP_ADD_FP:
	  out_one (AARCH64_UOP_ADD_FP);
	  out_one (e->info & 0xff);
	  break;

	default:
	  abort ();
	}
    }

  /* Terminate with END opcode.  */
  out_one (AARCH64_UOP_END);
}

/* Write the xdata for one AArch64 function.  */
static void
seh_aarch64_write_function_xdata (seh_context *c)
{
  int code_words, epilog_count;
  expressionS exp;
  unsigned int func_length;

  /* 4-byte alignment.  */
  frag_align (2, 0, 0);

  c->xdata_addr = symbol_temp_new_now ();

  /* Calculate function length in 4-byte units.  */
  exp.X_op = O_subtract;
  exp.X_add_symbol = c->end_addr;
  exp.X_op_symbol = c->start_addr;
  exp.X_add_number = 0;
  if (resolve_expression (&exp) && exp.X_op == O_constant)
    func_length = exp.X_add_number >> 2;
  else
    func_length = 0;

  /* Count unwind code bytes, including the terminating END.  */
  int code_bytes = seh_aarch64_size_prologue_data (c) + 1;
  code_words = (code_bytes + 3) / 4;

  /* Header word:
     bits [1:0] = Version (0)
     bits [4:2] = Function Length (high bits, 3 bits)
     bit  5    = Exception Handler Present (X)
     bit  6    = Epilogues Present (E)
     bits [8:7] = Code Words (2 bits)
     bit  9    = Extended Code Words
     bits [17:10] = Epilog Count (if E=1, else 0)
     bits [31:18] = Function Length (low 14 bits)
  */
  epilog_count = 0;
  unsigned int header = (func_length << 18);
  if (code_words > 3)
    header |= (1 << 9) | ((code_words >> 2) << 10);
  else
    header |= (code_words << 7);

  if (c->handler_flags & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER))
    header |= (1 << 5);

  out_four (header);

  /* If extended code words needed, emit extension word.  */
  if (code_words > 3)
    {
      /* Extended header: [CodeWords:8][EpilogCount:16][EpilogStart:8].  */
      unsigned int ext = (code_words & 0xff) | (epilog_count << 8);
      out_four (ext);
    }

  /* Write epilogue scopes (none for simple functions).  */

  /* Write prologue unwind codes.  */
  int prologue_start = (ftell (stdout) < 0 ? 0 : 0); /* track position */
  seh_aarch64_write_prologue_data (c);

  /* Pad to 4-byte alignment.  */
  int remainder = (code_bytes) & 3;
  if (remainder)
    for (int i = 0; i < 4 - remainder; i++)
      out_one (AARCH64_UOP_NOP);

  /* If exception handler present, emit it.  */
  if (c->handler_flags & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER))
    {
      if (c->handler.X_op == O_symbol)
	c->handler.X_op = O_symbol_rva;
      emit_expr (&c->handler, 4);
    }

  /* Handler data follows in subsections.  */
}

/* Write out xdata for one function.  */

static void
write_function_xdata (seh_context *c)
{
  segT save_seg = now_seg;
  int save_subseg = now_subseg;

  if (seh_get_target_kind () == seh_kind_x64)
    {
      switch_xdata (c->subsection, c->code_seg);
      seh_x64_write_function_xdata (c);
    }
  else if (seh_get_target_kind () == seh_kind_aarch64)
    {
      switch_xdata (c->subsection, c->code_seg);
      seh_aarch64_write_function_xdata (c);
    }

  subseg_set (save_seg, save_subseg);
}



/* Write out pdata for one function.  */

static void
write_function_pdata (seh_context *c)
{
  expressionS exp;
  segT save_seg = now_seg;
  int save_subseg = now_subseg;
  memset (&exp, 0, sizeof (expressionS));
  switch_pdata (c->code_seg);

  switch (seh_get_target_kind ())
    {
    case seh_kind_x64:
      exp.X_op = O_symbol_rva;
      exp.X_add_number = 0;

      exp.X_add_symbol = c->start_addr;
      emit_expr (&exp, 4);
      exp.X_op = O_symbol_rva;
      exp.X_add_number = 0;
      exp.X_add_symbol = c->end_addr;
      emit_expr (&exp, 4);
      exp.X_op = O_symbol_rva;
      exp.X_add_number = 0;
      exp.X_add_symbol = c->xdata_addr;
      emit_expr (&exp, 4);
      break;

    case seh_kind_mips:
      exp.X_op = O_symbol;
      exp.X_add_number = 0;

      exp.X_add_symbol = c->start_addr;
      emit_expr (&exp, 4);
      exp.X_add_symbol = c->end_addr;
      emit_expr (&exp, 4);

      emit_expr (&c->handler, 4);
      emit_expr (&c->handler_data, 4);

      exp.X_add_symbol = (c->endprologue_addr
			  ? c->endprologue_addr
			  : c->start_addr);
      emit_expr (&exp, 4);
      break;

    case seh_kind_arm:
      seh_arm_write_function_pdata (c);
      break;

    case seh_kind_aarch64:
      exp.X_op = O_symbol_rva;
      exp.X_add_number = 0;
      exp.X_add_symbol = c->start_addr;
      emit_expr (&exp, 4);
      exp.X_op = O_symbol_rva;
      exp.X_add_number = 0;
      exp.X_add_symbol = c->xdata_addr;
      emit_expr (&exp, 4);
      break;

    default:
      abort ();
    }

  subseg_set (save_seg, save_subseg);
}
