#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t avail_pid = 1;

#define OPT_CALC    "calc"
#define OPT_ALLOC   "alloc"
#define OPT_FREE    "free"
#define OPT_READ    "read"
#define OPT_WRITE   "write"
#define OPT_SYSCALL "syscall"

static enum ins_opcode_t get_opcode(char* opt) {
    if (!strcmp(opt, OPT_CALC))   return CALC;
    if (!strcmp(opt, OPT_ALLOC))  return ALLOC;
    if (!strcmp(opt, OPT_FREE))   return FREE;
    if (!strcmp(opt, OPT_READ))   return READ;
    if (!strcmp(opt, OPT_WRITE))  return WRITE;
    if (!strcmp(opt, OPT_SYSCALL))return SYSCALL;

    printf("Invalid opcode: %s\n", opt);
    exit(1);
}

struct pcb_t* load(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) {
        printf("Cannot open process at %s\n", path);
        exit(1);
    }

    struct pcb_t* proc = malloc(sizeof(struct pcb_t));
    memset(proc, 0, sizeof(struct pcb_t));

    proc->pid = avail_pid++;
    proc->pc = 0;

    proc->code = malloc(sizeof(struct code_seg_t));

    fscanf(file, "%u %u", &proc->priority, &proc->code->size);
    proc->prio = proc->priority;
    proc->code->text = malloc(sizeof(struct inst_t) * proc->code->size);

    for (uint32_t i = 0; i < proc->code->size; i++) {
        char opcode[16];
        fscanf(file, "%s", opcode);

        proc->code->text[i].opcode = get_opcode(opcode);

        switch (proc->code->text[i].opcode) {
        case CALC:
            break;
        case ALLOC:
            fscanf(file, "%u %u",
                &proc->code->text[i].arg_0,
                &proc->code->text[i].arg_1);
            break;
        case FREE:
            fscanf(file, "%u",
                &proc->code->text[i].arg_0);
            break;
        case READ:
        case WRITE:
            fscanf(file, "%u %u %u",
                &proc->code->text[i].arg_0,
                &proc->code->text[i].arg_1,
                &proc->code->text[i].arg_2);
            break;

        case SYSCALL: {
            char buf[256];
            fgets(buf, sizeof(buf), file);
            sscanf(buf, "%u %u %u %u",
                &proc->code->text[i].arg_0,
                &proc->code->text[i].arg_1,
                &proc->code->text[i].arg_2,
                &proc->code->text[i].arg_3);
            break;
        }
        }
    }

    fclose(file);
    return proc;
}
