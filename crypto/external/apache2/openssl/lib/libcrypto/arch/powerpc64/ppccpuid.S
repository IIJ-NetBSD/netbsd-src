.machine	"any"
.text

.globl	OPENSSL_fpu_probe
.type	OPENSSL_fpu_probe,@function
.section	".opd","aw"
.align	3
OPENSSL_fpu_probe:
.quad	.OPENSSL_fpu_probe,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_fpu_probe:
	fmr	0,0
	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0

.globl	OPENSSL_ppc64_probe
.type	OPENSSL_ppc64_probe,@function
.section	".opd","aw"
.align	3
OPENSSL_ppc64_probe:
.quad	.OPENSSL_ppc64_probe,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_ppc64_probe:
	fcfid	1,1
	rldicl	0,0,32,32
	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0


.globl	OPENSSL_altivec_probe
.type	OPENSSL_altivec_probe,@function
.section	".opd","aw"
.align	3
OPENSSL_altivec_probe:
.quad	.OPENSSL_altivec_probe,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_altivec_probe:
.long	0x10000484
	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0


.globl	OPENSSL_crypto207_probe
.type	OPENSSL_crypto207_probe,@function
.section	".opd","aw"
.align	3
OPENSSL_crypto207_probe:
.quad	.OPENSSL_crypto207_probe,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_crypto207_probe:
.long	0x7C000E99
.long	0x10000508
	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0


.globl	OPENSSL_madd300_probe
.type	OPENSSL_madd300_probe,@function
.section	".opd","aw"
.align	3
OPENSSL_madd300_probe:
.quad	.OPENSSL_madd300_probe,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_madd300_probe:
	xor	0,0,0
.long	0x10600033
.long	0x10600031
	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0

.globl	OPENSSL_brd31_probe
.type	OPENSSL_brd31_probe,@function
.section	".opd","aw"
.align	3
OPENSSL_brd31_probe:
.quad	.OPENSSL_brd31_probe,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_brd31_probe:
	xor	0,0,0
.long	0x7C030176
	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0



.globl	OPENSSL_wipe_cpu
.type	OPENSSL_wipe_cpu,@function
.section	".opd","aw"
.align	3
OPENSSL_wipe_cpu:
.quad	.OPENSSL_wipe_cpu,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_wipe_cpu:
	xor	0,0,0
	fmr	0,31
	fmr	1,31
	fmr	2,31
	mr	3,1
	fmr	3,31
	xor	4,4,4
	fmr	4,31
	xor	5,5,5
	fmr	5,31
	xor	6,6,6
	fmr	6,31
	xor	7,7,7
	fmr	7,31
	xor	8,8,8
	fmr	8,31
	xor	9,9,9
	fmr	9,31
	xor	10,10,10
	fmr	10,31
	xor	11,11,11
	fmr	11,31
	xor	12,12,12
	fmr	12,31
	fmr	13,31
	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0


.globl	OPENSSL_atomic_add
.type	OPENSSL_atomic_add,@function
.section	".opd","aw"
.align	3
OPENSSL_atomic_add:
.quad	.OPENSSL_atomic_add,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_atomic_add:
.Ladd:	lwarx	5,0,3
	add	0,4,5
	stwcx.	0,0,3
	bne-	.Ladd
	extsw	3,0
	blr	
.long	0
.byte	0,12,0x14,0,0,0,2,0
.long	0


.globl	OPENSSL_rdtsc_mftb
.type	OPENSSL_rdtsc_mftb,@function
.section	".opd","aw"
.align	3
OPENSSL_rdtsc_mftb:
.quad	.OPENSSL_rdtsc_mftb,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_rdtsc_mftb:
	mftb	3
	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0


.globl	OPENSSL_rdtsc_mfspr268
.type	OPENSSL_rdtsc_mfspr268,@function
.section	".opd","aw"
.align	3
OPENSSL_rdtsc_mfspr268:
.quad	.OPENSSL_rdtsc_mfspr268,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_rdtsc_mfspr268:
	mfspr	3,268
	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0


.globl	OPENSSL_cleanse
.type	OPENSSL_cleanse,@function
.section	".opd","aw"
.align	3
OPENSSL_cleanse:
.quad	.OPENSSL_cleanse,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_cleanse:
	cmpldi	4,7
	li	0,0
	bge	.Lot
	cmpldi	4,0
	beqlr
.Little:	mtctr	4
	stb	0,0(3)
	addi	3,3,1
	bc	16,0,$-8
	blr	
.Lot:	andi.	5,3,3
	beq	.Laligned
	stb	0,0(3)
	subi	4,4,1
	addi	3,3,1
	b	.Lot
.Laligned:
	srdi	5,4,2
	mtctr	5
	stw	0,0(3)
	addi	3,3,4
	bc	16,0,$-8
	andi.	4,4,3
	bne	.Little
	blr	
.long	0
.byte	0,12,0x14,0,0,0,2,0
.long	0


