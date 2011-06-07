/*
** Copyright (C) 2011 EADS France, stephane duverger <stephane.duverger@eads.net>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License along
** with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include <emulate.h>
#include <vmm.h>
#include <vm.h>
#include <gdb.h>
#include <disasm.h>
#include <intr.h>
#include <debug.h>
#include <info_data.h>

extern info_data_t *info;

static uint8_t __emulate_get_gpr(struct ud_operand *op)
{
   uint8_t gpr;

   if(op->base < UD_R_RAX)
      gpr = op->base - UD_R_EAX;
   else
      gpr = op->base - UD_R_RAX;

   return (GPR64_RAX - gpr);
}

static void __emulate_rmode_push(uint16_t value)
{
   loc_t stack;

   __pre_access(__ss.base_addr);

   info->vm.cpu.gpr->rsp.wlow -= sizeof(uint16_t);
   stack.linear = __ss.base_addr.low + info->vm.cpu.gpr->rsp.wlow;
   *stack.u16   = value;
}

static void __emulate_rmode_far_jump(fptr_t *fptr)
{
   __cs.selector.raw  = fptr->segment;
   __cs.base_addr.low = (__cs.selector.raw)<<4;

   /* on op16, eip high is cleared */
   __rip.low  = fptr->offset.raw;

   /* debug(EMU_INSN, "far jump to 0x%x:0x%x\n", */
   /* 	 __cs.selector.raw, __rip.wlow); */

   __post_access(__cs.selector);
   __post_access(__cs.base_addr);
   __post_access(__rip);
}

static void __emulate_rmode_far_call(fptr_t *fptr, uint16_t insn_sz)
{
   __emulate_rmode_push(__cs.selector.raw);
   __emulate_rmode_push(__rip.wlow+insn_sz);

   /* debug(EMU_INSN, "far call saved frame 0x%x:0x%x\n", */
   /* 	 __cs.selector.raw, __rip.wlow+insn_sz); */

   __emulate_rmode_far_jump(fptr);
}

static void __emulate_rmode_interrupt(uint8_t vector, uint16_t insn_sz)
{
   ivt_e_t *ivt;
   fptr_t  fptr;

   ivt = (ivt_e_t*)0;

   /* debug(EMU_INSN, "int 0x%x\n", vector); */

   fptr.segment      = ivt[vector].cs;
   fptr.offset.wlow  = ivt[vector].ip;
   fptr.offset.whigh = 0;

   __emulate_rmode_push(__rflags.wlow);

   __rflags.IF = 0;
   __rflags.tf = 0;
   __rflags.rf = 0;
   __rflags.ac = 0;

   __emulate_rmode_far_call(&fptr, insn_sz);

   __post_access(__rflags);
}

static int __emulate_interrupt(uint8_t __unused__ soft, uint8_t vector, uint16_t insn_sz)
{
   if(__rmode())
   {
      if(vector == BIOS_MISC_INTERRUPT)
      {
	 int rc = emulate_int15();

	 if(rc != EMU_SUCCESS_LET_RIP)
	    return rc;
      }

      __emulate_rmode_interrupt(vector, insn_sz);
      return EMU_SUCCESS_LET_RIP;
   }

   return EMU_FAIL;
}

int __emulate_hard_interrupt(uint8_t vector)
{
   return __emulate_interrupt(0, vector, 0);
}

int __emulate_exception(uint8_t vector)
{
   return __emulate_interrupt(0, vector, 0);
}

static int emulate_int1()
{
   return EMU_FAIL;
   /* return __emulate_interrupt(1, 1, 1); */
}

static int emulate_int3()
{
   return EMU_FAIL;
   /* return __emulate_interrupt(1, 3, 1); */
}

static int emulate_into()
{
   return EMU_FAIL;
   /* return __emulate_interrupt(1, 4, 1); */
}

static int emulate_intn(ud_t *disasm)
{
   struct ud_operand *op = &disasm->operand[0];

   if(op->type != UD_OP_IMM)
      return EMU_FAIL;

   return __emulate_interrupt(1, op->lval.ubyte, ud_insn_len(disasm));
}

