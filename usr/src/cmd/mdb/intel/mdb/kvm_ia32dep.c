/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Libkvm Kernel Target Intel 32-bit component
 *
 * This file provides the ISA-dependent portion of the libkvm kernel target.
 * For more details on the implementation refer to mdb_kvm.c.
 */

#include <sys/types.h>
#include <sys/regset.h>
#include <sys/frame.h>
#include <sys/stack.h>
#include <sys/sysmacros.h>
#include <sys/panic.h>
#include <strings.h>

#include <mdb/mdb_target_impl.h>
#include <mdb/mdb_disasm.h>
#include <mdb/mdb_modapi.h>
#include <mdb/mdb_conf.h>
#include <mdb/mdb_kreg_impl.h>
#include <mdb/mdb_ia32util.h>
#include <mdb/kvm_isadep.h>
#include <mdb/mdb_kvm.h>
#include <mdb/mdb_err.h>
#include <mdb/mdb_debug.h>
#include <mdb/mdb.h>


/*ARGSUSED*/
int
kt_regs(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	mdb_ia32_printregs((const mdb_tgt_gregset_t *)addr);
	return (DCMD_OK);
}

static int
kt_stack_common(uintptr_t addr, uint_t flags, int argc,
    const mdb_arg_t *argv, mdb_tgt_stack_f *func)
{
	kt_data_t *kt = mdb.m_target->t_data;
	void *arg = (void *)mdb.m_nargs;
	mdb_tgt_gregset_t gregs, *grp;

	if (flags & DCMD_ADDRSPEC) {
		bzero(&gregs, sizeof (gregs));
		gregs.kregs[KREG_EBP] = addr;
		grp = &gregs;
	} else
		grp = kt->k_regs;

	if (argc != 0) {
		if (argv->a_type == MDB_TYPE_CHAR || argc > 1)
			return (DCMD_USAGE);

		if (argv->a_type == MDB_TYPE_STRING)
			arg = (void *)(uint_t)mdb_strtoull(argv->a_un.a_str);
		else
			arg = (void *)(uint_t)argv->a_un.a_val;
	}

	(void) mdb_ia32_kvm_stack_iter(mdb.m_target, grp, func, arg);
	return (DCMD_OK);
}

int
kt_stack(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	return (kt_stack_common(addr, flags, argc, argv, mdb_ia32_kvm_frame));
}

int
kt_stackv(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	return (kt_stack_common(addr, flags, argc, argv, mdb_ia32_kvm_framev));
}

const mdb_tgt_ops_t kt_ia32_ops = {
	kt_setflags,				/* t_setflags */
	kt_setcontext,				/* t_setcontext */
	kt_activate,				/* t_activate */
	kt_deactivate,				/* t_deactivate */
	(void (*)()) mdb_tgt_nop,		/* t_periodic */
	kt_destroy,				/* t_destroy */
	kt_name,				/* t_name */
	(const char *(*)()) mdb_conf_isa,	/* t_isa */
	kt_platform,				/* t_platform */
	kt_uname,				/* t_uname */
	kt_dmodel,				/* t_dmodel */
	kt_aread,				/* t_aread */
	kt_awrite,				/* t_awrite */
	kt_vread,				/* t_vread */
	kt_vwrite,				/* t_vwrite */
	kt_pread,				/* t_pread */
	kt_pwrite,				/* t_pwrite */
	kt_fread,				/* t_fread */
	kt_fwrite,				/* t_fwrite */
	(ssize_t (*)()) mdb_tgt_notsup,		/* t_ioread */
	(ssize_t (*)()) mdb_tgt_notsup,		/* t_iowrite */
	kt_vtop,				/* t_vtop */
	kt_lookup_by_name,			/* t_lookup_by_name */
	kt_lookup_by_addr,			/* t_lookup_by_addr */
	kt_symbol_iter,				/* t_symbol_iter */
	kt_mapping_iter,			/* t_mapping_iter */
	kt_object_iter,				/* t_object_iter */
	kt_addr_to_map,				/* t_addr_to_map */
	kt_name_to_map,				/* t_name_to_map */
	kt_addr_to_ctf,				/* t_addr_to_ctf */
	kt_name_to_ctf,				/* t_name_to_ctf */
	kt_status,				/* t_status */
	(int (*)()) mdb_tgt_notsup,		/* t_run */
	(int (*)()) mdb_tgt_notsup,		/* t_step */
	(int (*)()) mdb_tgt_notsup,		/* t_step_out */
	(int (*)()) mdb_tgt_notsup,		/* t_step_branch */
	(int (*)()) mdb_tgt_notsup,		/* t_next */
	(int (*)()) mdb_tgt_notsup,		/* t_cont */
	(int (*)()) mdb_tgt_notsup,		/* t_signal */
	(int (*)()) mdb_tgt_null,		/* t_add_vbrkpt */
	(int (*)()) mdb_tgt_null,		/* t_add_sbrkpt */
	(int (*)()) mdb_tgt_null,		/* t_add_pwapt */
	(int (*)()) mdb_tgt_null,		/* t_add_vwapt */
	(int (*)()) mdb_tgt_null,		/* t_add_iowapt */
	(int (*)()) mdb_tgt_null,		/* t_add_sysenter */
	(int (*)()) mdb_tgt_null,		/* t_add_sysexit */
	(int (*)()) mdb_tgt_null,		/* t_add_signal */
	(int (*)()) mdb_tgt_null,		/* t_add_fault */
	kt_getareg,				/* t_getareg */
	kt_putareg,				/* t_putareg */
	mdb_ia32_kvm_stack_iter,		/* t_stack_iter */
};

void
kt_regs_to_kregs(struct regs *regs, mdb_tgt_gregset_t *gregs)
{
	gregs->kregs[KREG_SAVFP] = regs->r_savfp;
	gregs->kregs[KREG_SAVPC] = regs->r_savpc;
	gregs->kregs[KREG_EAX] = regs->r_eax;
	gregs->kregs[KREG_EBX] = regs->r_ebx;
	gregs->kregs[KREG_ECX] = regs->r_ecx;
	gregs->kregs[KREG_EDX] = regs->r_edx;
	gregs->kregs[KREG_ESI] = regs->r_esi;
	gregs->kregs[KREG_EDI] = regs->r_edi;
	gregs->kregs[KREG_EBP] = regs->r_ebp;
	gregs->kregs[KREG_ESP] = regs->r_esp;
	gregs->kregs[KREG_CS] = regs->r_cs;
	gregs->kregs[KREG_DS] = regs->r_ds;
	gregs->kregs[KREG_SS] = regs->r_ss;
	gregs->kregs[KREG_ES] = regs->r_es;
	gregs->kregs[KREG_FS] = regs->r_fs;
	gregs->kregs[KREG_GS] = regs->r_gs;
	gregs->kregs[KREG_EFLAGS] = regs->r_efl;
	gregs->kregs[KREG_EIP] = regs->r_eip;
	gregs->kregs[KREG_UESP] = regs->r_uesp;
	gregs->kregs[KREG_TRAPNO] = regs->r_trapno;
	gregs->kregs[KREG_ERR] = regs->r_err;
}

