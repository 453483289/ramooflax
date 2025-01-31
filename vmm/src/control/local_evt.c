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
#include <ctrl_evt.h>
#include <dbg.h>
#include <debug.h>
#include <info_data.h>

extern info_data_t *info;

static int local_evt_cr_rd(arg_t __unused__ arg)
{
   return VM_DONE;
}

static int local_evt_cr_wr(arg_t __unused__ arg)
{
   return VM_DONE;
}

static int local_evt_excp(arg_t __unused__ arg)
{
   return VM_DONE;
}

static int local_evt_brk(arg_t __unused__ arg)
{
   return VM_DONE;
}

static int local_evt_stp(arg_t __unused__ arg)
{
   return VM_DONE;
}

static int local_evt_npf(arg_t __unused__ arg)
{
   return VM_FAIL;
}

static int local_evt_hyp(arg_t __unused__ arg)
{
   return VM_DONE;
}

static int local_evt_cpuid(arg_t __unused__ arg)
{
   return VM_DONE;
}

ctrl_evt_hdl_t ctrl_evt_dft_hdl[] = {
   local_evt_cr_rd,
   local_evt_cr_wr,
   local_evt_excp,
   local_evt_brk,
   local_evt_stp,
   local_evt_npf,
   local_evt_hyp,
   local_evt_cpuid,
};
