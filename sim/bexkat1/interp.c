#include "config.h"
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include "sysdep.h"
#include <sys/times.h>
#include <sys/param.h>
#include <netinet/in.h>	/* for byte ordering macros */
#include "bfd.h"
#include "gdb/callback.h"
#include "libiberty.h"
#include "gdb/remote-sim.h"

#include "sim-main.h"
#include "sim-base.h"

#define EXTRACT_WORD(addr) \
  ((sim_core_read_aligned_1 (scpu, cia, read_map, addr) << 24) \
   + (sim_core_read_aligned_1 (scpu, cia, read_map, addr+1) << 16) \
   + (sim_core_read_aligned_1 (scpu, cia, read_map, addr+2) << 8) \
   + (sim_core_read_aligned_1 (scpu, cia, read_map, addr+3)))

typedef int word;
typedef unsigned int uword;

static void set_initial_gprs ();

host_callback *callback;
FILE *tracefile;

static int tracing = 0;
static char *myname;
static SIM_OPEN_KIND sim_kind;
static int issue_messages = 0;

struct bexkat1_regset {
  word regs[NUM_BEXKAT1_REGS + 1];
  word sregs[NUM_BEXKAT1_SREGS];
  word cc;
  int exception;
  unsigned long long insts;
};

#define CC_ZERO (1<<0)
#define CC_OV   (1<<1)
#define CC_NEG  (1<<2)
#define CC_C    (1<<3)

union
{
  struct bexkat1_regset asregs;
  word asints [1];		/* but accessed larger... */
} cpu;

/* Run or resume simulated program */
void sim_resume(SIM_DESC sd, int step, int signal) {
  word pc, opc;
  unsigned long long insts;
  unsigned int inst;
  sim_cpu *scpu = STATE_CPU (sd, 0); /* FIXME */
  address_word cia = CIA_GET (scpu);

  cpu.asregs.exception = step ? SIGTRAP: 0;
  pc = cpu.asregs.regs[PC_REGNO];
  insts = cpu.asregs.insts;

  /* Run instructions here. */
  do 
    {
      opc = pc;

      /* Fetch the instruction at pc.  */
      inst = EXTRACT_WORD(pc);

      int mode = (inst >> 29);
      int opcode = (inst >> 21) & 0xff;
      printf("pc = %08x, inst = %08x, mode = %0x, opcode = %0x\n", pc, inst, mode, opcode);
      pc += 4;
      insts++;
    } while (!cpu.asregs.exception);
  cpu.asregs.regs[PC_REGNO] = pc;
  cpu.asregs.insts += insts;
}

unsigned long
bexkat1_extract_unsigned_integer (addr, len)
     unsigned char * addr;
     int len;
{
  unsigned long retval;
  unsigned char * p;
  unsigned char * startaddr = (unsigned char *)addr;
  unsigned char * endaddr = startaddr + len;
 
  if (len > (int) sizeof (unsigned long))
    printf ("That operation is not available on integers of more than %ld bytes.",
	    sizeof (unsigned long));
 
  /* Start at the most significant end of the integer, and work towards
     the least significant.  */
  retval = 0;

  for (p = endaddr; p > startaddr;)
    retval = (retval << 8) | * -- p;
  
  return retval;
}

void
bexkat1_store_unsigned_integer (addr, len, val)
     unsigned char * addr;
     int len;
     unsigned long val;
{
  unsigned char * p;
  unsigned char * startaddr = (unsigned char *)addr;
  unsigned char * endaddr = startaddr + len;

  for (p = endaddr; p > startaddr;)
    {
      * -- p = val & 0xff;
      val >>= 8;
    }
}

/* Store bytes into the simulated program's memory */
int sim_write(SIM_DESC sd, SIM_ADDR addr, const unsigned char *buffer, int size) {
  sim_cpu *scpu = STATE_CPU (sd, 0); /* FIXME */

  sim_core_write_buffer (sd, scpu, write_map, buffer, addr, size);

  return size;
}

/* Fetch bytes of the simulated program's memory */
int sim_read (SIM_DESC sd, SIM_ADDR addr, unsigned char *buffer, int size) {
  sim_cpu *scpu = STATE_CPU (sd, 0); /* FIXME */

  sim_core_read_buffer (sd, scpu, read_map, buffer, addr, size);
  
  return size;
}

