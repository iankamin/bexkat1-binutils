/* bexkat1-dis.c -- Bexkat1 disassembly
   Copyright (C) 1999-2014 Free Software Foundation, Inc.
   Written by Matt Stock (stock@bexkat.com)

   This file is part of the GNU opcodes library.

   This library is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   It is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#include "sysdep.h"
#include <stdio.h>
#include <stdint.h>

#define STATIC_TABLE
#define DEFINE_TABLE

#include "opcode/bexkat1.h"
#include "dis-asm.h"

static fprintf_ftype fpr;
static void *stream;

/* Prototypes for local functions.  */
int print_insn_bexkat1 (bfd_vma, struct disassemble_info *);

static const bexkat1_opc_info_t *
find_opcode(unsigned mode, uint8_t opcode) {
  int i;
  for (i=0; i < bexkat1_opc_count; i++)
    if (bexkat1_opc_info[i].mode == mode &&
	bexkat1_opc_info[i].opcode == opcode)
      return &bexkat1_opc_info[i];
  return NULL;
}

static char *print_reg_name(int regno) {
  static char *list[] = { "%0", "%1", "%2", "%3",
			  "%4", "%5", "%6", "%7",
			  "%8", "%9", "%10", "%11",
			  "%12", "%13", "%fp", "%sp",
			  "%16", "%17", "%18", "%19",
			  "%20", "%21", "%22", "%23",
			  "%24", "%25", "%26", "%27",
			  "%28", "%29", "%30", "%31" };
  
  return list[regno];
}

/* Disassemble one instruction at address 'memaddr'.  Returns the number
   of bytes used by that instruction.  */
int print_insn_bexkat1 (bfd_vma memaddr, struct disassemble_info* info) {
  int status;
  int length = 4;
  const bexkat1_opc_info_t *opcode;
  bfd_byte buffer[6];
  unsigned int iword;
  int imm32;
  int offset;

  stream = info->stream;
  fpr = info->fprintf_func; 

  if ((status = info->read_memory_func(memaddr, buffer, 4, info)))
    goto fail;
  if (info->endian == BFD_ENDIAN_BIG)
    iword = bfd_getb32(buffer);
  else
    iword = bfd_getl32(buffer);    

  opcode = NULL;
  switch (iword >> 30) {
  case BEXKAT1_REG:
    opcode = find_opcode((iword >> 30), (iword >> 23) & 0x7f);
    break;
  case BEXKAT1_IMM:
    opcode = find_opcode((iword >> 30), (iword >> 26) & 0xf);
    break;
  case BEXKAT1_REGIND:
    opcode = find_opcode((iword >> 30), (iword >> 26) & 0xf);
    break;
  case BEXKAT1_DIR:
    opcode = find_opcode((iword >> 30), (iword >> 24) & 0x3f);
    break;
  }

  if (opcode == NULL)
    abort();

  switch (opcode->mode) {
  case BEXKAT1_REG:
    if (opcode->args == 0)
      fpr(stream, "%s", opcode->name);
    if (opcode->args == 1)
      fpr(stream, "%s %s", opcode->name, print_reg_name((iword >> 16) & 0xf));
    if (opcode->args == 2)
      fpr(stream, "%s %s, %s", opcode->name,
	  print_reg_name((iword >> 16) & 0xf),
	  print_reg_name((iword >> 12) & 0xf));
    if (opcode->args == 3)
      fpr(stream, "%s %s, %s, %s", opcode->name,
	  print_reg_name((iword >> 16) & 0xf),
	  print_reg_name((iword >> 12) & 0xf),
	  print_reg_name((iword >> 8) & 0xf));
    length = 4;
    break;
  case BEXKAT1_DIR:
    offset = 0;
    imm32 = 0;
    if (opcode->opcode == 0x30 || opcode->opcode == 0x31 || opcode->opcode & 0x10) {
      if ((status = info->read_memory_func(memaddr+4, buffer, 4, info)))
        goto fail;
      if (info->endian == BFD_ENDIAN_BIG)
        imm32 = bfd_getb32(buffer);
      else
        imm32 = bfd_getl32(buffer);
      length = 8;
    } else {
      offset = ((iword >> 8) & 0xf000) | (iword & 0xfff);
      if (offset & 0x00008000)
        offset |=  0xffff0000;
      length = 4;
    }

    if (opcode->args == 1) {
      fpr(stream, "%s ", opcode->name);
      if (opcode->opcode == 0x32)
        fpr(stream, "%d", offset);
      else
        info->print_address_func((bfd_vma) imm32, info);
    }
    if (opcode->args == 2) {
      if (!strcmp("ldi", opcode->name)) {
	fpr(stream, "%s %s, %d", opcode->name,
	    print_reg_name((iword >> 16) & 0xf),
	    imm32);
      } else {
	fpr(stream, "%s %s, ", opcode->name,
	    print_reg_name((iword >> 16) & 0xf));
	info->print_address_func((bfd_vma) imm32, info);
      }
    }
    if (opcode->args == 3)
      fpr(stream, "%s %s, %s, %d", opcode->name, 
	  print_reg_name((iword >> 16) & 0xf),
	  print_reg_name((iword >> 12) & 0xf),
	  offset);
    break;
  case BEXKAT1_IMM:
    offset = ((iword >> 8) & 0xf000) | (iword & 0xfff);
    if (opcode->args == 1) {
      if (offset & 0x00008000)
        offset |=  0xffff0000;
      fpr(stream, "%s %d", opcode->name, offset);
    } else {
      fpr(stream, "%s %s, %d", opcode->name,
	  print_reg_name((iword >> 16) & 0xf),
	  offset);
    }
    length = 4;
    break;
  case BEXKAT1_REGIND:
    offset = ((iword >> 8) & 0xf000) | (iword & 0xfff);
    if (offset & 0x00008000)
      offset |=  0xffff0000;
    if (opcode->args == 2) {
      if (offset == 0)
	fpr(stream, "%s (%s)", opcode->name,
	    print_reg_name((iword >> 12) & 0xf));
      else
	fpr(stream, "%s %d(%s)", opcode->name, offset,
	    print_reg_name((iword >> 12) & 0xf));
    }
    if (opcode->args == 3) {
      if (offset == 0)
	fpr(stream, "%s %s, (%s)", opcode->name,
	    print_reg_name((iword >> 16) & 0xf),
	    print_reg_name((iword >> 12) & 0xf));
      else
	fpr(stream, "%s %s, %d(%s)", opcode->name,
	    print_reg_name((iword >> 16) & 0xf),
	    offset,
	    print_reg_name((iword >> 12) & 0xf));
    }
    length = 4;
    break;
  }

  return length;

fail:
  info->memory_error_func(status, memaddr, info);
  return -1;
}
