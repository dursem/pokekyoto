.cpu arm7tdmi
.syntax unified
.arm
.text
.global KyotoCloudShadePixels
KyotoCloudShadePixels:
    push {r4-r11,lr}
    sub sp,sp,#16
    str r0,[sp]
    str r1,[sp,#4]
    mov r10,r2
    mov r11,r3
    ldr r0,[r11,#24]
    str r0,[sp,#8]
    mov r0,#0
    str r0,[sp,#12]
    ldr r0,[r11,#20]
    ldr r1,[r11,#28]
    cmp r0,r1
    bge .Lshade_done
    ldr r0,[r11,#24]
    ldr r1,[r11,#32]
    cmp r0,r1
    bge .Lshade_done
.Lshade_row:
    ldr r0,[sp,#8]
    ldr r1,[r11,#4]
    sub r0,r0,r1
    ldr r2,[r11,#16]
    tst r2,#8192
    ldrne r1,[r11,#12]
    subne r0,r1,r0
    subne r0,r0,#1
    ldr r1,[r11,#8]
    and r3,r0,#7
    lsr r0,r0,#3
    mla r0,r1,r0,r3
    ldr r4,[sp,#4]
    add r4,r4,r0,lsl #2
    ldr r6,[r11,#20]
    ldr r0,[r11]
    sub r6,r6,r0
    tst r2,#4096
    subne r6,r1,r6
    subne r6,r6,#1
    and r0,r6,#7
    lsr r6,r6,#3
    add r4,r4,r6,lsl #5
    ldr r6,[r4]
    beq .Lshade_forward_start
    add r9,r0,#1
    rsb r0,r0,#7
    lsl r0,r0,#2
    lsl r6,r6,r0
    b .Lshade_dest_start
.Lshade_forward_start:
    rsb r9,r0,#8
    lsl r0,r0,#2
    lsr r6,r6,r0
.Lshade_dest_start:
    ldr r0,[sp,#8]
    ldr r1,[r11,#40]
    sub r0,r0,r1
    and r1,r0,#7
    lsr r0,r0,#3
    ldr r5,[sp]
    add r5,r5,r0,lsl #8
    add r5,r5,r1,lsl #2
    ldr r7,[r11,#20]
    ldr r0,[r11,#36]
    sub r7,r7,r0
    and r12,r7,#7
    lsl r12,r12,#2
    lsr r7,r7,#3
    add r5,r5,r7,lsl #5
    ldr r7,[r5]
    ror r7,r7,r12
    ldr r8,[r11,#28]
    ldr r0,[r11,#20]
    sub r8,r8,r0
    adr lr,.Lshade_tones
    tst r2,#4096
    bne .Lshade_reverse

@ Work a tile row as packed 32-bit words. Each source/destination word is
@ loaded once for eight pixels, instead of a byte read/modify/write per pixel.
@ Return nonzero when anything was shaded; callers do not require a count.
.macro SHADE_LOOP name, reverse
\name:
    cmp r6,#0
    beq 2f
    .if \reverse
    movs r0,r6,lsr #28
    lsl r6,r6,#4
    .else
    ands r0,r6,#15
    lsr r6,r6,#4
    .endif
    beq 1f
    ands r1,r7,#15
    beq 1f
    ldrb r1,[lr,r1]
    ldrb r0,[r10,r0]
    add r0,r0,r1
    add r0,r0,#4
    bic r7,r7,#15
    orr r7,r7,r0
    str r0,[sp,#12]
1:
    ror r7,r7,#4
    add r12,r12,#4
    cmp r12,#32
    streq r7,[r5]
    moveq r12,#0
    addeq r5,r5,#32
    ldreq r7,[r5]
    subs r8,r8,#1
    beq .Lshade_row_finished
    subs r9,r9,#1
    .if \reverse
    ldreq r6,[r4,#-32]!
    .else
    ldreq r6,[r4,#32]!
    .endif
    moveq r9,#8
    b \name
2:
    @ Skip a fully transparent source run, bounded by both tile rows.
    mov r0,r9
    cmp r0,r8
    movhi r0,r8
    rsb r1,r12,#32
    lsr r1,r1,#2
    cmp r0,r1
    movhi r0,r1
    lsl r1,r0,#2
    ror r7,r7,r1
    add r12,r12,r1
    cmp r12,#32
    streq r7,[r5]
    moveq r12,#0
    addeq r5,r5,#32
    ldreq r7,[r5]
    subs r8,r8,r0
    beq .Lshade_row_finished
    subs r9,r9,r0
    .if \reverse
    ldreq r6,[r4,#-32]!
    .else
    ldreq r6,[r4,#32]!
    .endif
    moveq r9,#8
    b \name
.endm
    SHADE_LOOP .Lshade_forward,0
    SHADE_LOOP .Lshade_reverse,1
.Lshade_row_finished:
    cmp r12,#0
    rsbne r0,r12,#32
    rorne r7,r7,r0
    strne r7,[r5]
    ldr r0,[sp,#8]
    add r0,r0,#1
    str r0,[sp,#8]
    ldr r1,[r11,#32]
    cmp r0,r1
    blt .Lshade_row
.Lshade_done:
    ldr r0,[sp,#12]
    add sp,sp,#16
    pop {r4-r11,lr}
    bx lr
.align 2
.Lshade_tones: .byte 0,0,3,6,0,0,0,3,3,3,6,6,6,0,0,0
.global KyotoCloudShadePixelsEnd
KyotoCloudShadePixelsEnd:
