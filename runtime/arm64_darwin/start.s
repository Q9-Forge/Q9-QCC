// QCC ARM64/Darwin Plattform-Runtime: bewusst klein und ohne C-Laufzeit.
// Vertragsgrenze zum Backend: _tc_main, _tc_putint(w0), _tc_exit(w0).
	.text
	.p2align 2
	.globl _start
_start:
	bl _tc_main
	b  _tc_exit

// signed int32 aus w0 als Dezimalzahl mit Newline nach stdout schreiben.
	.globl _tc_putint
_tc_putint:
	sub sp,sp,#64
	mov x9,sp
	add x10,sp,#63
	mov w11,#10
	mov w12,#0
	sxtw x0,w0
	cmp x0,#0
	b.ge 1f
	neg x0,x0
	mov w12,#1
1:
	mov w13,#10
	strb w13,[x10]
	sub x10,x10,#1
	cbnz x0,2f
	mov w13,#'0'
	strb w13,[x10]
	sub x10,x10,#1
	b 4f
2:
	udiv x13,x0,x11
	msub x14,x13,x11,x0
	add w14,w14,#'0'
	strb w14,[x10]
	sub x10,x10,#1
	mov x0,x13
	cbnz x0,2b
4:
	cbz w12,5f
	mov w13,#'-'
	strb w13,[x10]
	sub x10,x10,#1
5:
	add x1,x10,#1
	add x2,x9,#64
	sub x2,x2,x1
	mov x0,#1
	movz x16,#4
	movk x16,#0x2000,lsl #16
	svc #0x80
	add sp,sp,#64
	ret

// unsigned int32 aus w0 als Dezimalzahl mit Newline nach stdout schreiben.
	.globl _tc_putuint
_tc_putuint:
	sub sp,sp,#64
	mov x9,sp
	add x10,sp,#63
	mov w11,#10
	uxtw x0,w0
	mov w13,#10
	strb w13,[x10]
	sub x10,x10,#1
	cbnz x0,6f
	mov w13,#'0'
	strb w13,[x10]
	sub x10,x10,#1
	b 8f
6:
	udiv x13,x0,x11
	msub x14,x13,x11,x0
	add w14,w14,#'0'
	strb w14,[x10]
	sub x10,x10,#1
	mov x0,x13
	cbnz x0,6b
8:
	add x1,x10,#1
	add x2,x9,#64
	sub x2,x2,x1
	mov x0,#1
	movz x16,#4
	movk x16,#0x2000,lsl #16
	svc #0x80
	add sp,sp,#64
	ret

// Das niederwertige Byte aus w0 nach stdout schreiben.
	.globl _tc_putchar
_tc_putchar:
	sub sp,sp,#16
	strb w0,[sp]
	mov x0,#1
	mov x1,sp
	mov x2,#1
	movz x16,#4
	movk x16,#0x2000,lsl #16
	svc #0x80
	add sp,sp,#16
	ret

	.globl _tc_exit
_tc_exit:
	movz x16,#1
	movk x16,#0x2000,lsl #16
	svc #0x80
	brk #0