.globl	CRYPTO_memcmp
.type	CRYPTO_memcmp,@function
.section	".opd","aw"
.align	3
CRYPTO_memcmp:
.quad	.CRYPTO_memcmp,.TOC.@tocbase,0
.previous
.align	4
.CRYPTO_memcmp:
	cmpldi	5,0
	li	0,0
	beq	.Lno_data
	mtctr	5
.Loop_cmp:
	lbz	6,0(3)
	addi	3,3,1
	lbz	7,0(4)
	addi	4,4,1
	xor	6,6,7
	or	0,0,6
	bc	16,0,.Loop_cmp

.Lno_data:
	li	3,0
	sub	3,3,0
	extrwi	3,3,1,0
	blr	
.long	0
.byte	0,12,0x14,0,0,0,3,0
.long	0

.globl	OPENSSL_instrument_bus_mftb
.type	OPENSSL_instrument_bus_mftb,@function
.section	".opd","aw"
.align	3
OPENSSL_instrument_bus_mftb:
.quad	.OPENSSL_instrument_bus_mftb,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_instrument_bus_mftb:
	mtctr	4

	mftb	7
	li	8,0

	dcbf	0,3
	lwarx	6,0,3
	add	6,6,8
	stwcx.	6,0,3
	stwx	6,0,3

.Loop:	mftb	6
	sub	8,6,7
	mr	7,6
	dcbf	0,3
	lwarx	6,0,3
	add	6,6,8
	stwcx.	6,0,3
	stwx	6,0,3
	addi	3,3,4
	bc	16,0,.Loop

	mr	3,4
	blr	
.long	0
.byte	0,12,0x14,0,0,0,2,0
.long	0


.globl	OPENSSL_instrument_bus2_mftb
.type	OPENSSL_instrument_bus2_mftb,@function
.section	".opd","aw"
.align	3
OPENSSL_instrument_bus2_mftb:
.quad	.OPENSSL_instrument_bus2_mftb,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_instrument_bus2_mftb:
	mr	0,4
	slwi	4,4,2

	mftb	7
	li	8,0

	dcbf	0,3
	lwarx	6,0,3
	add	6,6,8
	stwcx.	6,0,3
	stwx	6,0,3

	mftb	6
	sub	8,6,7
	mr	7,6
	mr	9,8
.Loop2:
	dcbf	0,3
	lwarx	6,0,3
	add	6,6,8
	stwcx.	6,0,3
	stwx	6,0,3

	addic.	5,5,-1
	beq	.Ldone2

	mftb	6
	sub	8,6,7
	mr	7,6
	cmplw	7,8,9
	mr	9,8

	mfcr	6
	not	6,6
	rlwinm	6,6,1,29,29

	sub.	4,4,6
	add	3,3,6
	bne	.Loop2

.Ldone2:
	srwi	4,4,2
	sub	3,0,4
	blr	
.long	0
.byte	0,12,0x14,0,0,0,3,0
.long	0


.globl	OPENSSL_instrument_bus_mfspr268
.type	OPENSSL_instrument_bus_mfspr268,@function
.section	".opd","aw"
.align	3
OPENSSL_instrument_bus_mfspr268:
.quad	.OPENSSL_instrument_bus_mfspr268,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_instrument_bus_mfspr268:
	mtctr	4

	mfspr	7,268
	li	8,0

	dcbf	0,3
	lwarx	6,0,3
	add	6,6,8
	stwcx.	6,0,3
	stwx	6,0,3

.Loop3:	mfspr	6,268
	sub	8,6,7
	mr	7,6
	dcbf	0,3
	lwarx	6,0,3
	add	6,6,8
	stwcx.	6,0,3
	stwx	6,0,3
	addi	3,3,4
	bc	16,0,.Loop3

	mr	3,4
	blr	
.long	0
.byte	0,12,0x14,0,0,0,2,0
.long	0


.globl	OPENSSL_instrument_bus2_mfspr268
.type	OPENSSL_instrument_bus2_mfspr268,@function
.section	".opd","aw"
.align	3
OPENSSL_instrument_bus2_mfspr268:
.quad	.OPENSSL_instrument_bus2_mfspr268,.TOC.@tocbase,0
.previous
.align	4
.OPENSSL_instrument_bus2_mfspr268:
	mr	0,4
	slwi	4,4,2

	mfspr	7,268
	li	8,0

	dcbf	0,3
	lwarx	6,0,3
	add	6,6,8
	stwcx.	6,0,3
	stwx	6,0,3

	mfspr	6,268
	sub	8,6,7
	mr	7,6
	mr	9,8
.Loop4:
	dcbf	0,3
	lwarx	6,0,3
	add	6,6,8
	stwcx.	6,0,3
	stwx	6,0,3

	addic.	5,5,-1
	beq	.Ldone4

	mfspr	6,268
	sub	8,6,7
	mr	7,6
	cmplw	7,8,9
	mr	9,8

	mfcr	6
	not	6,6
	rlwinm	6,6,1,29,29

	sub.	4,4,6
	add	3,3,6
	bne	.Loop4

.Ldone4:
	srwi	4,4,2
	sub	3,0,4
	blr	
.long	0
.byte	0,12,0x14,0,0,0,3,0
.long	0