/* Store target-endian value in register */
int sim_store_register (SIM_DESC sd, int rn, unsigned char *memory, int length) {
  if (rn < NUM_BEXKAT1_REGS && rn >= 0)
    {
      if (length == 4)
	{
	  long ival;
	  
	  /* misalignment safe */
	  ival = bexkat1_extract_unsigned_integer (memory, 4);
	  cpu.asints[rn] = ival;
	}

      return 4;
    }
  else
    return 0;
}

/* Fetch register, storing target-endian value */
int sim_fetch_register (SIM_DESC sd, int rn, unsigned char *memory, int length) {
  if (rn < NUM_BEXKAT1_REGS && rn >= 0)
    {
      if (length == 4)
	{
	  long ival = cpu.asints[rn];

	  /* misalignment-safe */
	  bexkat1_store_unsigned_integer (memory, 4, ival);
	}
      
      return 4;
    }
  else
    return 0;
}

/* Return the reason why a program stopped */
void sim_stop_reason (SIM_DESC sd, enum sim_stop * reason,  int *sigrc) {
  if (cpu.asregs.exception == SIGQUIT)
    {
      * reason = sim_exited;
      * sigrc = cpu.asregs.regs[2];
    }
  else
    {
      * reason = sim_stopped;
      * sigrc = cpu.asregs.exception;
    }
}

/* Async request to stop the simulation */
int sim_stop (SIM_DESC sd) {
  cpu.asregs.exception = SIGINT;
  return 1;
}

/* Create an initialized simulator instance */
SIM_DESC sim_open (SIM_OPEN_KIND kind, host_callback *cb, struct bfd *abfd, char **argv) {
  SIM_DESC sd = sim_state_alloc(kind, cb);
  SIM_ASSERT(STATE_MAGIC(sd) == SIM_MAGIC_NUMBER);

  if (sim_pre_argv_init(sd, argv[0]) != SIM_RC_OK)
    return 0;

  sim_do_command(sd, " memory region 0x00000000,0x4000");
  sim_do_command(sd, " memory region 0xffff0000,0x10000");

  myname = argv[0];
  callback = cb;

  if (kind == SIM_OPEN_STANDALONE)
    issue_messages = 1;

  set_initial_gprs();

  if (sim_config(sd) != SIM_RC_OK) {
    sim_module_uninstall(sd);
    return 0;
  }

  if (sim_post_argv_init(sd) != SIM_RC_OK) {
    sim_module_uninstall(sd);
    return 0;
  }

  return sd;
}

/* Write a 4 byte value to memory.  */

static void INLINE wlat (sim_cpu *scpu, word pc, word x, word v) {
  address_word cia = CIA_GET (scpu);
	
  sim_core_write_aligned_4 (scpu, cia, write_map, x, v);
}

static void set_initial_gprs () {
  int i;
  long space;
  
  /* Set up machine just out of reset.  */
  cpu.asregs.regs[PC_REGNO] = 0;
  
  /* Clean out the register contents.  */
  for (i = 0; i < NUM_BEXKAT1_REGS; i++)
    cpu.asregs.regs[i] = 0;
  for (i = 0; i < NUM_BEXKAT1_SREGS; i++)
    cpu.asregs.sregs[i] = 0;
}

void sim_do_command (SIM_DESC sd, const char *cmd) {
  if (sim_args_command (sd, cmd) != SIM_RC_OK)
    sim_io_printf (sd, 
		   "Error: \"%s\" is not a valid bexkat1 simulator command.\n",
		   cmd);
}

/* Destroy a simulator instance */
void sim_close (SIM_DESC sd, int quitting) {
}