static int emulate_mov(ud_t *disasm)
{
   struct ud_operand  *op1, *op2;
   uint8_t            gpr;

   op1 = &disasm->operand[0];
   op2 = &disasm->operand[1];

   if(op2->type != UD_OP_REG || op1->type != UD_OP_REG)
      return EMU_FAIL;

   /* read cr */
   if(range(op2->base, UD_R_CR0, UD_R_CR4))
   {
      uint8_t cr = op2->base - UD_R_CR0;

      if(disasm->pfx_lock != UD_NONE || disasm->pfx_rex != UD_NONE)
	 return EMU_FAIL;

      gpr = __emulate_get_gpr(op1);
      return __resolve_cr(0, cr, gpr);
   }
   /* write cr */
   else if(range(op1->base, UD_R_CR0, UD_R_CR4))
   {
      uint8_t cr = op1->base - UD_R_CR0;

      if(disasm->pfx_lock != UD_NONE || disasm->pfx_rex != UD_NONE)
	 return EMU_FAIL;

      gpr = __emulate_get_gpr(op2);
      return __resolve_cr(1, cr, gpr);
   }
   /* read dr */
   else if(range(op2->base, UD_R_DR0, UD_R_DR7))
   {
      uint8_t dr = op2->base - UD_R_DR0;
      gpr = __emulate_get_gpr(op1);
      return __resolve_dr(0, dr, gpr);
   }
   /* write dr */
   else if(range(op1->base, UD_R_DR0, UD_R_DR7))
   {
      uint8_t dr = op1->base - UD_R_DR0;
      gpr = __emulate_get_gpr(op2);
      return __resolve_dr(1, dr, gpr);
   }

   return EMU_FAIL;
}

static int emulate_clts()
{
   if(__cpl && !__rmode())
   {
      __inject_exception(GP_EXCP, 0, 0);
      return EMU_FAULT;
   }

   __cr0.ts = 0;
   __post_access(__cr0);

   return EMU_SUCCESS;
}

static int emulate_pushf()
{
   return EMU_FAIL;
}

/*
** RFLAGS inspection
*/
static int emulate_pre_inspect_rflags(ud_t *disasm, offset_t addend)
{
   int          mode;
   offset_t     vaddr;
   rflags_reg_t rflags;

   if(!gdb_singlestep_enabled())
   {
      debug(EMU, "should not trap %s when not single-stepping\n", ud_insn_asm(disasm));
      return EMU_FAIL;
   }

   vm_get_stack_addr(&vaddr, addend, &mode);

   /*
   ** we "try" to retrieve minimum size of rflags
   ** if we fail, we let vm run the insn and fail on its own
   */
   if(vm_read_mem(vaddr, (uint8_t*)&rflags, 2))
   {
      if(!gdb_singlestep_correct_rflags(&rflags))
      {
	 gdb_singlestep_set_rflags(&rflags);
	 vm_write_mem(vaddr, (uint8_t*)&rflags, 2);
      }
   }

   return EMU_SUCCESS_LET_RIP;
}

static int emulate_popf(ud_t *disasm)
{
   __allow_popf();
   return emulate_pre_inspect_rflags(disasm, 0);
}

static int emulate_iret(ud_t *disasm, size_t sz)
{
   offset_t addend = 2*sz;

   __allow_iret();
   return emulate_pre_inspect_rflags(disasm, addend);
}

static int emulate_sysenter()
{
   seg_sel_t cs;

   cs.raw = __sysenter_cs.wlow;

   if(!__cr0.pe || !cs.index)
   {
      debug(EMU, "sysenter fault: cr0.pe %d cs.index %d\n", __cr0.pe, cs.index);
      __inject_exception(GP_EXCP, 0, 0);
      return EMU_FAULT;
   }

   __rflags.vm = 0;
   __rflags.IF = 0;
   __rflags.rf = 0;

   __cs.selector.raw = __sysenter_cs.wlow;
   __cs.base_addr.raw = 0ULL;
   __cs.limit.raw = 0xffffffff;
   __cs.attributes.g = 1;
   __cs.attributes.s = 1;
   __cs.attributes.type = SEG_DESC_CODE_XRA;
   __cs.attributes.d = 1;
   __cs.attributes.dpl = 0;
   __cs.selector.rpl = 0;
   __cs.attributes.p = 1;
   __cpl = 0;

   __ss.selector.raw = __sysenter_cs.wlow + 8;
   __ss.base_addr.raw = 0ULL;
   __ss.limit.raw = 0xffffffff;
   __ss.attributes.g = 1;
   __ss.attributes.s = 1;
   __ss.attributes.type = SEG_DESC_DATA_RWA;
   __ss.attributes.d = 1;
   __ss.attributes.dpl = 0;
   __ss.selector.rpl = 0;
   __ss.attributes.p = 1;

   __rip.low = __sysenter_eip.low;
   info->vm.cpu.gpr->rsp.low = __sysenter_esp.low;
   return EMU_SUCCESS_LET_RIP;
}

