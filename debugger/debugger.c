/*
 * Copyright (c) 2013, Peter Rutenbar <pruten@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <histedit.h>

#include "debugger.h"

struct dbg_state_t {
    EditLine *el;
    uint8_t running;
    uint64_t breakpoint_counter;
    dbg_breakpoint_t *breakpoints;
    _Bool trace;
    uint32_t slow_factor;
    
    uint64_t op_count[0x10000];

};

struct dbg_state_t dbg_state;


void print_mmu_rp(uint64_t rp)
{
    printf("lu=%u limit=0x%x sg=%u dt=%u addr=0x%08x\n", rp_lu(rp), rp_limit(rp), rp_sg(rp), rp_dt(rp), rp_addr(rp));
}

void printregs()
{
	printf("[d0]%08x  [d1]%08x  [d2]%08x  [d3]%08x\n", shoe.d[0], shoe.d[1], shoe.d[2], shoe.d[3]);
	printf("[d4]%08x  [d5]%08x  [d6]%08x  [d7]%08x\n", shoe.d[4], shoe.d[5], shoe.d[6], shoe.d[7]);
	printf("[a0]%08x  [a1]%08x  [a2]%08x  [a3]%08x\n", shoe.a[0], shoe.a[1], shoe.a[2], shoe.a[3]);
	printf("[a4]%08x  [a5]%08x  [a6]%08x  [a7]%08x\n", shoe.a[4], shoe.a[5], shoe.a[6], shoe.a[7]);
	printf("[pc]%08x  [sr]%c%c%c%c%c%c%c  [tc]%08x\n", shoe.pc,
           sr_s()?'S':'s',
           sr_m()?'M':'m',
           sr_x()?'X':'x',
           sr_n()?'N':'n',
           sr_z()?'Z':'z',
           sr_v()?'V':'v',
           sr_c()?'C':'c',
           shoe.tc
           );
    
    printf("[vbr]%08x\n", shoe.vbr);
    
    printf("srp: ");
    print_mmu_rp(shoe.srp);
    
    printf("crp: ");
    print_mmu_rp(shoe.crp);
    
    printf("tc: e=%u sre=%u fcl=%u ps=%u is=%u (tia=%u tib=%u tic=%u tid=%u)\n",
           _tc_enable(), _tc_sre(), tc_fcl(), _tc_ps(), _tc_is(), tc_tia(), tc_tib(), tc_tic(), tc_tid());
    
    printf("\n");
}

void print_pc()
{
    char str[1024];
    uint8_t binary[32];
    uint32_t i;
    uint32_t len;
    const char *name = NULL;
    
    if ((shoe.pc >= 0x40000000) && (shoe.pc < 0x50000000)) {
        uint32_t i, addr = shoe.pc % (shoe.physical_rom_size);
        for (i=0; macii_rom_symbols[i].name; i++) {
            if (macii_rom_symbols[i].addr > addr) {
                break;
            }
            name = macii_rom_symbols[i].name;
        }
    }
    else if (sr_s()) { // these symbols are only meaningful in supervisor mode
        coff_symbol *symb = coff_find_func(shoe.coff, shoe.pc);
        if (symb && strlen(symb->name))
            name = symb->name;
    }
    else {
        if ((shoe.pc >= 0x10000000) && (shoe.pc < 0x20000000)) {
            uint32_t i, addr = shoe.pc % (shoe.physical_rom_size);
            for (i=0; macii_rom_symbols[i].name; i++) {
                if (macii_rom_symbols[i].addr > addr) {
                    break;
                }
                name = macii_rom_symbols[i].name;
            }
        }
        else {
            name = "";
        }
    }
    
    const uint16_t old_abort = shoe.abort;
    shoe.suppress_exceptions = 1;
    
    for (i=0; i<32; i++) {
        binary[i] = (uint8_t) lget(shoe.pc+i, 1);
    }
    
    disassemble_inst(binary, shoe.pc, str, &len);
    
    printf("*0x%08x %s [ ", shoe.pc, name ? name : "");
    for (i=0; i<len; i+=2) {
        printf("%02x%02x ", binary[i], binary[i+1]);
    }
    printf("]  %s\n", str);
    
    shoe.abort = old_abort;
    shoe.suppress_exceptions = 0;
    
}


static void dump_proc(uint32_t procnum)
{
    uint32_t u_proc_p;
    uint16_t pid;
    uint8_t do_print = 0, cpuflag;
    
    // Only dump this process state if we're in user mode
    if (sr_s())
        return ;
    
    shoe.suppress_exceptions = 1;
    cpuflag = lget(0x0000012f, 1);
    set_sr_s(1); // set supervisor mode so we can access the proc structure
    
    u_proc_p = lget(0x1ff01000, 4);
    if (shoe.abort)
        goto done;
    
    pid = lget(u_proc_p + 0x26, 2);
    if (shoe.abort)
        goto done;
    
    do_print = 1;
    
done:

    set_sr_s(0);
    shoe.abort = 0;
    shoe.suppress_exceptions = 0;

    if (do_print) {
        printf("pid = %u, cpuflag=0x%02x\n", pid, cpuflag);
        // print_pc();
        // printregs();
    }

}

void verb_backtrace_handler (const char *line)
{
    const uint32_t old_abort = shoe.abort;
    shoe.suppress_exceptions = 1;
    shoe.abort = 0;
    
    // link
    //   push a6 to a7
    //   set a6 = a7
    //   set a7 = a7 - (some stack space)
    
    // jsr
    //   push return pointer to a7
    
    // call
    //   set a7 = a7 - (some stack space)
    //   push arguments to a7
    //   push return pointer to a7
    //   (jump to function)
    //   push 
    
    // bt algorithm
    //   set a7 = a6
    //   pop a7 -> a6
    //   pop a7 -> return pointer
    
    
    uint32_t i, j, a7, a6 = shoe.a[6];
    coff_symbol *symb;
    
    if (sr_s()) {
        symb = coff_find_func(shoe.coff, shoe.pc);
        printf("%u:  *0x%08x  %s+%u\n", 0, shoe.pc, (symb && strlen(symb->name))?symb->name:"?", shoe.pc - symb->value);
    }
    else
        printf("%u:  *0x%08x\n", 0, shoe.pc);
    
    for (i=1; 1; i++) {
        a7 = a6;
        const uint32_t last_a6 = lget(a7, 4);
        const uint32_t last_pc = lget(a7+4, 4);
        
        if ((last_a6 - a6) <= 1000) {
            printf("    {");
            for (j = a6+8; j < last_a6; j+=4) {
                uint32_t data = lget(j, 4);
                printf("%x, ", data);
            }
            printf("}\n");
        }
        
        if (sr_s()) {
            symb = coff_find_func(shoe.coff, last_pc);
            printf("%u:  *0x%08x  %s+%u\n", i, last_pc, (symb && strlen(symb->name))?symb->name:"?", last_pc - symb->value);
        }
        else
            printf("%u:  *0x%08x\n", i, last_pc);
    
        if ((last_a6 - a6) > 1000) {
            break;
        }
        
        a6 = last_a6;
    }
    
    shoe.suppress_exceptions = 0;
    shoe.abort = old_abort;
}

void verb_break_handler (const char *line)
{
    errno = 0;
    const uint32_t addr = (uint32_t) strtoul(line, NULL, 0);
    
    if (errno) {
        printf("errno: %d\n", errno);
        return ;
    }
    
    dbg_breakpoint_t *brk = calloc(sizeof(dbg_breakpoint_t), 1);
    brk->next = NULL;
    brk->addr = addr;
    brk->num = dbg_state.breakpoint_counter++;
    
    dbg_breakpoint_t **cur = &dbg_state.breakpoints;
    while (*cur)
        cur = &(*cur)->next;
    *cur = brk;
    
    printf("Set breakpoint %llu = *0x%08x\n", brk->num, brk->addr);
}

void verb_delete_handler (const char *line)
{
    errno = 0;
    uint64_t num = strtoull(line, NULL, 0);
    
    if (errno) {
        printf("errno: %d\n", errno);
        return ;
    }
    
    dbg_breakpoint_t **cur = &dbg_state.breakpoints;
    while (*cur) {
        if ((*cur)->num == num) {
            dbg_breakpoint_t *victim = *cur;
            *cur = (*cur)->next;
            free(victim);
            return ;
        }
        cur = &(*cur)->next;
    }
    
    printf("No such breakpoint (#%llu)\n", num);
}

 
void verb_help_handler (const char *line)
{
    printf("Help help help\n");
}


void verb_stepi_handler (const char *line)
{
    dbg_state.running = 1;
    cpu_step();
    dbg_state.running = 0;
    print_pc();
}

void verb_registers_handler (const char *line)
{
    printregs();
}

void verb_trace_toggle_handler (const char *line)
{
    dbg_state.trace = !dbg_state.trace;
}

void verb_examine_handler (const char *line)
{
    uint32_t addr = (uint32_t)strtoul(line, NULL, 0);
    uint32_t old_suppress = shoe.suppress_exceptions;
    
    shoe.suppress_exceptions = 1;
    printf("(uint32_t)*0x%08x = 0x%08x\n", addr, (uint32_t)lget(addr, 4));
    shoe.suppress_exceptions = old_suppress;
    
}

void verb_lookup_handler (const char *line)
{
    char *sym_name = malloc(strlen(line)+1);
    
    sscanf(line, "%s", sym_name);
    coff_symbol *symb = coff_find_symbol(shoe.coff, sym_name);
    
    free(sym_name);
    
    if (symb == NULL) {
        printf("Couldn't find \"%s\"\n", sym_name);
        return ;
    }
    
    printf("%s = *0x%08x\n", symb->name, symb->value);
}


void stepper()
{
    dbg_breakpoint_t *cur;
    
    if (shoe.cpu_thread_notifications) {
        
        // If there's an interrupt pending
        if (shoe.cpu_thread_notifications & 0xff) {
            // process_pending_interrupt() may clear SHOEBILL_STATE_STOPPED
            process_pending_interrupt();
        }
        
        if (shoe.cpu_thread_notifications & SHOEBILL_STATE_STOPPED) {
            // I think it's safe to ignore STOP instructions...
        }
    }
    
    cpu_step();
    
    
    
    if (dbg_state.trace) {
        print_pc();
        printregs();
    }
    
    for (cur = dbg_state.breakpoints; cur != NULL; cur = cur->next) {
        if (shoe.pc == cur->addr) {
            printf("Hit breakpoint %llu *0x%08x\n", cur->num, shoe.pc);
            dbg_state.running = 0;
            return ;
        }
    }
}

void verb_continue_handler (const char *line)
{
    dbg_state.running = 1;
    while (dbg_state.running) {
        if (dbg_state.slow_factor)
            usleep(dbg_state.slow_factor);
        stepper();
    }
    print_pc();
}

void verb_quit_handler (const char *line)
{
    printf("Quitting\n");
    fflush(stdout);
    exit(0);
}

void verb_reset_handler (const char *line)
{
    p_free_pool(shoe.pool);
    shoe.pool = NULL;
}

void verb_slow_handler (const char *line)
{
    const uint64_t usecs = strtoul(line, NULL, 0);
    printf("Slow factor %u -> %u\n", dbg_state.slow_factor, (uint32_t)usecs);
    dbg_state.slow_factor = usecs;
}

struct verb_handler_table_t {
    const char *name;
    void (*func)(const char *);
} verb_handler_table[] =
{
    {"quit", verb_quit_handler},
    {"continue", verb_continue_handler},
    {"help", verb_help_handler},
    {"registers", verb_registers_handler},
    {"stepi", verb_stepi_handler},
    {"backtrace", verb_backtrace_handler},
    {"bt", verb_backtrace_handler},
    {"break", verb_break_handler},
    {"delete", verb_delete_handler},
    {"lookup", verb_lookup_handler},
    {"trace", verb_trace_toggle_handler},
    {"x", verb_examine_handler},
    {"reset", verb_reset_handler},
    {"slow", verb_slow_handler},
};

void execute_verb (const char *line)
{
    char verb[128];
	uint32_t max_len=0, max_i=0;
	const char *remainder;
    uint32_t i, matches = 0, match_i;
    
    if (sscanf(line, "%127s", verb) != 1)
        return ;
	
	// Skip past the verb
	for (remainder = line; *remainder && !isspace(*remainder); remainder++)
        ;
	
	// Skip past the space between the verb and the arguments
	for (; *remainder && isspace(*remainder); remainder++)
        ;

    const uint32_t verb_len = strlen(verb);
    for (i=0; i < (sizeof(verb_handler_table) / sizeof(struct verb_handler_table_t)); i++) {
        const uint32_t i_len = strlen(verb_handler_table[i].name);
        
        // If it's a perfect match,
        if (strcasecmp(verb, verb_handler_table[i].name)==0) {
            verb_handler_table[i].func(remainder);
            return ;
        }
        
        // Otherwise, see if it's a partial match
        if ((i_len >= verb_len) && strncasecmp(verb, verb_handler_table[i].name, verb_len)==0) {
            matches++;
            match_i = i;
        }
    }

    // Only execute the verb if it's an unambiguous match (matches == 1)
    if (matches == 1) {
        verb_handler_table[match_i].func(remainder);
        return ;
    }
	
    printf("  %s?\n", verb);
}

char *cli_prompt_callback(EditLine *el)
{
	return "~ ";
}

// Hack to clear line after ^C. el_reset() screws up tty when called from the signal handler.
void ch_reset(EditLine *el, int mclear);

void signal_callback(int sig)
{
    EditLine *el = dbg_state.el;
    (void) signal(SIGINT, signal_callback);
    (void) signal(SIGWINCH, signal_callback);
    
    switch (sig) {
        case SIGWINCH:
            el_resize(el);
            break ;
        case SIGINT:
            if (dbg_state.running) {
                dbg_state.running = 0;
            }
            else {
                printf("\n");
                ch_reset(el, 0);
                el_set(el, EL_REFRESH);
            }
            break ;
    }
    
	return ;
}
 
void *cpu_debugger_thread (void *arg)
{
    EditLine *el;
    History *hist;
	HistEvent histev;
	
	const char *buf;
	int num;
	
	hist = history_init();
	history(hist, &histev, H_SETSIZE, 10000); // Remember 10000 previous user inputs
    
	el = el_init("Shoebill", stdin, stdout, stderr);
	dbg_state.el = el;
    
	el_set(el, EL_SIGNAL, 0);
	el_set(el, EL_PROMPT, cli_prompt_callback);
	el_set(el, EL_EDITOR, "emacs");
	el_set(el, EL_HIST, history, hist);
	
	(void) signal(SIGINT, signal_callback);
    (void) signal(SIGWINCH, signal_callback);
    
	while ((buf = el_gets(el, &num)) != NULL) {
		if (strcmp(buf, "\n")!=0) {
			execute_verb(buf);
			history(hist, &histev, H_ENTER, buf);
		}
	}
	
	el_end(el);
	history_end(hist);
	return NULL;
}
