// Flash command execute-and-wait, translated from ARM Compiler assembler.
//
// The original (IAP.s) used armasm directives — AREA, EXPORT, bare labels —
// which GNU as does not accept, and the online compiler was the only place it
// ever built. The instructions themselves are unchanged.
//
// Why this lives in RAM: the KL25Z cannot fetch instructions from flash while
// the flash controller is busy, so a routine that starts a flash command and
// then polls for its completion has to run from somewhere else. Putting it in
// .data does that — the startup code copies that section into RAM before main
// runs — which is what AREA ... READWRITE achieved before.

    .syntax unified
    .thumb

    .section .data.iap_ram_asm_code, "awx", %progbits
    .align 2

    .global iapExecAndWait
    .thumb_func
iapExecAndWait:
    push    {r1, r2, lr}
    cpsid   i                   // set PRIMASK to disable interrupts
    dsb                         // data synchronization barrier
    isb                         // instruction synchronization barrier
    ldr     r0, FTFA_FSTAT
    movs    r2, #0x80           // CCIF
    strb    r2, [r0]            // FTFA->FSTAT = CCIF, launching the command
Lew0:
    ldrb    r1, [r0]            // r1 <- FTFA->FSTAT
    tst     r1, r2              // test r1 & CCIF
    beq     Lew0                // zero: the command is still running
    cpsie   i
    pop     {r1, r2, pc}

    .align 2
FTFA_FSTAT:
    .word   0x40020000