/* Load a program into simulator memory */
SIM_RC sim_load (SIM_DESC sd, const char *prog, bfd *abfd, int from_tty) {
  /* Do the right thing for ELF executables; this turns out to be
     just about the right thing for any object format that:
       - we crack using BFD routines
       - follows the traditional UNIX text/data/bss layout
       - calls the bss section ".bss".   */

  extern bfd * sim_load_file (); /* ??? Don't know where this should live.  */
  bfd * prog_bfd;

  {
    bfd * handle;
    handle = bfd_openr (prog, 0);	/* could be "moxie" */
    
    if (!handle)
      {
	printf("``%s'' could not be opened.\n", prog);
	return SIM_RC_FAIL;
      }
    
    /* Makes sure that we have an object file, also cleans gets the 
       section headers in place.  */
    if (!bfd_check_format (handle, bfd_object))
      {
	/* wasn't an object file */
	bfd_close (handle);
	printf ("``%s'' is not appropriate object file.\n", prog);
	return SIM_RC_FAIL;
      }

    /* Clean up after ourselves.  */
    bfd_close (handle);
  }

  /* from sh -- dac */
  prog_bfd = sim_load_file (sd, myname, callback, prog, abfd,
                            sim_kind == SIM_OPEN_DEBUG,
                            0, sim_write);
  if (prog_bfd == NULL)
    return SIM_RC_FAIL;
  
  if (abfd == NULL)
    bfd_close (prog_bfd);

  return SIM_RC_OK;
}

/* Load the device tree blob.  */
static void load_dtb (SIM_DESC sd, const char *filename) {
  int size = 0;
  FILE *f = fopen (filename, "rb");
  char *buf;
  sim_cpu *scpu = STATE_CPU (sd, 0); /* FIXME */ 
 if (f == NULL)
    {
      printf ("WARNING: ``%s'' could not be opened.\n", filename);
      return;
    }
  fseek (f, 0, SEEK_END);
  size = ftell(f);
  fseek (f, 0, SEEK_SET);
  buf = alloca (size);
  if (size != fread (buf, 1, size, f))
    {
      printf ("ERROR: error reading ``%s''.\n", filename);
      return;
    }
  sim_core_write_buffer (sd, scpu, write_map, buf, 0xffff0000, size);
  cpu.asregs.sregs[9] = 0xffff0000;
  fclose (f);
}

/* Prepare to run the simulated program */
SIM_RC sim_create_inferior (SIM_DESC sd, struct bfd *prog_bfd, char **argv, char **env) {
  char ** avp;
  int l, argc, i, tp;
  sim_cpu *scpu = STATE_CPU (sd, 0); /* FIXME */

  /* Set the initial register set.  */
  l = issue_messages;
  issue_messages = 0;
  set_initial_gprs ();
  issue_messages = l;
  
  if (prog_bfd != NULL)
    cpu.asregs.regs[PC_REGNO] = bfd_get_start_address (prog_bfd);

  /* Copy args into target memory.  */
  avp = argv;
  for (argc = 0; avp && *avp; avp++)
    argc++;

  /* Target memory looks like this:
     0x00000000 zero word
     0x00000004 argc word
     0x00000008 start of argv
     .
     0x0000???? end of argv
     0x0000???? zero word 
     0x0000???? start of data pointed to by argv  */

  wlat (scpu, 0, 0, 0);
  wlat (scpu, 0, 4, argc);

  /* tp is the offset of our first argv data.  */
  tp = 4 + 4 + argc * 4 + 4;

  for (i = 0; i < argc; i++)
    {
      /* Set the argv value.  */
      wlat (scpu, 0, 4 + 4 + i * 4, tp);

      /* Store the string.  */
      sim_core_write_buffer (sd, scpu, write_map, argv[i],
			     tp, strlen(argv[i])+1);
      tp += strlen (argv[i]) + 1;
    }

  wlat (scpu, 0, 4 + 4 + i * 4, 0);

  load_dtb (sd, DTB);

  return SIM_RC_OK;
}

/* Print statistics from simulator */
void sim_info (SIM_DESC sd, int verbose) {
  callback->printf_filtered (callback, "\n\n# instructions executed  %llu\n",
			     cpu.asregs.insts);
}

int sim_trace(SIM_DESC sd) {
  if (tracefile == 0)
    tracefile = fopen("trace.csv", "wb");

  tracing = 1;
  
  sim_resume (sd, 0, 0);

  tracing = 0;
  
  return 1;
}

/* Legacy stuff */
void sim_set_callbacks (host_callback *ptr) {
  callback = ptr;
}

void sim_size(int s) {
}


