; ASM Library
; Angelina Abuhilal

;-----------------------------------------------------------------------------
; Hardware Target
;-----------------------------------------------------------------------------

; Target Platform: EK-TM4C123GXL
; Target uC:       TM4C123GH6PM
; System Clock:    40 MHz

; Hardware configuration:
; 16 MHz external crystal oscillator

;-----------------------------------------------------------------------------
; Device includes, defines, and assembler directives
;-----------------------------------------------------------------------------

    .def getPsp
    .def getMsp
    .def getControl
    .def getIpsr
    .def setPsp
    .def setAspOn
    .def setAspOff
    .def setPrivOff
    .def setPrivOn

;-----------------------------------------------------------------------------
; Register values and large immediate values
;-----------------------------------------------------------------------------

.thumb
.text

getPsp:
    MRS     r0, PSP
    BX      lr

getMsp:
    MRS     r0, MSP
    BX      lr

getControl:             ; to know if the ASP bit was turned on or not
    MRS     r0, CONTROL
    BX      lr

getIpsr:
    MRS     r0, IPSR
    BX      lr

setPsp:
    MSR     PSP, r0      ; the value to be set will be passed in r0
    ISB
    BX      lr

; the useful bits in control are bit 0 (ORR #0x1) Unprivileged software can be executed in Thread mode, otherwise only privileged can execute in Thread mode
;                                bit 1 (ORR #0x2) sets active stack pointer to 1(PSP - process stack(candy crush)) or 0(MSP - main stack(OS code))
;                                bit 2 (ORR #0x3) float point context active (floating point math)
; other bits should be preserved

setAspOn:;  SWITCH TO PSP
    MRS     r0, CONTROL
    ORR     r0, r0, #0x2 ; OR with 00000000000000000000000000000010 to turn bit on, keeping everything else the same
    MSR     CONTROL, r0
    ISB                  ;  instructions that were already fetched or partially executed before are discarded
    BX      lr

setAspOff:; SWITCH TO MSP
    MRS     r0, CONTROL
    BIC     r0, r0, #0x2 ; turns bit off, keeping everything else the same
    MSR     CONTROL, r0
    ISB                  ;  instructions that were already fetched or partially executed before are discarded
    BX      lr

setPrivOff:
    MRS     r0, CONTROL
    ORR     r0, r0, #0x1 ; OR with 00000000000000000000000000000001 to turn bit on, keeping everything else the same
    MSR     CONTROL, r0
    ISB                  ;  instructions that were already fetched or partially executed before are discarded
    BX      lr

setPrivOn:
    MRS     r0, CONTROL
    BIC     r0, r0, #0x1 ; turn bit off, keeping everything else the same
    MSR     CONTROL, r0
    ISB                  ;  instructions that were already fetched or partially executed before are discarded
    BX      lr