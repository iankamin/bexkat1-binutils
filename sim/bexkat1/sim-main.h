#ifndef _SIM_MAIN_H
#define _SIM_MAIN_H

#include "sim-basics.h"

typedef address_word sim_cia;

#include "sim-base.h"
#include "bfd.h"

#define NUM_BEXKAT1_REGS 34
#define NUM_BEXKAT1_SREGS 256
#define PC_REGNO 33
#define SP_REGNO 32

struct _sim_cpu {

  /* The following are internal simulator state variables: */
#define CIA_GET(CPU) ((CPU)->registers[PC_REGNO] + 0)
#define CIA_SET(CPU,CIA) ((CPU)->registers[PC_REGNO] = (CIA))

/* To keep this default simulator simple, and fast, we use a direct
   vector of registers. The internal simulator engine then uses
   manifests to access the correct slot. */

  unsigned_word registers[NUM_BEXKAT1_REGS + 1];

  sim_cpu_base base;
};

struct sim_state {

  sim_cpu cpu[MAX_NR_PROCESSORS];
#if (WITH_SMP)
#define STATE_CPU(sd,n) (&(sd)->cpu[n])
#else
#define STATE_CPU(sd,n) (&(sd)->cpu[0])
#endif

  sim_state_base base;
};

#endif