static int emulate_sysexit()
{
   seg_sel_t cs;

   cs.raw = __sysenter_cs.wlow;

   if(__cpl || !__cr0.pe || !cs.index)
   {
      debug(EMU, "sysexit fault: cpl %d cr0.pe %d cs.index %d\n"
	    , __cpl, __cr0.pe, cs.index);
      __inject_exception(GP_EXCP, 0, 0);
      return EMU_FAULT;
   }

   __cs.selector.raw = __sysenter_cs.wlow + 16;
   __cs.base_addr.raw = 0ULL;
   __cs.limit.raw = 0xffffffff;
   __cs.attributes.g = 1;
   __cs.attributes.s = 1;
   __cs.attributes.type = SEG_DESC_CODE_XRA;
   __cs.attributes.d = 1;
   __cs.attributes.dpl = 3;
   __cs.selector.rpl = 3;
   __cs.attributes.p = 1;
   __cpl = 3;

   __ss.selector.raw = __sysenter_cs.wlow + 24;
   __ss.base_addr.raw = 0ULL;
   __ss.limit.raw = 0xffffffff;
   __ss.attributes.g = 1;
   __ss.attributes.s = 1;
   __ss.attributes.type = SEG_DESC_DATA_RWA;
   __ss.attributes.d = 1;
   __ss.attributes.dpl = 3;
   __ss.selector.rpl = 3;
   __ss.attributes.p = 1;

   info->vm.cpu.gpr->rsp.low = info->vm.cpu.gpr->rcx.low;
   __rip.low = info->vm.cpu.gpr->rdx.low;
   return EMU_SUCCESS_LET_RIP;
}


/*
** Data hardware and single-step traps
** are checked after insn execution. If we
** emulated one, we may loose a #db condition
**
** Such a situation is raised when we single-step
** or put data breakpoint on an instruction which
** is emulated by the vmm (cr access, ...)
**
** Insn hardware breakpoints are not subject to this
** because they are checked before execution
*/
static void __emulate_vmm_db_condition()
{
   if(!gdb_active_cr3_check())
      return;

   if(gdb_singlestep_fake())
      return;

   /* XXX: we must check data-breakpoints too */
}

void __emulate_hazard(ud_t *disasm)
{
   uint32_t x;

   debug(EMU, "emulation hazard on (%d) \"%s\": "
	 , ud_insn_len(disasm)
	 , ud_insn_asm(disasm));

   for(x=0;x<ud_insn_len(disasm);x++)
      printf(" %x", ud_insn_ptr(disasm)[x]);
   printf("\n");
}

int __emulate_insn(ud_t *disasm)
{
   int rc;

   switch(disasm->mnemonic)
   {
   case UD_Iint:
      rc = emulate_intn(disasm);
      break;

   case UD_Imov:
      rc = emulate_mov(disasm);
      break;

   case UD_Isysenter:
      rc = emulate_sysenter();
      break;
   case UD_Isysexit:
      rc = emulate_sysexit();
      break;

   case UD_Iiretw:
      rc = emulate_iret(disasm, 2);
      break;
   case UD_Iiretd:
      rc = emulate_iret(disasm, 4);
      break;
   case UD_Iiretq:
      rc = emulate_iret(disasm, 8);
      break;

   case UD_Iclts:
      rc = emulate_clts();
      break;

   case UD_Ipushfw:
   case UD_Ipushfd:
   case UD_Ipushfq:
      rc = emulate_pushf();
      break;

   case UD_Ipopfw:
   case UD_Ipopfd:
   case UD_Ipopfq:
      rc = emulate_popf(disasm);
      break;

   case UD_Iint1:
      rc = emulate_int1();
      break;
   case UD_Iint3:
      rc = emulate_int3();
      break;
   case UD_Iinto:
      if(__rflags.of)
	 rc = emulate_into();
      else
	 rc = EMU_SUCCESS;
      break;

   default:
      rc = EMU_UNSUPPORTED;
      break;
   }

   switch(rc)
   {
   case EMU_SUCCESS:
      vm_update_rip(ud_insn_len(disasm));
      break;
   case EMU_UNSUPPORTED:
   case EMU_FAIL:
      __emulate_hazard(disasm);
      break;
   }

   return rc;
}

int emulate()
{
   ud_t disasm;

   if(!disassemble(&disasm))
      return 0;

   switch(__emulate_insn(&disasm))
   {
   case EMU_FAIL:
   case EMU_UNSUPPORTED:
      return 0;
   }

   /* even if fault, we must check if we get control */
   __emulate_vmm_db_condition();
   return 1;
}