void
kt_ia32_init(mdb_tgt_t *t)
{
	kt_data_t *kt = t->t_data;
	panic_data_t pd;
	label_t label;
	struct regs regs;
	kreg_t *kregs;
	uintptr_t addr;

	/*
	 * Initialize the machine-dependent parts of the kernel target
	 * structure.  Once this is complete and we fill in the ops
	 * vector, the target is now fully constructed and we can use
	 * the target API itself to perform the rest of our initialization.
	 */
	kt->k_rds = mdb_ia32_kregs;
	kt->k_regs = mdb_zalloc(sizeof (mdb_tgt_gregset_t), UM_SLEEP);
	kt->k_regsize = sizeof (mdb_tgt_gregset_t);
	kt->k_dcmd_regs = kt_regs;
	kt->k_dcmd_stack = kt_stack;
	kt->k_dcmd_stackv = kt_stackv;
	kt->k_dcmd_stackr = kt_stackv;
	kt->k_dcmd_cpustack = kt_cpustack;
	kt->k_dcmd_cpuregs = kt_cpuregs;

	t->t_ops = &kt_ia32_ops;
	kregs = kt->k_regs->kregs;

	(void) mdb_dis_select("ia32");

	/*
	 * Lookup the symbols corresponding to subroutines in locore.s where
	 * we expect a saved regs structure to be pushed on the stack.  When
	 * performing stack tracebacks we will attempt to detect interrupt
	 * frames by comparing the %eip value to these symbols.
	 */
	(void) mdb_tgt_lookup_by_name(t, MDB_TGT_OBJ_EXEC,
	    "cmnint", &kt->k_intr_sym, NULL);

	(void) mdb_tgt_lookup_by_name(t, MDB_TGT_OBJ_EXEC,
	    "cmntrap", &kt->k_trap_sym, NULL);

	/*
	 * Don't attempt to load any thread or register information if
	 * we're examining the live operating system.
	 */
	if (kt->k_symfile != NULL && strcmp(kt->k_symfile, "/dev/ksyms") == 0)
		return;

	/*
	 * If the panicbuf symbol is present and we can consume a panicbuf
	 * header of the appropriate version from this address, then we can
	 * initialize our current register set based on its contents.
	 * Prior to the re-structuring of panicbuf, our only register data
	 * was the panic_regs label_t, into which a setjmp() was performed,
	 * or the panic_reg register pointer, which was only non-zero if
	 * the system panicked as a result of a trap calling die().
	 */
	if (mdb_tgt_readsym(t, MDB_TGT_AS_VIRT, &pd, sizeof (pd),
	    MDB_TGT_OBJ_EXEC, "panicbuf") == sizeof (pd) &&
	    pd.pd_version == PANICBUFVERS) {

		size_t pd_size = MIN(PANICBUFSIZE, pd.pd_msgoff);
		panic_data_t *pdp = mdb_zalloc(pd_size, UM_SLEEP);
		uint_t i, n;

		(void) mdb_tgt_readsym(t, MDB_TGT_AS_VIRT, pdp, pd_size,
		    MDB_TGT_OBJ_EXEC, "panicbuf");

		n = (pd_size - (sizeof (panic_data_t) -
		    sizeof (panic_nv_t))) / sizeof (panic_nv_t);

		for (i = 0; i < n; i++) {
			(void) kt_putareg(t, kt->k_tid,
			    pdp->pd_nvdata[i].pnv_name,
			    pdp->pd_nvdata[i].pnv_value);
		}

		mdb_free(pdp, pd_size);

		return;
	}

	if (mdb_tgt_readsym(t, MDB_TGT_AS_VIRT, &addr, sizeof (addr),
	    MDB_TGT_OBJ_EXEC, "panic_reg") == sizeof (addr) && addr != NULL &&
	    mdb_tgt_vread(t, &regs, sizeof (regs), addr) == sizeof (regs)) {
		kt_regs_to_kregs(&regs, kt->k_regs);
		return;
	}

	/*
	 * If we can't read any panic regs, then our penultimate try is for any
	 * CPU context that may have been stored (for example, in Xen core
	 * dumps).  As this can only succeed for kernels with the above
	 * methods available, we let it over-ride the older panic_regs method,
	 * which will always manage to read the label_t, even if there's
	 * nothing useful there.
	 */
	if (kt_kvmregs(t, 0, kt->k_regs) == 0)
		return;

	if (mdb_tgt_readsym(t, MDB_TGT_AS_VIRT, &label, sizeof (label),
	    MDB_TGT_OBJ_EXEC, "panic_regs") == sizeof (label)) {
		kregs[KREG_EDI] = label.val[0];
		kregs[KREG_ESI] = label.val[1];
		kregs[KREG_EBX] = label.val[2];
		kregs[KREG_EBP] = label.val[3];
		kregs[KREG_ESP] = label.val[4];
		kregs[KREG_EIP] = label.val[5];
		return;
	}

	warn("failed to read panicbuf, panic_reg and panic_regs -- "
	    "current register set will be unavailable\n");
}
