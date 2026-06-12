	.file	"h0.c"
	.text
	.section	.rodata.str1.1,"aMS",@progbits,1
.LC0:
	.string	"1 32 160 4 a:21"
	.text
	.type	arena_new, @function
arena_new:
.LASANPC25:
.LFB25:
	.cfi_startproc
	pushq	%r14
	.cfi_def_cfa_offset 16
	.cfi_offset 14, -16
	pushq	%r12
	.cfi_def_cfa_offset 24
	.cfi_offset 12, -24
	pushq	%rbp
	.cfi_def_cfa_offset 32
	.cfi_offset 6, -32
	pushq	%rbx
	.cfi_def_cfa_offset 40
	.cfi_offset 3, -40
	subq	$264, %rsp
	.cfi_def_cfa_offset 304
	movq	%rdi, %rbx
	movq	%rsi, %r12
	movq	%rsp, %rbp
	movq	%rsp, %r14
	cmpl	$0, __asan_option_detect_stack_use_after_return(%rip)
	jne	.L26
.L1:
	leaq	256(%rbp), %rsi
	movq	$1102416563, 0(%rbp)
	leaq	.LC0(%rip), %rax
	movq	%rax, 8(%rbp)
	leaq	.LASANPC25(%rip), %rax
	movq	%rax, 16(%rbp)
	movq	%rbp, %r10
	shrq	$3, %r10
	movl	$-235802127, 2147450880(%r10)
	movl	$-202116109, 2147450904(%r10)
	movl	$-202116109, 2147450908(%r10)
	leaq	32(%rbp), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L27
	movq	$0, -224(%rsi)
	testq	%r12, %r12
	movl	$65536, %eax
	cmove	%rax, %r12
	leaq	-216(%rsi), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L28
	movq	%r12, -216(%rsi)
	movl	$0, %eax
	leaq	-224(%rsi), %rcx
	movl	$8, %r9d
	subq	%rcx, %r9
	jmp	.L16
.L26:
	movl	$256, %edi
	call	__asan_stack_malloc_2@PLT
	testq	%rax, %rax
	cmovne	%rax, %rbp
	jmp	.L1
.L27:
	call	__asan_report_store8@PLT
.L28:
	call	__asan_report_store8@PLT
.L32:
	movq	%rdx, %rsi
	leaq	.Lubsan_data54(%rip), %rdi
	call	__ubsan_handle_out_of_bounds_abort@PLT
.L33:
	addq	%rdi, %r8
	jc	.L9
	movq	%rdi, %rsi
	leaq	.Lubsan_data55(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L10:
	cmpq	%rdi, %rcx
	jb	.L12
.L11:
	movslq	%eax, %rdx
	leaq	16(%rcx,%rdx,8), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L29
	movslq	%eax, %rdx
	movq	$0, -208(%rsi,%rdx,8)
	movl	%eax, %edx
	addl	$1, %edx
	jo	.L30
	movl	%edx, %eax
	cmpl	$15, %edx
	jg	.L31
.L16:
	movslq	%eax, %rdx
	cmpq	$16, %rdx
	jnb	.L32
	leal	2(%rax), %edx
	movslq	%edx, %rdx
	salq	$3, %rdx
	leaq	(%rcx,%rdx), %rdi
	leaq	(%rdi,%r9), %r8
	cmpq	$160, %r8
	ja	.L33
.L9:
	leaq	(%rcx,%rdx), %rdi
	testq	%rdx, %rdx
	js	.L10
	cmpq	%rcx, %rdi
	jnb	.L11
.L12:
	subq	$224, %rsi
	movq	%rdi, %rdx
	leaq	.Lubsan_data56(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L29:
	call	__asan_report_store8@PLT
.L30:
	movslq	%eax, %rsi
	movl	$1, %edx
	leaq	.Lubsan_data57(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L31:
	leaq	-80(%rsi), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L34
	movq	$0, -80(%rsi)
	leaq	-72(%rsi), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L18
	cmpb	$3, %al
	jle	.L35
.L18:
	movl	$0, -72(%rsi)
	leaq	-224(%rsi), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %edx
	leaq	-65(%rsi), %rax
	movq	%rax, %rcx
	shrq	$3, %rcx
	movzbl	2147450880(%rcx), %ecx
	andl	$7, %eax
	cmpb	%al, %cl
	setle	%r8b
	testb	%cl, %cl
	setne	%al
	testb	%al, %r8b
	jne	.L22
	testb	%dl, %dl
	setne	%cl
	setle	%al
	testb	%al, %cl
	jne	.L22
	movdqa	-224(%rsi), %xmm1
	movups	%xmm1, (%rbx)
	movdqa	-208(%rsi), %xmm2
	movups	%xmm2, 16(%rbx)
	movdqa	-192(%rsi), %xmm3
	movups	%xmm3, 32(%rbx)
	movdqa	-176(%rsi), %xmm4
	movups	%xmm4, 48(%rbx)
	movdqa	-160(%rsi), %xmm5
	movups	%xmm5, 64(%rbx)
	movdqa	-144(%rsi), %xmm6
	movups	%xmm6, 80(%rbx)
	movdqa	-128(%rsi), %xmm7
	movups	%xmm7, 96(%rbx)
	movdqa	-112(%rsi), %xmm0
	movups	%xmm0, 112(%rbx)
	movdqa	-96(%rsi), %xmm1
	movups	%xmm1, 128(%rbx)
	movq	-80(%rsi), %rax
	movq	-72(%rsi), %rdx
	movq	%rax, 144(%rbx)
	movq	%rdx, 152(%rbx)
	cmpq	%rbp, %r14
	jne	.L36
	movl	$0, 2147450880(%r10)
	movq	$0, 2147450904(%r10)
.L3:
	movq	%rbx, %rax
	addq	$264, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 40
	popq	%rbx
	.cfi_def_cfa_offset 32
	popq	%rbp
	.cfi_def_cfa_offset 24
	popq	%r12
	.cfi_def_cfa_offset 16
	popq	%r14
	.cfi_def_cfa_offset 8
	ret
.L34:
	.cfi_restore_state
	call	__asan_report_store8@PLT
.L35:
	call	__asan_report_store4@PLT
.L22:
	movl	$160, %esi
	call	__asan_report_load_n@PLT
.L36:
	movq	$1172321806, 0(%rbp)
	movdqa	.LC2(%rip), %xmm0
	movups	%xmm0, 2147450880(%r10)
	movups	%xmm0, 2147450896(%r10)
	movq	248(%rbp), %rax
	movb	$0, (%rax)
	jmp	.L3
	.cfi_endproc
.LFE25:
	.size	arena_new, .-arena_new
	.type	arena_recycle, @function
arena_recycle:
.LASANPC26:
.LFB26:
	.cfi_startproc
	movq	%rdi, %rax
	movq	%rsi, %rdi
	testq	%rsi, %rsi
	sete	%cl
	cmpq	$15, %rdx
	setbe	%sil
	orb	%sil, %cl
	jne	.L62
	testq	%rax, %rax
	je	.L62
	subq	$8, %rsp
	.cfi_def_cfa_offset 16
	addq	$7, %rdx
	movq	%rdx, %rsi
	andq	$-8, %rsi
	shrq	$3, %rdx
	testq	%rdi, %rdi
	je	.L39
	testb	$7, %dil
	jne	.L39
	leaq	8(%rdi), %rcx
	cmpq	$-8, %rdi
	jnb	.L65
	leaq	8(%rdi), %rcx
	movq	%rcx, %r8
	shrq	$3, %r8
	cmpb	$0, 2147450880(%r8)
	jne	.L66
	movq	%rsi, 8(%rdi)
	cmpq	$15, %rdx
	jbe	.L67
	testq	%rax, %rax
	je	.L52
	testb	$7, %al
	jne	.L52
	leaq	152(%rax), %rdx
	cmpq	$-152, %rax
	jnb	.L68
	leaq	152(%rax), %rcx
	movq	%rcx, %rdx
	shrq	$3, %rdx
	movzbl	2147450880(%rdx), %edx
	testb	%dl, %dl
	je	.L55
	cmpb	$3, %dl
	jle	.L69
.L55:
	cmpl	$31, 152(%rax)
	jg	.L37
	leaq	144(%rax), %rdx
	movq	%rdx, %rcx
	shrq	$3, %rcx
	cmpb	$0, 2147450880(%rcx)
	jne	.L70
	movq	144(%rax), %rdx
	movq	%rdi, %rcx
	shrq	$3, %rcx
	cmpb	$0, 2147450880(%rcx)
	jne	.L71
	movq	%rdx, (%rdi)
	movq	%rdi, 144(%rax)
	movl	152(%rax), %edx
	addl	$1, %edx
	jo	.L72
	movl	%edx, 152(%rax)
.L37:
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
.L39:
	.cfi_restore_state
	movq	%rdi, %rsi
	leaq	.Lubsan_data58(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L65:
	movq	%rcx, %rdx
	movq	%rdi, %rsi
	leaq	.Lubsan_data59(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L66:
	movq	%rcx, %rdi
	call	__asan_report_store8@PLT
.L67:
	cmpq	$16, %rdx
	jnb	.L73
	testq	%rax, %rax
	je	.L45
	testb	$7, %al
	jne	.L45
	leaq	16(,%rdx,8), %rcx
	movq	%rax, %rsi
	addq	%rcx, %rsi
	jc	.L74
	leaq	16(%rax,%rdx,8), %rsi
	movq	%rsi, %r8
	shrq	$3, %r8
	cmpb	$0, 2147450880(%r8)
	jne	.L75
	movq	16(%rax,%rdx,8), %rsi
	movq	%rdi, %r8
	shrq	$3, %r8
	cmpb	$0, 2147450880(%r8)
	jne	.L76
	movq	%rsi, (%rdi)
	cmpq	$16, %rdx
	jnb	.L77
	addq	%rax, %rcx
	jc	.L78
	movq	%rdi, 16(%rax,%rdx,8)
	jmp	.L37
.L73:
	movq	%rdx, %rsi
	leaq	.Lubsan_data60(%rip), %rdi
	call	__ubsan_handle_out_of_bounds_abort@PLT
.L45:
	movq	%rax, %rsi
	leaq	.Lubsan_data61(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L74:
	movq	%rsi, %rdx
	movq	%rax, %rsi
	leaq	.Lubsan_data62(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L75:
	movq	%rsi, %rdi
	call	__asan_report_load8@PLT
.L76:
	call	__asan_report_store8@PLT
.L77:
	movq	%rdx, %rsi
	leaq	.Lubsan_data63(%rip), %rdi
	call	__ubsan_handle_out_of_bounds_abort@PLT
.L78:
	movq	%rcx, %rdx
	movq	%rax, %rsi
	leaq	.Lubsan_data64(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L52:
	movq	%rax, %rsi
	leaq	.Lubsan_data65(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L68:
	movq	%rax, %rsi
	leaq	.Lubsan_data66(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L69:
	movq	%rcx, %rdi
	call	__asan_report_load4@PLT
.L70:
	movq	%rdx, %rdi
	call	__asan_report_load8@PLT
.L71:
	call	__asan_report_store8@PLT
.L72:
	movslq	152(%rax), %rsi
	movl	$1, %edx
	leaq	.Lubsan_data67(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L62:
	.cfi_def_cfa_offset 8
	ret
	.cfi_endproc
.LFE26:
	.size	arena_recycle, .-arena_recycle
	.type	arena_child, @function
arena_child:
.LASANPC27:
.LFB27:
	.cfi_startproc
	pushq	%rbx
	.cfi_def_cfa_offset 16
	.cfi_offset 3, -16
	testq	%rsi, %rsi
	je	.L80
	movq	%rdi, %rbx
	testb	$7, %sil
	jne	.L80
	leaq	8(%rsi), %rdx
	cmpq	$-8, %rsi
	jnb	.L85
	leaq	8(%rsi), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L86
	movq	8(%rsi), %rsi
	movq	%rbx, %rdi
	call	arena_new
	movq	%rbx, %rax
	popq	%rbx
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
.L80:
	.cfi_restore_state
	leaq	.Lubsan_data68(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L85:
	leaq	.Lubsan_data69(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L86:
	call	__asan_report_load8@PLT
	.cfi_endproc
.LFE27:
	.size	arena_child, .-arena_child
	.type	arena_free, @function
arena_free:
.LASANPC30:
.LFB30:
	.cfi_startproc
	subq	$8, %rsp
	.cfi_def_cfa_offset 16
	movq	%rdi, %rsi
	movl	$0, %eax
	movq	%rdi, %r8
	andl	$7, %r8d
	jmp	.L97
.L114:
	movq	%rdx, %rsi
	leaq	.Lubsan_data70(%rip), %rdi
	call	__ubsan_handle_out_of_bounds_abort@PLT
.L89:
	leaq	.Lubsan_data71(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L91:
	cmpq	%rcx, %rsi
	jb	.L93
.L92:
	movslq	%eax, %rdx
	leaq	16(%rsi,%rdx,8), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L111
	movslq	%eax, %rdx
	movq	$0, 16(%rsi,%rdx,8)
	movl	%eax, %edx
	addl	$1, %edx
	jo	.L112
	movl	%edx, %eax
	cmpl	$15, %edx
	jg	.L113
.L97:
	movslq	%eax, %rdx
	cmpq	$16, %rdx
	jnb	.L114
	testq	%rsi, %rsi
	je	.L89
	testq	%r8, %r8
	jne	.L89
	leal	2(%rax), %edx
	movslq	%edx, %rdx
	salq	$3, %rdx
	leaq	(%rsi,%rdx), %rcx
	js	.L91
	cmpq	%rsi, %rcx
	jnb	.L92
.L93:
	movq	%rcx, %rdx
	leaq	.Lubsan_data72(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L111:
	call	__asan_report_store8@PLT
.L112:
	movslq	%eax, %rsi
	movl	$1, %edx
	leaq	.Lubsan_data76(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L113:
	leaq	144(%rsi), %rdx
	cmpq	$-144, %rsi
	jnb	.L115
	leaq	144(%rsi), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L116
	movq	$0, 144(%rsi)
	leaq	152(%rsi), %rdx
	cmpq	$-152, %rsi
	jnb	.L117
	leaq	152(%rsi), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L101
	cmpb	$3, %al
	jle	.L118
.L101:
	movl	$0, 152(%rsi)
	movq	%rsi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L119
	movq	(%rsi), %rdi
	testq	%rdi, %rdi
	jne	.L106
	jmp	.L103
.L115:
	leaq	.Lubsan_data73(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L116:
	call	__asan_report_store8@PLT
.L117:
	leaq	.Lubsan_data74(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L118:
	call	__asan_report_store4@PLT
.L119:
	movq	%rsi, %rdi
	call	__asan_report_load8@PLT
.L107:
	movq	%rax, %rdi
.L106:
	testb	$7, %dil
	jne	.L120
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L121
	movq	(%rdi), %rax
	movq	%fs:g_pool@tpoff, %rdx
	movq	%rdx, (%rdi)
	movq	%rdi, %fs:g_pool@tpoff
	testq	%rax, %rax
	jne	.L107
.L103:
	movq	$0, (%rsi)
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
.L120:
	.cfi_restore_state
	movq	%rdi, %rsi
	leaq	.Lubsan_data75(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L121:
	call	__asan_report_load8@PLT
	.cfi_endproc
.LFE30:
	.size	arena_free, .-arena_free
	.type	amem, @function
amem:
.LASANPC49:
.LFB49:
	.cfi_startproc
	pushq	%r13
	.cfi_def_cfa_offset 16
	.cfi_offset 13, -16
	pushq	%r12
	.cfi_def_cfa_offset 24
	.cfi_offset 12, -24
	pushq	%rbp
	.cfi_def_cfa_offset 32
	.cfi_offset 6, -32
	pushq	%rbx
	.cfi_def_cfa_offset 40
	.cfi_offset 3, -40
	subq	$8, %rsp
	.cfi_def_cfa_offset 48
	testq	%rdi, %rdi
	je	.L123
	movq	%rdi, %rbp
	leaq	7(%rsi), %rdi
	movq	%rdi, %r12
	andq	$-8, %r12
	movq	%rdi, %rsi
	shrq	$3, %rsi
	cmpq	$127, %rdi
	ja	.L124
	cmpq	$16, %rsi
	jnb	.L219
	testq	%rbp, %rbp
	je	.L126
	testb	$7, %bpl
	jne	.L126
	leaq	16(,%rsi,8), %rdx
	movq	%rbp, %rax
	addq	%rdx, %rax
	jc	.L220
	leaq	16(%rbp,%rsi,8), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L221
	movq	16(%rbp,%rsi,8), %rax
	testq	%rax, %rax
	je	.L130
	cmpq	$16, %rsi
	jnb	.L222
	movq	%rbp, %rcx
	addq	%rdx, %rcx
	jc	.L223
	cmpq	$16, %rsi
	jnb	.L224
	movq	%rax, %rcx
	testq	%rax, %rax
	je	.L134
	testb	$7, %al
	jne	.L134
	shrq	$3, %rcx
	cmpb	$0, 2147450880(%rcx)
	jne	.L225
	movq	(%rax), %rcx
	addq	%rbp, %rdx
	jc	.L226
	movq	%rcx, 16(%rbp,%rsi,8)
	jmp	.L122
.L219:
	leaq	.Lubsan_data77(%rip), %rdi
	call	__ubsan_handle_out_of_bounds_abort@PLT
.L126:
	movq	%rbp, %rsi
	leaq	.Lubsan_data78(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L220:
	movq	%rax, %rdx
	movq	%rbp, %rsi
	leaq	.Lubsan_data79(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L221:
	call	__asan_report_load8@PLT
.L222:
	leaq	.Lubsan_data80(%rip), %rdi
	call	__ubsan_handle_out_of_bounds_abort@PLT
.L223:
	movq	%rcx, %rdx
	movq	%rbp, %rsi
	leaq	.Lubsan_data81(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L224:
	leaq	.Lubsan_data82(%rip), %rdi
	call	__ubsan_handle_out_of_bounds_abort@PLT
.L134:
	movq	%rcx, %rsi
	leaq	.Lubsan_data83(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L225:
	movq	%rax, %rdi
	call	__asan_report_load8@PLT
.L226:
	movq	%rbp, %rsi
	leaq	.Lubsan_data84(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L124:
	testq	%rbp, %rbp
	je	.L139
	testb	$7, %bpl
	jne	.L139
	leaq	144(%rbp), %rdx
	cmpq	$-144, %rbp
	jnb	.L227
	leaq	144(%rbp), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L228
	cmpq	$0, 144(%rbp)
	je	.L130
	testb	$7, %dil
	jne	.L229
	leaq	144(%rbp), %rax
	movq	%rax, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L230
	movq	144(%rbp), %rsi
	testq	%rsi, %rsi
	je	.L130
	leaq	(%r12,%r12), %r9
	movl	$0, %ecx
	movq	$-1, %r8
	jmp	.L152
.L139:
	movq	%rbp, %rsi
	leaq	.Lubsan_data85(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L227:
	movq	%rbp, %rsi
	leaq	.Lubsan_data86(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L228:
	call	__asan_report_load8@PLT
.L229:
	movq	%rdi, %rsi
	leaq	.Lubsan_data87(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L230:
	movq	%rax, %rdi
	call	__asan_report_load8@PLT
.L234:
	movq	%rdi, %rsi
	leaq	.Lubsan_data88(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L235:
	leaq	.Lubsan_data89(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L236:
	leaq	.Lubsan_data90(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L237:
	movq	%rax, %rdi
	call	__asan_report_load8@PLT
.L149:
	movq	%rsi, %rdi
	testb	$7, %sil
	jne	.L231
	movq	%rsi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L232
	movq	(%rsi), %rsi
	testq	%rsi, %rsi
	je	.L233
.L152:
	testb	$7, %dil
	jne	.L234
	testb	$7, %sil
	jne	.L235
	leaq	8(%rsi), %rdx
	cmpq	$-8, %rsi
	jnb	.L236
	leaq	8(%rsi), %rax
	movq	%rax, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L237
	movq	8(%rsi), %rax
	cmpq	%r12, %rax
	jb	.L149
	cmpq	%rax, %r9
	jb	.L149
	cmpq	%r8, %rax
	cmovb	%rax, %r8
	cmovb	%rdi, %rcx
	jmp	.L149
.L231:
	leaq	.Lubsan_data91(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L232:
	call	__asan_report_load8@PLT
.L233:
	testq	%rcx, %rcx
	je	.L130
	movq	%rcx, %rsi
	je	.L153
	testb	$7, %cl
	jne	.L153
	movq	%rcx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L238
	movq	(%rcx), %rax
	movq	%rax, %rsi
	testq	%rax, %rax
	je	.L156
	testb	$7, %al
	jne	.L156
	movq	%rax, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L239
	movq	(%rax), %rdx
	movq	%rdx, (%rcx)
	leaq	152(%rbp), %rdx
	cmpq	$-152, %rbp
	jnb	.L240
	leaq	152(%rbp), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	movzbl	2147450880(%rdx), %edx
	testb	%dl, %dl
	je	.L160
	cmpb	$3, %dl
	jle	.L241
.L160:
	movl	152(%rbp), %edx
	subl	$1, %edx
	jo	.L242
	movl	%edx, 152(%rbp)
	jmp	.L122
.L153:
	leaq	.Lubsan_data92(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L238:
	movq	%rcx, %rdi
	call	__asan_report_load8@PLT
.L156:
	leaq	.Lubsan_data93(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L239:
	movq	%rax, %rdi
	call	__asan_report_load8@PLT
.L240:
	movq	%rbp, %rsi
	leaq	.Lubsan_data94(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L241:
	call	__asan_report_load4@PLT
.L242:
	movslq	152(%rbp), %rsi
	movl	$1, %edx
	leaq	.Lubsan_data114(%rip), %rdi
	call	__ubsan_handle_sub_overflow_abort@PLT
.L130:
	testq	%rbp, %rbp
	je	.L163
	testb	$7, %bpl
	jne	.L163
	movq	%rbp, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L243
	movq	0(%rbp), %rsi
	testq	%rsi, %rsi
	je	.L166
	je	.L167
	testb	$7, %sil
	jne	.L167
	leaq	8(%rsi), %rdx
	cmpq	$-8, %rsi
	jnb	.L244
	leaq	8(%rsi), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L245
	movq	%r12, %rax
	addq	8(%rsi), %rax
	leaq	16(%rsi), %rdx
	cmpq	$-16, %rsi
	jnb	.L246
	leaq	16(%rsi), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L247
	cmpq	%rax, 16(%rsi)
	jnb	.L173
.L166:
	leaq	8(%rbp), %rdx
	cmpq	$-8, %rbp
	jnb	.L248
	leaq	8(%rbp), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L249
	movq	8(%rbp), %r13
	cmpq	%r13, %r12
	cmovnb	%r12, %r13
	movq	%fs:g_pool@tpoff, %rbx
	testq	%rbx, %rbx
	je	.L176
	movq	%rbx, %rsi
	je	.L177
	testb	$7, %bl
	jne	.L177
	leaq	16(%rbx), %rdx
	cmpq	$-16, %rbx
	jnb	.L250
	leaq	16(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L251
	movq	%fs:0, %rax
	leaq	g_pool@tpoff(%rax), %rsi
	cmpq	%r13, 16(%rbx)
	jnb	.L252
.L181:
	testb	$7, %sil
	jne	.L253
	testb	$7, %bl
	jne	.L254
	leaq	16(%rbx), %rdx
	cmpq	$-16, %rbx
	jnb	.L255
	leaq	16(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L256
	cmpq	%r13, 16(%rbx)
	jnb	.L257
	movq	%rbx, %rsi
	testb	$7, %bl
	jne	.L258
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L259
	movq	(%rbx), %rbx
	testq	%rbx, %rbx
	jne	.L181
.L176:
	movl	$32, %edi
	call	malloc@PLT
	movq	%rax, %rbx
	movq	%r13, %rdi
	call	malloc@PLT
	testq	%rbx, %rbx
	je	.L260
	leaq	24(%rbx), %rdx
	cmpq	$-24, %rbx
	jnb	.L261
	leaq	24(%rbx), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L262
	movq	%rax, 24(%rbx)
	leaq	16(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L263
	movq	%r13, 16(%rbx)
	leaq	8(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L264
	movq	$0, 8(%rbx)
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L265
	movq	$0, (%rbx)
.L184:
	movq	0(%rbp), %rax
	movq	%rbx, %rsi
	testq	%rbx, %rbx
	je	.L201
	testb	$7, %bl
	jne	.L201
	movq	%rbx, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L266
	movq	%rax, (%rbx)
	movq	%rbx, 0(%rbp)
.L173:
	movq	0(%rbp), %rsi
	testq	%rsi, %rsi
	je	.L204
	testb	$7, %sil
	jne	.L204
	leaq	24(%rsi), %rdx
	cmpq	$-24, %rsi
	jnb	.L267
	leaq	24(%rsi), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L268
	movq	24(%rsi), %rax
	leaq	8(%rsi), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L269
	movq	8(%rsi), %rdx
	leaq	(%rax,%rdx), %rcx
	testq	%rdx, %rdx
	js	.L209
	cmpq	%rax, %rcx
	jnb	.L210
.L211:
	movq	%rcx, %rdx
	movq	%rax, %rsi
	leaq	.Lubsan_data112(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L163:
	movq	%rbp, %rsi
	leaq	.Lubsan_data95(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L243:
	movq	%rbp, %rdi
	call	__asan_report_load8@PLT
.L167:
	leaq	.Lubsan_data96(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L244:
	leaq	.Lubsan_data97(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L245:
	call	__asan_report_load8@PLT
.L246:
	leaq	.Lubsan_data98(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L247:
	call	__asan_report_load8@PLT
.L248:
	movq	%rbp, %rsi
	leaq	.Lubsan_data99(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L249:
	call	__asan_report_load8@PLT
.L177:
	leaq	.Lubsan_data100(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L250:
	leaq	.Lubsan_data101(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L251:
	call	__asan_report_load8@PLT
.L252:
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L270
	movq	(%rbx), %rax
	movq	%rax, %fs:g_pool@tpoff
	leaq	8(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L271
	movq	$0, 8(%rbx)
	movq	$0, (%rbx)
	jmp	.L184
.L270:
	movq	%rbx, %rdi
	call	__asan_report_load8@PLT
.L271:
	call	__asan_report_store8@PLT
.L253:
	leaq	.Lubsan_data103(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L254:
	movq	%rbx, %rsi
	leaq	.Lubsan_data104(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L255:
	movq	%rbx, %rsi
	leaq	.Lubsan_data105(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L256:
	call	__asan_report_load8@PLT
.L257:
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L272
	movq	(%rbx), %rax
	movq	%rsi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L273
	movq	%rax, (%rsi)
	leaq	8(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L274
	movq	$0, 8(%rbx)
	movq	$0, (%rbx)
	jmp	.L184
.L272:
	movq	%rbx, %rdi
	call	__asan_report_load8@PLT
.L273:
	movq	%rsi, %rdi
	call	__asan_report_store8@PLT
.L274:
	call	__asan_report_store8@PLT
.L258:
	leaq	.Lubsan_data106(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L259:
	movq	%rbx, %rdi
	call	__asan_report_load8@PLT
.L260:
	movl	$0, %esi
	leaq	.Lubsan_data107(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L261:
	movq	%rbx, %rsi
	leaq	.Lubsan_data108(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L262:
	call	__asan_report_store8@PLT
.L263:
	call	__asan_report_store8@PLT
.L264:
	call	__asan_report_store8@PLT
.L265:
	movq	%rbx, %rdi
	call	__asan_report_store8@PLT
.L201:
	leaq	.Lubsan_data109(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L266:
	movq	%rbx, %rdi
	call	__asan_report_store8@PLT
.L204:
	leaq	.Lubsan_data110(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L267:
	leaq	.Lubsan_data111(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L268:
	call	__asan_report_load8@PLT
.L269:
	call	__asan_report_load8@PLT
.L209:
	cmpq	%rcx, %rax
	jb	.L211
.L210:
	addq	%rdx, %rax
	addq	%rdx, %r12
	movq	%r12, 8(%rsi)
.L122:
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 40
	popq	%rbx
	.cfi_def_cfa_offset 32
	popq	%rbp
	.cfi_def_cfa_offset 24
	popq	%r12
	.cfi_def_cfa_offset 16
	popq	%r13
	.cfi_def_cfa_offset 8
	ret
.L123:
	.cfi_restore_state
	movq	%rsi, %rdi
	call	malloc@PLT
	jmp	.L122
	.cfi_endproc
.LFE49:
	.size	amem, .-amem
	.type	hs, @function
hs:
.LASANPC50:
.LFB50:
	.cfi_startproc
	pushq	%rbx
	.cfi_def_cfa_offset 16
	.cfi_offset 3, -16
	movq	%rsi, %rbx
	addq	$8, %rsi
	jo	.L292
	movq	%rsi, %rax
	addq	$1, %rax
	jo	.L293
	movq	%rax, %rsi
	call	amem
	testq	%rax, %rax
	je	.L280
	testb	$7, %al
	jne	.L280
	movq	%rax, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L294
	movq	%rbx, (%rax)
	leaq	8(%rax), %rdx
	cmpq	$-8, %rax
	jnb	.L295
	addq	$8, %rax
	leaq	(%rax,%rbx), %rdx
	testq	%rbx, %rbx
	js	.L284
	cmpq	%rax, %rdx
	jnb	.L285
.L286:
	movq	%rax, %rsi
	leaq	.Lubsan_data117(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L292:
	movl	$8, %edx
	movq	%rbx, %rsi
	leaq	.Lubsan_data119(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L293:
	movl	$1, %edx
	leaq	.Lubsan_data120(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L280:
	movq	%rax, %rsi
	leaq	.Lubsan_data115(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L294:
	movq	%rax, %rdi
	call	__asan_report_store8@PLT
.L295:
	movq	%rax, %rsi
	leaq	.Lubsan_data116(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L284:
	cmpq	%rdx, %rax
	jb	.L286
.L285:
	addq	%rax, %rbx
	je	.L296
	movq	%rbx, %rdx
	shrq	$3, %rdx
	movzbl	2147450880(%rdx), %edx
	movq	%rbx, %rcx
	andl	$7, %ecx
	cmpb	%cl, %dl
	jg	.L288
	testb	%dl, %dl
	jne	.L297
.L288:
	movb	$0, (%rbx)
	popq	%rbx
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
.L296:
	.cfi_restore_state
	movl	$0, %esi
	leaq	.Lubsan_data118(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L297:
	movq	%rbx, %rdi
	call	__asan_report_store1@PLT
	.cfi_endproc
.LFE50:
	.size	hs, .-hs
	.type	ms_int_cap, @function
ms_int_cap:
.LASANPC151:
.LFB151:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r12
	.cfi_def_cfa_offset 32
	.cfi_offset 12, -32
	pushq	%rbp
	.cfi_def_cfa_offset 40
	.cfi_offset 6, -40
	pushq	%rbx
	.cfi_def_cfa_offset 48
	.cfi_offset 3, -48
	movq	%rdi, %r12
	movq	%rsi, %rdi
	cmpq	$8, %rdx
	jle	.L299
	movl	$8, %ebx
	.p2align 4
.L302:
	imulq	$2, %rbx, %rax
	jo	.L325
	movq	%rax, %rbx
	cmpq	%rax, %rdx
	jg	.L302
.L303:
	leaq	0(,%rbx,8), %r14
	movq	%r14, %rsi
	movq	%rdi, %r15
	call	amem
	movq	%rax, %rbp
	movq	%r14, %rsi
	movq	%r15, %rdi
	call	amem
	movl	$0, %ecx
	jmp	.L313
.L325:
	movl	$2, %edx
	movq	%rbx, %rsi
	leaq	.Lubsan_data123(%rip), %rdi
	call	__ubsan_handle_mul_overflow_abort@PLT
.L299:
	movl	$0, %ebp
	movl	$0, %eax
	movl	$8, %ebx
	testq	%rdx, %rdx
	jne	.L303
.L304:
	movq	%r12, %rcx
	shrq	$3, %rcx
	cmpb	$0, 2147450880(%rcx)
	jne	.L326
	movq	%rbp, (%r12)
	leaq	8(%r12), %rdi
	movq	%rdi, %rcx
	shrq	$3, %rcx
	cmpb	$0, 2147450880(%rcx)
	jne	.L327
	movq	%rax, 8(%r12)
	leaq	16(%r12), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L328
	movq	$0, 16(%r12)
	leaq	24(%r12), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L329
	movq	%rdx, 24(%r12)
	movq	%r12, %rax
	popq	%rbx
	.cfi_remember_state
	.cfi_def_cfa_offset 40
	popq	%rbp
	.cfi_def_cfa_offset 32
	popq	%r12
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L305:
	.cfi_restore_state
	cmpq	%rdx, %rbp
	jb	.L307
.L306:
	addq	%rbp, %rsi
	je	.L308
	testb	$7, %sil
	jne	.L308
	movq	%rsi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L330
	movq	$0, (%rsi)
	movq	%rcx, %rdx
	addq	$1, %rdx
	jo	.L331
	movq	%rdx, %rcx
	cmpq	%rbx, %rdx
	jge	.L332
.L313:
	leaq	0(,%rcx,8), %rsi
	leaq	0(%rbp,%rsi), %rdx
	testq	%rsi, %rsi
	js	.L305
	cmpq	%rbp, %rdx
	jnb	.L306
.L307:
	movq	%rbp, %rsi
	leaq	.Lubsan_data121(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L308:
	leaq	.Lubsan_data122(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L330:
	movq	%rsi, %rdi
	call	__asan_report_store8@PLT
.L331:
	movl	$1, %edx
	movq	%rcx, %rsi
	leaq	.Lubsan_data124(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L332:
	movq	%rbx, %rdx
	jmp	.L304
.L326:
	movq	%r12, %rdi
	call	__asan_report_store8@PLT
.L327:
	call	__asan_report_store8@PLT
.L328:
	call	__asan_report_store8@PLT
.L329:
	call	__asan_report_store8@PLT
	.cfi_endproc
.LFE151:
	.size	ms_int_cap, .-ms_int_cap
	.type	hier_task_new, @function
hier_task_new:
.LASANPC32:
.LFB32:
	.cfi_startproc
	pushq	%rbx
	.cfi_def_cfa_offset 16
	.cfi_offset 3, -16
	movl	$184, %edi
	call	malloc@PLT
	testq	%rax, %rax
	je	.L341
	movq	%rax, %rbx
	leaq	8(%rax), %rdx
	cmpq	$-8, %rax
	jnb	.L342
	leaq	8(%rax), %rdi
	movl	$0, %esi
	call	arena_new
	leaq	168(%rbx), %rdx
	cmpq	$-168, %rbx
	jnb	.L343
	leaq	168(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L344
	movq	$0, 168(%rbx)
	leaq	176(%rbx), %rdx
	cmpq	$-176, %rbx
	jnb	.L345
	leaq	176(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L339
	cmpb	$3, %al
	jle	.L346
.L339:
	movl	$0, 176(%rbx)
	movq	%rbx, %rax
	popq	%rbx
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
.L341:
	.cfi_restore_state
	movl	$0, %esi
	leaq	.Lubsan_data125(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L342:
	movq	%rax, %rsi
	leaq	.Lubsan_data126(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L343:
	movq	%rbx, %rsi
	leaq	.Lubsan_data127(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L344:
	call	__asan_report_store8@PLT
.L345:
	movq	%rbx, %rsi
	leaq	.Lubsan_data128(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L346:
	call	__asan_report_store4@PLT
	.cfi_endproc
.LFE32:
	.size	hier_task_new, .-hier_task_new
	.type	hier_pool_flush, @function
hier_pool_flush:
.LASANPC31:
.LFB31:
	.cfi_startproc
	pushq	%rbx
	.cfi_def_cfa_offset 16
	.cfi_offset 3, -16
	movq	%fs:g_pool@tpoff, %rbx
	testq	%rbx, %rbx
	je	.L347
.L353:
	testb	$7, %bl
	jne	.L356
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L357
	movq	(%rbx), %rax
	movq	%rax, %fs:g_pool@tpoff
	leaq	24(%rbx), %rdx
	cmpq	$-24, %rbx
	jnb	.L358
	leaq	24(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L359
	movq	24(%rbx), %rdi
	call	free@PLT
	movq	%rbx, %rdi
	call	free@PLT
	movq	%fs:g_pool@tpoff, %rbx
	testq	%rbx, %rbx
	jne	.L353
.L347:
	popq	%rbx
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
.L356:
	.cfi_restore_state
	movq	%rbx, %rsi
	leaq	.Lubsan_data129(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L357:
	movq	%rbx, %rdi
	call	__asan_report_load8@PLT
.L358:
	movq	%rbx, %rsi
	leaq	.Lubsan_data130(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L359:
	call	__asan_report_load8@PLT
	.cfi_endproc
.LFE31:
	.size	hier_pool_flush, .-hier_pool_flush
	.type	hier_task_finish, @function
hier_task_finish:
.LASANPC35:
.LFB35:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	pushq	%rbx
	.cfi_def_cfa_offset 24
	.cfi_offset 3, -24
	subq	$8, %rsp
	.cfi_def_cfa_offset 32
	movq	%rdi, %rbx
	testq	%rdi, %rdi
	je	.L361
	testb	$7, %dil
	jne	.L361
	leaq	176(%rdi), %rdx
	cmpq	$-176, %rdi
	jnb	.L369
	leaq	176(%rdi), %rbp
	movq	%rbp, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L364
	cmpb	$3, %al
	jle	.L370
.L364:
	cmpl	$0, 176(%rbx)
	je	.L371
.L365:
	leaq	8(%rbx), %rdi
	call	arena_free
	movq	%rbx, %rdi
	call	free@PLT
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 24
	popq	%rbx
	.cfi_def_cfa_offset 16
	popq	%rbp
	.cfi_def_cfa_offset 8
	ret
.L361:
	.cfi_restore_state
	movq	%rbx, %rsi
	leaq	.Lubsan_data131(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L369:
	movq	%rdi, %rsi
	leaq	.Lubsan_data132(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L370:
	movq	%rbp, %rdi
	call	__asan_report_load4@PLT
.L371:
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L372
	movl	$0, %esi
	movq	(%rbx), %rdi
	call	pthread_join@PLT
	movq	%rbp, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L367
	cmpb	$3, %al
	jle	.L373
.L367:
	movl	$1, 176(%rbx)
	jmp	.L365
.L372:
	movq	%rbx, %rdi
	call	__asan_report_load8@PLT
.L373:
	movq	%rbp, %rdi
	call	__asan_report_store4@PLT
	.cfi_endproc
.LFE35:
	.size	hier_task_finish, .-hier_task_finish
	.type	Arr_int_push, @function
Arr_int_push:
.LASANPC78:
.LFB78:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$24, %rsp
	.cfi_def_cfa_offset 80
	movq	%rsi, %rbx
	testq	%rsi, %rsi
	je	.L375
	movq	%rdi, %r14
	movq	%rdx, %rbp
	testb	$7, %sil
	jne	.L375
	leaq	8(%rsi), %rdx
	cmpq	$-8, %rsi
	jnb	.L409
	leaq	8(%rsi), %r12
	movq	%r12, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L410
	movq	8(%rsi), %rax
	leaq	16(%rsi), %rdx
	cmpq	$-16, %rsi
	jnb	.L411
	leaq	16(%rsi), %r13
	movq	%r13, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L412
	movq	16(%rsi), %rsi
	cmpq	%rsi, %rax
	je	.L413
.L381:
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L414
	movq	(%rbx), %rsi
	movq	%r12, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L415
	movq	8(%rbx), %rax
	movq	%rax, %rdx
	addq	$1, %rdx
	jo	.L416
	movq	%rdx, 8(%rbx)
	salq	$3, %rax
	leaq	(%rsi,%rax), %rdx
	js	.L399
	cmpq	%rsi, %rdx
	jnb	.L400
.L401:
	leaq	.Lubsan_data136(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L375:
	movq	%rbx, %rsi
	leaq	.Lubsan_data133(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L409:
	leaq	.Lubsan_data134(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L410:
	movq	%r12, %rdi
	call	__asan_report_load8@PLT
.L411:
	leaq	.Lubsan_data135(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L412:
	movq	%r13, %rdi
	call	__asan_report_load8@PLT
.L413:
	testq	%rsi, %rsi
	je	.L405
	imulq	$2, %rsi, %rax
	jo	.L417
	movq	%rax, 8(%rsp)
	jmp	.L382
.L417:
	movl	$2, %edx
	leaq	.Lubsan_data138(%rip), %rdi
	call	__ubsan_handle_mul_overflow_abort@PLT
.L405:
	movq	$4, 8(%rsp)
.L382:
	movq	8(%rsp), %rax
	leaq	0(,%rax,8), %rsi
	movq	%r14, %rdi
	call	amem
	movq	%rax, %r15
	movq	%r12, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L418
	movq	8(%rbx), %rdx
	testq	%rdx, %rdx
	je	.L386
	salq	$3, %rdx
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L419
	movq	(%rbx), %rsi
	testq	%r15, %r15
	je	.L420
	testq	%rsi, %rsi
	je	.L421
	movq	%r15, %rdi
	call	memcpy@PLT
.L386:
	testq	%r14, %r14
	je	.L390
	movq	%r13, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L422
	movq	16(%rbx), %rdx
	testq	%rdx, %rdx
	jne	.L423
.L390:
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L424
	movq	%r15, (%rbx)
	movq	%r13, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L425
	movq	8(%rsp), %rax
	movq	%rax, 16(%rbx)
	jmp	.L381
.L418:
	movq	%r12, %rdi
	call	__asan_report_load8@PLT
.L419:
	movq	%rbx, %rdi
	call	__asan_report_load8@PLT
.L420:
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data3(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L421:
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data4(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L422:
	movq	%r13, %rdi
	call	__asan_report_load8@PLT
.L423:
	salq	$3, %rdx
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L426
	movq	(%rbx), %rsi
	movq	%r14, %rdi
	call	arena_recycle
	jmp	.L390
.L426:
	movq	%rbx, %rdi
	call	__asan_report_load8@PLT
.L424:
	movq	%rbx, %rdi
	call	__asan_report_store8@PLT
.L425:
	movq	%r13, %rdi
	call	__asan_report_store8@PLT
.L414:
	movq	%rbx, %rdi
	call	__asan_report_load8@PLT
.L415:
	movq	%r12, %rdi
	call	__asan_report_load8@PLT
.L416:
	movl	$1, %edx
	movq	%rax, %rsi
	leaq	.Lubsan_data139(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L399:
	cmpq	%rdx, %rsi
	jb	.L401
.L400:
	addq	%rax, %rsi
	je	.L402
	testb	$7, %sil
	jne	.L402
	movq	%rsi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L427
	movq	%rbp, (%rsi)
	addq	$24, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L402:
	.cfi_restore_state
	leaq	.Lubsan_data137(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L427:
	movq	%rsi, %rdi
	call	__asan_report_store8@PLT
	.cfi_endproc
.LFE78:
	.size	Arr_int_push, .-Arr_int_push
	.type	Arr_int_grow, @function
Arr_int_grow:
.LASANPC79:
.LFB79:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$8, %rsp
	.cfi_def_cfa_offset 64
	movq	%rdx, %rbx
	testq	%rdx, %rdx
	je	.L429
	movq	%rdi, %r13
	movq	%rsi, %rbp
	movq	%rcx, %r12
	testb	$7, %dl
	jne	.L429
	movq	%rdx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L453
	movq	(%rdx), %rsi
	testq	%rsi, %rsi
	je	.L450
	imulq	$2, %rsi, %r15
	jno	.L432
	movl	$2, %edx
	leaq	.Lubsan_data144(%rip), %rdi
	call	__ubsan_handle_mul_overflow_abort@PLT
.L429:
	movq	%rbx, %rsi
	leaq	.Lubsan_data140(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L453:
	movq	%rdx, %rdi
	call	__asan_report_load8@PLT
.L450:
	movl	$4, %r15d
.L432:
	leaq	0(,%r15,8), %rsi
	movq	%r13, %rdi
	call	amem
	movq	%rax, %r14
	testq	%r12, %r12
	je	.L435
	leaq	0(,%r12,8), %rdx
	testq	%rbp, %rbp
	je	.L436
	testb	$7, %bpl
	jne	.L436
	movq	%rbp, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L454
	movq	0(%rbp), %rsi
	testq	%r14, %r14
	je	.L455
	testq	%rsi, %rsi
	je	.L456
	movq	%r14, %rdi
	call	memcpy@PLT
.L435:
	testq	%r13, %r13
	je	.L441
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L457
	movq	(%rbx), %rdx
	testq	%rdx, %rdx
	jne	.L458
.L441:
	testq	%rbp, %rbp
	je	.L446
	testb	$7, %bpl
	jne	.L446
	movq	%rbp, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L459
	movq	%r14, 0(%rbp)
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L460
	movq	%r15, (%rbx)
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L436:
	.cfi_restore_state
	movq	%rbp, %rsi
	leaq	.Lubsan_data141(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L454:
	movq	%rbp, %rdi
	call	__asan_report_load8@PLT
.L455:
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data5(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L456:
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data6(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L457:
	movq	%rbx, %rdi
	call	__asan_report_load8@PLT
.L458:
	salq	$3, %rdx
	testq	%rbp, %rbp
	je	.L443
	testb	$7, %bpl
	jne	.L443
	movq	%rbp, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L461
	movq	0(%rbp), %rsi
	movq	%r13, %rdi
	call	arena_recycle
	jmp	.L441
.L443:
	movq	%rbp, %rsi
	leaq	.Lubsan_data142(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L461:
	movq	%rbp, %rdi
	call	__asan_report_load8@PLT
.L446:
	movq	%rbp, %rsi
	leaq	.Lubsan_data143(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L459:
	movq	%rbp, %rdi
	call	__asan_report_store8@PLT
.L460:
	movq	%rbx, %rdi
	call	__asan_report_store8@PLT
	.cfi_endproc
.LFE79:
	.size	Arr_int_grow, .-Arr_int_grow
	.type	Arr_int_from, @function
Arr_int_from:
.LASANPC76:
.LFB76:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r12
	.cfi_def_cfa_offset 32
	.cfi_offset 12, -32
	pushq	%rbp
	.cfi_def_cfa_offset 40
	.cfi_offset 6, -40
	pushq	%rbx
	.cfi_def_cfa_offset 48
	.cfi_offset 3, -48
	movq	%rdi, %rbx
	movq	%rcx, %rbp
	movl	$0, %r12d
	testq	%rcx, %rcx
	jne	.L471
.L463:
	testq	%rbp, %rbp
	movl	$0, %eax
	cmovns	%rbp, %rax
	movq	%rbx, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L472
	movq	%r12, (%rbx)
	leaq	8(%rbx), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L473
	movq	%rbp, 8(%rbx)
	leaq	16(%rbx), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L474
	movq	%rax, 16(%rbx)
	movq	%rbx, %rax
	popq	%rbx
	.cfi_remember_state
	.cfi_def_cfa_offset 40
	popq	%rbp
	.cfi_def_cfa_offset 32
	popq	%r12
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L471:
	.cfi_restore_state
	movq	%rsi, %rdi
	movq	%rdx, %r14
	leaq	0(,%rcx,8), %rax
	movq	%rax, %r15
	movq	%rax, %rsi
	call	amem
	movq	%rax, %r12
	testq	%rax, %rax
	je	.L475
	testq	%r14, %r14
	je	.L476
	movq	%r15, %rdx
	movq	%r14, %rsi
	movq	%r12, %rdi
	call	memcpy@PLT
	jmp	.L463
.L475:
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data7(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L476:
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data8(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L472:
	movq	%rbx, %rdi
	call	__asan_report_store8@PLT
.L473:
	call	__asan_report_store8@PLT
.L474:
	call	__asan_report_store8@PLT
	.cfi_endproc
.LFE76:
	.size	Arr_int_from, .-Arr_int_from
	.type	hbox, @function
hbox:
.LASANPC70:
.LFB70:
	.cfi_startproc
	pushq	%r12
	.cfi_def_cfa_offset 16
	.cfi_offset 12, -16
	pushq	%rbp
	.cfi_def_cfa_offset 24
	.cfi_offset 6, -24
	pushq	%rbx
	.cfi_def_cfa_offset 32
	.cfi_offset 3, -32
	movq	%rsi, %r12
	movq	%rdx, %rbp
	call	amem
	testq	%rax, %rax
	je	.L481
	testq	%rbp, %rbp
	je	.L482
	movq	%r12, %rdx
	movq	%rbp, %rsi
	movq	%rax, %rdi
	call	memcpy@PLT
	popq	%rbx
	.cfi_remember_state
	.cfi_def_cfa_offset 24
	popq	%rbp
	.cfi_def_cfa_offset 16
	popq	%r12
	.cfi_def_cfa_offset 8
	ret
.L481:
	.cfi_restore_state
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data9(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L482:
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data10(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
	.cfi_endproc
.LFE70:
	.size	hbox, .-hbox
	.type	Env_0_copy, @function
Env_0_copy:
.LASANPC200:
.LFB200:
	.cfi_startproc
	subq	$8, %rsp
	.cfi_def_cfa_offset 16
	movq	%rsi, %rdx
	movl	$8, %esi
	call	hbox
	addq	$8, %rsp
	.cfi_def_cfa_offset 8
	ret
	.cfi_endproc
.LFE200:
	.size	Env_0_copy, .-Env_0_copy
	.type	Env_1_copy, @function
Env_1_copy:
.LASANPC201:
.LFB201:
	.cfi_startproc
	pushq	%r12
	.cfi_def_cfa_offset 16
	.cfi_offset 12, -16
	pushq	%rbp
	.cfi_def_cfa_offset 24
	.cfi_offset 6, -24
	pushq	%rbx
	.cfi_def_cfa_offset 32
	.cfi_offset 3, -32
	subq	$32, %rsp
	.cfi_def_cfa_offset 64
	movq	%rdi, %r12
	movq	%rsi, %rbp
	movq	%rsi, %rdx
	movl	$24, %esi
	call	hbox
	movq	%rax, %rbx
	testq	%rax, %rax
	je	.L486
	testb	$7, %al
	jne	.L486
	testq	%rbp, %rbp
	je	.L488
	testb	$7, %bpl
	jne	.L488
	movq	%rbp, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L496
	movq	0(%rbp), %rdx
	leaq	8(%rbp), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L497
	movq	%rsp, %rdi
	movq	8(%rbp), %rcx
	movq	%r12, %rsi
	call	Arr_int_from
	movq	%rbx, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %ecx
	leaq	23(%rbx), %rax
	movq	%rax, %rdx
	shrq	$3, %rdx
	movzbl	2147450880(%rdx), %edx
	andl	$7, %eax
	cmpb	%al, %dl
	setle	%sil
	testb	%dl, %dl
	setne	%al
	testb	%al, %sil
	jne	.L494
	testb	%cl, %cl
	setne	%dl
	setle	%al
	testb	%al, %dl
	jne	.L494
	movdqa	(%rsp), %xmm0
	movups	%xmm0, (%rbx)
	movq	16(%rsp), %rax
	movq	%rax, 16(%rbx)
	movq	%rbx, %rax
	addq	$32, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 32
	popq	%rbx
	.cfi_def_cfa_offset 24
	popq	%rbp
	.cfi_def_cfa_offset 16
	popq	%r12
	.cfi_def_cfa_offset 8
	ret
.L486:
	.cfi_restore_state
	movq	%rbx, %rsi
	leaq	.Lubsan_data145(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L488:
	movq	%rbp, %rsi
	leaq	.Lubsan_data146(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L496:
	movq	%rbp, %rdi
	call	__asan_report_load8@PLT
.L497:
	call	__asan_report_load8@PLT
.L494:
	movl	$24, %esi
	movq	%rbx, %rdi
	call	__asan_report_store_n@PLT
	.cfi_endproc
.LFE201:
	.size	Env_1_copy, .-Env_1_copy
	.type	hi_intern, @function
hi_intern:
.LASANPC51:
.LFB51:
	.cfi_startproc
	pushq	%r12
	.cfi_def_cfa_offset 16
	.cfi_offset 12, -16
	pushq	%rbp
	.cfi_def_cfa_offset 24
	.cfi_offset 6, -24
	pushq	%rbx
	.cfi_def_cfa_offset 32
	.cfi_offset 3, -32
	testq	%rdi, %rdi
	je	.L511
	movq	%rdi, %r12
	call	strlen@PLT
	movq	%rax, %rbp
	movq	%rax, %rsi
	addq	$8, %rsi
	jo	.L512
	movq	%rsi, %rbx
	addq	$1, %rbx
	jo	.L513
	movq	%rbx, %rdi
	call	malloc@PLT
	testq	%rax, %rax
	je	.L514
	cmpq	$8, %rbx
	jb	.L515
	movq	%rax, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L516
	movq	%rbp, (%rax)
	leaq	8(%rax), %rdx
	cmpq	$-8, %rax
	jnb	.L517
	leaq	8(%rax), %rbx
	leaq	1(%rbp), %rdx
	movq	%r12, %rsi
	movq	%rbx, %rdi
	call	memcpy@PLT
	popq	%rbx
	.cfi_remember_state
	.cfi_def_cfa_offset 24
	popq	%rbp
	.cfi_def_cfa_offset 16
	popq	%r12
	.cfi_def_cfa_offset 8
	ret
.L511:
	.cfi_restore_state
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data11(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L512:
	movl	$8, %edx
	movq	%rax, %rsi
	leaq	.Lubsan_data150(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L513:
	movl	$1, %edx
	leaq	.Lubsan_data151(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L514:
	movl	$0, %esi
	leaq	.Lubsan_data147(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L515:
	movq	%rax, %rsi
	leaq	.Lubsan_data148(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L516:
	movq	%rax, %rdi
	call	__asan_report_store8@PLT
.L517:
	movq	%rax, %rsi
	leaq	.Lubsan_data149(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
	.cfi_endproc
.LFE51:
	.size	hi_intern, .-hi_intern
	.type	sc, @function
sc:
.LASANPC53:
.LFB53:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$8, %rsp
	.cfi_def_cfa_offset 64
	testq	%rsi, %rsi
	je	.L527
	movq	%rdi, %rbp
	movq	%rsi, %r13
	movq	%rdx, %r12
	movq	%rsi, %rdi
	call	strlen@PLT
	movq	%rax, %rbx
	testq	%r12, %r12
	je	.L528
	movq	%r12, %rdi
	call	strlen@PLT
	movq	%rax, %r14
	movq	%rbx, %rsi
	addq	%rax, %rsi
	jo	.L529
	movq	%rbp, %rdi
	call	hs
	movq	%rax, %rbp
	testq	%rax, %rax
	je	.L530
	movq	%rbx, %rdx
	movq	%r13, %rsi
	movq	%rax, %rdi
	call	memcpy@PLT
	movq	%rbp, %rdx
	addq	%rbx, %rdx
	jc	.L531
	leaq	0(%rbp,%rbx), %rdi
	movq	%r14, %rdx
	movq	%r12, %rsi
	call	memcpy@PLT
	movq	%rbp, %rax
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L527:
	.cfi_restore_state
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data14(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L528:
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data15(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L529:
	movq	%rax, %rdx
	movq	%rbx, %rsi
	leaq	.Lubsan_data153(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L530:
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data16(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L531:
	movq	%rbp, %rsi
	leaq	.Lubsan_data152(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
	.cfi_endproc
.LFE53:
	.size	sc, .-sc
	.type	scopy, @function
scopy:
.LASANPC57:
.LFB57:
	.cfi_startproc
	pushq	%r12
	.cfi_def_cfa_offset 16
	.cfi_offset 12, -16
	pushq	%rbp
	.cfi_def_cfa_offset 24
	.cfi_offset 6, -24
	pushq	%rbx
	.cfi_def_cfa_offset 32
	.cfi_offset 3, -32
	testq	%rsi, %rsi
	je	.L536
	movq	%rdi, %rbx
	movq	%rsi, %rbp
	movq	%rsi, %rdi
	call	strlen@PLT
	movq	%rax, %r12
	movq	%rax, %rsi
	movq	%rbx, %rdi
	call	hs
	testq	%rax, %rax
	je	.L537
	movq	%r12, %rdx
	movq	%rbp, %rsi
	movq	%rax, %rdi
	call	memcpy@PLT
	popq	%rbx
	.cfi_remember_state
	.cfi_def_cfa_offset 24
	popq	%rbp
	.cfi_def_cfa_offset 16
	popq	%r12
	.cfi_def_cfa_offset 8
	ret
.L536:
	.cfi_restore_state
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data20(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L537:
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data21(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
	.cfi_endproc
.LFE57:
	.size	scopy, .-scopy
	.section	.rodata.str1.1
.LC3:
	.string	"1 32 32 5 m:245"
	.text
	.type	ms_int_slot, @function
ms_int_slot:
.LASANPC153:
.LFB153:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$120, %rsp
	.cfi_def_cfa_offset 176
	movq	%rdi, %r12
	leaq	16(%rsp), %r14
	movq	%r14, 8(%rsp)
	cmpl	$0, __asan_option_detect_stack_use_after_return(%rip)
	jne	.L579
.L538:
	leaq	96(%r14), %rax
	movq	$1102416563, (%r14)
	leaq	.LC3(%rip), %rcx
	movq	%rcx, 8(%r14)
	leaq	.LASANPC153(%rip), %rcx
	movq	%rcx, 16(%r14)
	movq	%r14, %r15
	shrq	$3, %r15
	movl	$-235802127, 2147450880(%r15)
	movl	$-202116109, 2147450888(%r15)
	movdqu	176(%rsp), %xmm0
	movups	%xmm0, -64(%rax)
	movdqu	192(%rsp), %xmm1
	movups	%xmm1, -48(%rax)
	leaq	32(%r14), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L580
	movq	-64(%rax), %rbx
	leaq	-40(%rax), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L581
	movq	-40(%rax), %r13
	subq	$1, %r13
	testq	%r12, %r12
	je	.L582
	movq	%r12, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	movq	%r12, %rdx
	andl	$7, %edx
	cmpb	%dl, %al
	jg	.L545
	testb	%al, %al
	jne	.L583
.L545:
	movzbl	(%r12), %edx
	testb	%dl, %dl
	je	.L574
	movq	%r12, %rsi
	movabsq	$1469598103934665603, %rax
	movabsq	$1099511628211, %rdi
	jmp	.L550
.L579:
	movl	$96, %edi
	call	__asan_stack_malloc_1@PLT
	testq	%rax, %rax
	cmovne	%rax, %r14
	jmp	.L538
.L580:
	call	__asan_report_load8@PLT
.L581:
	call	__asan_report_load8@PLT
.L582:
	movl	$0, %esi
	leaq	.Lubsan_data154(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L583:
	movq	%r12, %rdi
	call	__asan_report_load1@PLT
.L584:
	leaq	.Lubsan_data155(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L585:
	leaq	.Lubsan_data156(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L549:
	movzbl	(%rsi), %edx
	testb	%dl, %dl
	je	.L546
.L550:
	testq	%rsi, %rsi
	je	.L584
	movzbl	%dl, %edx
	xorq	%rdx, %rax
	imulq	%rdi, %rax
	leaq	1(%rsi), %rdx
	cmpq	$-1, %rsi
	jnb	.L585
	addq	$1, %rsi
	movq	%rsi, %rdx
	shrq	$3, %rdx
	movzbl	2147450880(%rdx), %edx
	movq	%rsi, %rcx
	andl	$7, %ecx
	cmpb	%cl, %dl
	jg	.L549
	testb	%dl, %dl
	je	.L549
	movq	%rsi, %rdi
	call	__asan_report_load1@PLT
.L574:
	movabsq	$1469598103934665603, %rax
.L546:
	andq	%r13, %rax
	movq	%rax, %rbp
	salq	$3, %rax
	leaq	(%rbx,%rax), %rdx
	js	.L551
	cmpq	%rbx, %rdx
	jnb	.L552
.L553:
	movq	%rbx, %rsi
	leaq	.Lubsan_data158(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L551:
	cmpq	%rdx, %rbx
	jb	.L553
.L552:
	movq	%rbx, %rsi
	addq	%rax, %rsi
	je	.L554
	testb	$7, %sil
	jne	.L554
	movq	%rsi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L586
	movq	(%rsi), %rdi
	testq	%rdi, %rdi
	je	.L541
	leaq	(%rbx,%rax), %rdx
	testq	%rax, %rax
	js	.L558
	cmpq	%rbx, %rdx
	jnb	.L559
.L560:
	movq	%rbx, %rsi
	leaq	.Lubsan_data160(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L554:
	leaq	.Lubsan_data159(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L586:
	movq	%rsi, %rdi
	call	__asan_report_load8@PLT
.L558:
	cmpq	%rdx, %rbx
	jb	.L560
.L559:
	testq	%r12, %r12
	jne	.L576
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data38(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L563:
	cmpq	%rdx, %rbx
	jb	.L564
.L576:
	movq	%r12, %rsi
	call	strcmp@PLT
	testl	%eax, %eax
	je	.L541
	movq	%rbp, %rax
	addq	$1, %rax
	jo	.L587
	andq	%r13, %rax
	movq	%rax, %rbp
	leaq	0(,%rax,8), %rax
	leaq	(%rbx,%rax), %rdx
	testq	%rax, %rax
	js	.L567
	cmpq	%rbx, %rdx
	jnb	.L568
.L569:
	movq	%rbx, %rsi
	leaq	.Lubsan_data162(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L587:
	movl	$1, %edx
	movq	%rbp, %rsi
	leaq	.Lubsan_data164(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L567:
	cmpq	%rdx, %rbx
	jb	.L569
.L568:
	movq	%rbx, %rsi
	addq	%rax, %rsi
	je	.L570
	testb	$7, %sil
	jne	.L570
	movq	%rsi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L588
	movq	(%rsi), %rdi
	testq	%rdi, %rdi
	je	.L541
	leaq	(%rbx,%rax), %rdx
	testq	%rax, %rax
	js	.L563
	cmpq	%rbx, %rdx
	jnb	.L576
.L564:
	movq	%rbx, %rsi
	leaq	.Lubsan_data161(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L570:
	leaq	.Lubsan_data163(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L588:
	movq	%rsi, %rdi
	call	__asan_report_load8@PLT
.L541:
	cmpq	%r14, 8(%rsp)
	jne	.L589
	movl	$0, 2147450880(%r15)
	movl	$0, 2147450888(%r15)
.L540:
	movq	%rbp, %rax
	addq	$120, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L589:
	.cfi_restore_state
	movq	$1172321806, (%r14)
	movabsq	$-723401728380766731, %rax
	movq	%rax, 2147450880(%r15)
	movl	$-168430091, 2147450888(%r15)
	movq	120(%r14), %rax
	movb	$0, (%rax)
	jmp	.L540
	.cfi_endproc
.LFE153:
	.size	ms_int_slot, .-ms_int_slot
	.section	.rodata
	.align 32
.LC4:
	.string	"hier: spawn failed (cannot create thread)\n"
	.zero	53
	.text
	.type	hier_task_start, @function
hier_task_start:
.LASANPC36:
.LFB36:
	.cfi_startproc
	subq	$8, %rsp
	.cfi_def_cfa_offset 16
	testq	%rdi, %rdi
	je	.L591
	movq	%rdx, %rcx
	testb	$7, %dil
	jne	.L591
	testq	%rdi, %rdi
	je	.L599
	testq	%rsi, %rsi
	je	.L600
	movq	%rsi, %rdx
	movl	$0, %esi
	call	pthread_create@PLT
	testl	%eax, %eax
	jne	.L601
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
.L591:
	.cfi_restore_state
	movq	%rdi, %rsi
	leaq	.Lubsan_data165(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L599:
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data42(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L600:
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data43(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L601:
	leaq	stderr(%rip), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L602
	movq	stderr(%rip), %rcx
	testq	%rcx, %rcx
	je	.L603
	movl	$42, %edx
	movl	$1, %esi
	leaq	.LC4(%rip), %rdi
	call	fwrite@PLT
	call	__asan_handle_no_return@PLT
	movl	$1, %edi
	call	exit@PLT
.L602:
	call	__asan_report_load8@PLT
.L603:
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data45(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
	.cfi_endproc
.LFE36:
	.size	hier_task_start, .-hier_task_start
	.section	.rodata
	.align 32
.LC5:
	.string	"hier: task already waited\n"
	.zero	37
	.text
	.type	hier_task_join, @function
hier_task_join:
.LASANPC33:
.LFB33:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	pushq	%rbx
	.cfi_def_cfa_offset 24
	.cfi_offset 3, -24
	subq	$8, %rsp
	.cfi_def_cfa_offset 32
	movq	%rdi, %rbx
	testq	%rdi, %rdi
	je	.L605
	testb	$7, %dil
	jne	.L605
	leaq	176(%rdi), %rdx
	cmpq	$-176, %rdi
	jnb	.L615
	leaq	176(%rdi), %rbp
	movq	%rbp, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L608
	cmpb	$3, %al
	jle	.L616
.L608:
	cmpl	$0, 176(%rbx)
	jne	.L617
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L618
	movq	(%rbx), %rdi
	movl	$0, %esi
	call	pthread_join@PLT
	movq	%rbp, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L613
	cmpb	$3, %al
	jle	.L619
.L613:
	movl	$1, 176(%rbx)
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 24
	popq	%rbx
	.cfi_def_cfa_offset 16
	popq	%rbp
	.cfi_def_cfa_offset 8
	ret
.L605:
	.cfi_restore_state
	movq	%rbx, %rsi
	leaq	.Lubsan_data166(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L615:
	movq	%rdi, %rsi
	leaq	.Lubsan_data167(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L616:
	movq	%rbp, %rdi
	call	__asan_report_load4@PLT
.L617:
	leaq	stderr(%rip), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L620
	movq	stderr(%rip), %rcx
	testq	%rcx, %rcx
	je	.L621
	movl	$26, %edx
	movl	$1, %esi
	leaq	.LC5(%rip), %rdi
	call	fwrite@PLT
	call	__asan_handle_no_return@PLT
	movl	$1, %edi
	call	exit@PLT
.L620:
	call	__asan_report_load8@PLT
.L621:
	call	__asan_handle_no_return@PLT
	leaq	.Lubsan_data47(%rip), %rdi
	call	__ubsan_handle_nonnull_arg_abort@PLT
.L618:
	movq	%rbx, %rdi
	call	__asan_report_load8@PLT
.L619:
	movq	%rbp, %rdi
	call	__asan_report_store4@PLT
	.cfi_endproc
.LFE33:
	.size	hier_task_join, .-hier_task_join
	.section	.rodata.str1.8,"aMS",@progbits,1
	.align 8
.LC6:
	.string	"2 32 160 10 _scope:403 256 160 7 _b1:405"
	.text
	.globl	h_buildE0
	.type	h_buildE0, @function
h_buildE0:
.LASANPC202:
.LFB202:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$504, %rsp
	.cfi_def_cfa_offset 560
	movq	%rdi, %r12
	movq	%rsi, %r13
	leaq	16(%rsp), %rbp
	movq	%rbp, (%rsp)
	cmpl	$0, __asan_option_detect_stack_use_after_return(%rip)
	jne	.L646
.L622:
	leaq	480(%rbp), %r14
	movq	$1102416563, 0(%rbp)
	leaq	.LC6(%rip), %rax
	movq	%rax, 8(%rbp)
	leaq	.LASANPC202(%rip), %rax
	movq	%rax, 16(%rbp)
	movq	%rbp, %rbx
	shrq	$3, %rbx
	movl	$-235802127, 2147450880(%rbx)
	movl	$-218959118, 2147450904(%rbx)
	movl	$-218959118, 2147450908(%rbx)
	movl	$-202116109, 2147450932(%rbx)
	movl	$-202116109, 2147450936(%rbx)
	testq	%r12, %r12
	je	.L626
	leaq	-448(%r14), %rdi
	movq	%r12, %rsi
	call	arena_child
.L627:
	testq	%r13, %r13
	jle	.L647
	movq	%r13, %r15
	subq	$1, %r15
	jo	.L648
	movq	%r15, %rsi
	movq	%r12, %rdi
	call	h_buildE0
	movq	%rax, 8(%rsp)
	movq	%r15, %rsi
	movq	%r12, %rdi
	call	h_buildE0
	movq	%rax, %r13
	movl	$24, %esi
	movq	%r12, %rdi
	call	amem
	movq	%rax, %r12
	movq	%rax, %rsi
	testq	%rax, %rax
	je	.L637
	testb	$7, %al
	jne	.L637
	movq	%rax, %rdi
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L639
	cmpb	$3, %al
	jle	.L649
.L639:
	movl	$1, (%r12)
	leaq	8(%r12), %rdx
	cmpq	$-8, %r12
	jnb	.L650
	leaq	8(%r12), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L651
	movq	%r13, 8(%r12)
	leaq	16(%r12), %rdx
	cmpq	$-16, %r12
	jnb	.L652
	leaq	16(%r12), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L653
	movq	8(%rsp), %rax
	movq	%rax, 16(%r12)
	leaq	-448(%r14), %rdi
	call	arena_free
.L625:
	cmpq	%rbp, (%rsp)
	jne	.L654
	movl	$0, 2147450880(%rbx)
	movq	$0, 2147450904(%rbx)
	movq	$0, 2147450932(%rbx)
.L624:
	movq	%r12, %rax
	addq	$504, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L646:
	.cfi_restore_state
	movl	$480, %edi
	call	__asan_stack_malloc_3@PLT
	testq	%rax, %rax
	cmovne	%rax, %rbp
	jmp	.L622
.L626:
	leaq	-448(%r14), %rdi
	movl	$0, %esi
	call	arena_new
	jmp	.L627
.L647:
	leaq	-224(%r14), %rdi
	leaq	-448(%r14), %rsi
	call	arena_child
	movl	$16, %esi
	movq	%r12, %rdi
	call	amem
	movq	%rax, %r12
	movq	%rax, %rsi
	testq	%rax, %rax
	je	.L629
	testb	$7, %al
	jne	.L629
	movq	%rax, %rdi
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L631
	cmpb	$3, %al
	jle	.L655
.L631:
	movl	$0, (%r12)
	leaq	8(%r12), %rdx
	cmpq	$-8, %r12
	jnb	.L656
	leaq	8(%r12), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L657
	movq	%r13, 8(%r12)
	leaq	-224(%r14), %rdi
	call	arena_free
	leaq	-448(%r14), %rdi
	call	arena_free
	jmp	.L625
.L629:
	leaq	.Lubsan_data168(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L655:
	call	__asan_report_store4@PLT
.L656:
	movq	%r12, %rsi
	leaq	.Lubsan_data169(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L657:
	call	__asan_report_store8@PLT
.L648:
	movl	$1, %edx
	movq	%r13, %rsi
	leaq	.Lubsan_data173(%rip), %rdi
	call	__ubsan_handle_sub_overflow_abort@PLT
.L637:
	leaq	.Lubsan_data170(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L649:
	call	__asan_report_store4@PLT
.L650:
	movq	%r12, %rsi
	leaq	.Lubsan_data171(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L651:
	call	__asan_report_store8@PLT
.L652:
	movq	%r12, %rsi
	leaq	.Lubsan_data172(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L653:
	call	__asan_report_store8@PLT
.L654:
	movq	$1172321806, 0(%rbp)
	movdqa	.LC2(%rip), %xmm0
	movups	%xmm0, 2147450880(%rbx)
	movups	%xmm0, 2147450896(%rbx)
	movups	%xmm0, 2147450912(%rbx)
	movups	%xmm0, 2147450924(%rbx)
	movq	504(%rbp), %rax
	movb	$0, (%rax)
	jmp	.L624
	.cfi_endproc
.LFE202:
	.size	h_buildE0, .-h_buildE0
	.section	.rodata.str1.1
.LC7:
	.string	"1 32 160 10 _scope:412"
	.text
	.globl	h_sumE0
	.type	h_sumE0, @function
h_sumE0:
.LASANPC203:
.LFB203:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$280, %rsp
	.cfi_def_cfa_offset 336
	movq	%rdi, %r12
	movq	%rsi, %r14
	leaq	16(%rsp), %rbx
	movq	%rbx, (%rsp)
	cmpl	$0, __asan_option_detect_stack_use_after_return(%rip)
	jne	.L680
.L658:
	leaq	256(%rbx), %r13
	movq	$1102416563, (%rbx)
	leaq	.LC7(%rip), %rax
	movq	%rax, 8(%rbx)
	leaq	.LASANPC203(%rip), %rax
	movq	%rax, 16(%rbx)
	movq	%rbx, %rbp
	shrq	$3, %rbp
	movl	$-235802127, 2147450880(%rbp)
	movl	$-202116109, 2147450904(%rbp)
	movl	$-202116109, 2147450908(%rbp)
	testq	%r12, %r12
	je	.L662
	leaq	-224(%r13), %rdi
	movq	%r12, %rsi
	call	arena_child
.L663:
	testq	%r14, %r14
	je	.L664
	testb	$7, %r14b
	jne	.L664
	movq	%r14, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L666
	cmpb	$3, %al
	jle	.L681
.L666:
	movl	(%r14), %eax
	testl	%eax, %eax
	je	.L682
	cmpl	$1, %eax
	je	.L683
.L661:
	cmpq	%rbx, (%rsp)
	jne	.L684
	movl	$0, 2147450880(%rbp)
	movq	$0, 2147450904(%rbp)
.L660:
	movq	%r12, %rax
	addq	$280, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L680:
	.cfi_restore_state
	movl	$256, %edi
	call	__asan_stack_malloc_2@PLT
	testq	%rax, %rax
	cmovne	%rax, %rbx
	jmp	.L658
.L662:
	leaq	-224(%r13), %rdi
	movl	$0, %esi
	call	arena_new
	jmp	.L663
.L664:
	movq	%r14, %rsi
	leaq	.Lubsan_data174(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L681:
	movq	%r14, %rdi
	call	__asan_report_load4@PLT
.L682:
	leaq	8(%r14), %rdx
	cmpq	$-8, %r14
	jnb	.L685
	leaq	8(%r14), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L686
	movq	8(%r14), %r12
	leaq	-224(%r13), %rdi
	call	arena_free
	jmp	.L661
.L685:
	movq	%r14, %rsi
	leaq	.Lubsan_data175(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L686:
	call	__asan_report_load8@PLT
.L683:
	leaq	8(%r14), %rdx
	cmpq	$-8, %r14
	jnb	.L687
	leaq	8(%r14), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L688
	movq	8(%r14), %rsi
	leaq	16(%r14), %rdx
	cmpq	$-16, %r14
	jnb	.L689
	leaq	16(%r14), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L690
	movq	16(%r14), %r14
	movq	%r12, %rdi
	call	h_sumE0
	movq	%rax, %r15
	movq	%rax, 8(%rsp)
	movq	%r14, %rsi
	movq	%r12, %rdi
	call	h_sumE0
	addq	%rax, %r15
	movq	%r15, %r12
	jo	.L691
	leaq	-224(%r13), %rdi
	call	arena_free
	jmp	.L661
.L687:
	movq	%r14, %rsi
	leaq	.Lubsan_data176(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L688:
	call	__asan_report_load8@PLT
.L689:
	movq	%r14, %rsi
	leaq	.Lubsan_data177(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L690:
	call	__asan_report_load8@PLT
.L691:
	movq	%rax, %rdx
	movq	8(%rsp), %rsi
	leaq	.Lubsan_data178(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L684:
	movq	$1172321806, (%rbx)
	movdqa	.LC2(%rip), %xmm0
	movups	%xmm0, 2147450880(%rbp)
	movups	%xmm0, 2147450896(%rbp)
	movq	248(%rbx), %rax
	movb	$0, (%rax)
	jmp	.L660
	.cfi_endproc
.LFE203:
	.size	h_sumE0, .-h_sumE0
	.section	.rodata.str1.8
	.align 8
.LC8:
	.string	"3 32 160 10 _scope:425 256 160 7 _b1:428 480 160 6 _t:429"
	.text
	.globl	h_fillA
	.type	h_fillA, @function
h_fillA:
.LASANPC204:
.LFB204:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$776, %rsp
	.cfi_def_cfa_offset 832
	movq	%rdi, %rbp
	movq	%rsi, 24(%rsp)
	movq	%rdx, 32(%rsp)
	movq	%rcx, 16(%rsp)
	leaq	64(%rsp), %rax
	movq	%rax, 40(%rsp)
	movq	%rax, 48(%rsp)
	cmpl	$0, __asan_option_detect_stack_use_after_return(%rip)
	jne	.L707
.L692:
	movq	40(%rsp), %rax
	leaq	704(%rax), %rdx
	movq	%rdx, 8(%rsp)
	movq	$1102416563, (%rax)
	leaq	.LC8(%rip), %rcx
	movq	%rcx, 8(%rax)
	leaq	.LASANPC204(%rip), %rcx
	movq	%rcx, 16(%rax)
	movq	%rax, %rbx
	shrq	$3, %rbx
	movl	$-235802127, 2147450880(%rbx)
	movl	$-218959118, 2147450904(%rbx)
	movl	$-218959118, 2147450908(%rbx)
	movl	$-218959118, 2147450932(%rbx)
	movl	$-218959118, 2147450936(%rbx)
	movl	$-202116109, 2147450960(%rbx)
	movl	$-202116109, 2147450964(%rbx)
	testq	%rbp, %rbp
	je	.L696
	leaq	32(%rax), %rdi
	movq	%rbp, %rsi
	call	arena_child
.L699:
	movl	$0, %r12d
	cmpq	$0, 16(%rsp)
	jle	.L698
	movq	8(%rsp), %rax
	leaq	-448(%rax), %r15
	movq	%r15, %r14
	shrq	$3, %r14
	leaq	-224(%rax), %rdx
	movq	%rdx, %r13
	shrq	$3, %r13
	movq	%rbx, 56(%rsp)
	movq	%r15, (%rsp)
	movq	%rdx, %r15
.L697:
	leaq	2147450880(%r14), %rbx
	movl	$0, 2147450880(%r14)
	movl	$0, 4(%rbx)
	movl	$0, 8(%rbx)
	movl	$0, 12(%rbx)
	movl	$0, 16(%rbx)
	movq	8(%rsp), %rax
	leaq	-672(%rax), %rsi
	movq	(%rsp), %rdi
	call	arena_child
	leaq	2147450880(%r13), %rbp
	movl	$0, 2147450880(%r13)
	movl	$0, 4(%rbp)
	movl	$0, 8(%rbp)
	movl	$0, 12(%rbp)
	movl	$0, 16(%rbp)
	movl	$0, %esi
	movq	%r15, %rdi
	call	arena_new
	movq	%r12, %rdx
	movq	24(%rsp), %rsi
	movq	32(%rsp), %rdi
	call	Arr_int_push
	movq	%r15, %rdi
	call	arena_free
	movl	$-117901064, 2147450880(%r13)
	movl	$-117901064, 4(%rbp)
	movl	$-117901064, 8(%rbp)
	movl	$-117901064, 12(%rbp)
	movl	$-117901064, 16(%rbp)
	movq	(%rsp), %rdi
	call	arena_free
	movl	$-117901064, 2147450880(%r14)
	movl	$-117901064, 4(%rbx)
	movl	$-117901064, 8(%rbx)
	movl	$-117901064, 12(%rbx)
	movl	$-117901064, 16(%rbx)
	movq	%r12, %rax
	addq	$1, %rax
	jo	.L708
	movq	%rax, %r12
	cmpq	%rax, 16(%rsp)
	jg	.L697
	movq	56(%rsp), %rbx
.L698:
	movq	8(%rsp), %rdi
	subq	$672, %rdi
	call	arena_free
	movq	40(%rsp), %rdx
	cmpq	%rdx, 48(%rsp)
	jne	.L709
	movl	$0, 2147450880(%rbx)
	pxor	%xmm0, %xmm0
	movups	%xmm0, 2147450904(%rbx)
	movups	%xmm0, 2147450920(%rbx)
	movups	%xmm0, 2147450936(%rbx)
	movups	%xmm0, 2147450952(%rbx)
.L694:
	addq	$776, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L707:
	.cfi_restore_state
	movl	$704, %edi
	call	__asan_stack_malloc_4@PLT
	testq	%rax, %rax
	cmove	40(%rsp), %rax
	movq	%rax, 40(%rsp)
	jmp	.L692
.L696:
	movq	8(%rsp), %rax
	leaq	-672(%rax), %rdi
	movl	$0, %esi
	call	arena_new
	jmp	.L699
.L708:
	movl	$1, %edx
	movq	%r12, %rsi
	leaq	.Lubsan_data179(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L709:
	movq	%rdx, %rax
	movq	$1172321806, (%rdx)
	movdqa	.LC2(%rip), %xmm0
	movups	%xmm0, 2147450880(%rbx)
	movups	%xmm0, 2147450896(%rbx)
	movups	%xmm0, 2147450912(%rbx)
	movups	%xmm0, 2147450928(%rbx)
	movups	%xmm0, 2147450944(%rbx)
	movabsq	$-723401728380766731, %rdx
	movq	%rdx, 2147450960(%rbx)
	movq	1016(%rax), %rax
	movb	$0, (%rax)
	jmp	.L694
	.cfi_endproc
.LFE204:
	.size	h_fillA, .-h_fillA
	.section	.rodata.str1.8
	.align 8
.LC10:
	.string	"2 32 160 10 _scope:435 256 160 7 _b1:438"
	.section	.rodata
	.align 32
.LC11:
	.string	"y"
	.zero	62
	.text
	.globl	h_fz_sgrow
	.type	h_fz_sgrow, @function
h_fz_sgrow:
.LASANPC205:
.LFB205:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$536, %rsp
	.cfi_def_cfa_offset 592
	movq	%rdi, %rbp
	movq	%rsi, %rbx
	movq	%rdx, 8(%rsp)
	movq	%rcx, %r14
	leaq	48(%rsp), %rax
	movq	%rax, 32(%rsp)
	movq	%rax, 40(%rsp)
	cmpl	$0, __asan_option_detect_stack_use_after_return(%rip)
	jne	.L730
.L710:
	movq	32(%rsp), %rax
	leaq	480(%rax), %rcx
	movq	%rcx, 24(%rsp)
	movq	$1102416563, (%rax)
	leaq	.LC10(%rip), %rsi
	movq	%rsi, 8(%rax)
	leaq	.LASANPC205(%rip), %rdi
	movq	%rdi, 16(%rax)
	movq	%rax, %r15
	shrq	$3, %r15
	movl	$-235802127, 2147450880(%r15)
	movl	$-218959118, 2147450904(%r15)
	movl	$-218959118, 2147450908(%r15)
	movl	$-202116109, 2147450932(%r15)
	movl	$-202116109, 2147450936(%r15)
	testq	%rbp, %rbp
	je	.L714
	leaq	32(%rax), %rdi
	movq	%rbp, %rsi
	call	arena_child
.L717:
	movl	$0, %ebp
	testq	%r14, %r14
	jle	.L716
	movq	24(%rsp), %rax
	leaq	-224(%rax), %r13
	movq	%r13, %r12
	shrq	$3, %r12
	subq	$448, %rax
	movq	%rax, 16(%rsp)
	jmp	.L715
.L730:
	movl	$480, %edi
	call	__asan_stack_malloc_3@PLT
	testq	%rax, %rax
	cmove	32(%rsp), %rax
	movq	%rax, 32(%rsp)
	jmp	.L710
.L714:
	movq	24(%rsp), %rax
	leaq	-448(%rax), %rdi
	movl	$0, %esi
	call	arena_new
	jmp	.L717
.L718:
	movq	_l.8(%rip), %rdx
	testq	%rbx, %rbx
	je	.L719
	testb	$7, %bl
	jne	.L719
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L731
	movq	(%rbx), %rsi
	movq	8(%rsp), %rdi
	call	sc
	movq	%rbx, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L732
	movq	%rax, (%rbx)
	movq	%r13, %rdi
	call	arena_free
	movl	$-117901064, 2147450880(%r12)
	movl	$-117901064, 2147450884(%r12)
	movl	$-117901064, 2147450888(%r12)
	movl	$-117901064, 2147450892(%r12)
	movl	$-117901064, 2147450896(%r12)
	movq	%rbp, %rax
	addq	$1, %rax
	jo	.L733
	movq	%rax, %rbp
	cmpq	%rax, %r14
	jle	.L716
.L715:
	movl	$0, 2147450880(%r12)
	movl	$0, 2147450884(%r12)
	movl	$0, 2147450888(%r12)
	movl	$0, 2147450892(%r12)
	movl	$0, 2147450896(%r12)
	movq	16(%rsp), %rsi
	movq	%r13, %rdi
	call	arena_child
	cmpq	$0, _l.8(%rip)
	jne	.L718
	leaq	.LC11(%rip), %rdi
	call	hi_intern
	movq	%rax, _l.8(%rip)
	jmp	.L718
.L719:
	movq	%rbx, %rsi
	leaq	.Lubsan_data180(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L731:
	movq	%rbx, %rdi
	call	__asan_report_load8@PLT
.L732:
	movq	%rbx, %rdi
	call	__asan_report_store8@PLT
.L733:
	movl	$1, %edx
	movq	%rbp, %rsi
	leaq	.Lubsan_data181(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L716:
	movq	24(%rsp), %rdi
	subq	$448, %rdi
	call	arena_free
	movq	32(%rsp), %rcx
	cmpq	%rcx, 40(%rsp)
	jne	.L734
	movl	$0, 2147450880(%r15)
	pxor	%xmm0, %xmm0
	movups	%xmm0, 2147450904(%r15)
	movups	%xmm0, 2147450920(%r15)
	movl	$0, 2147450936(%r15)
.L712:
	addq	$536, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L734:
	.cfi_restore_state
	movq	$1172321806, (%rcx)
	movdqa	.LC2(%rip), %xmm0
	movups	%xmm0, 2147450880(%r15)
	movups	%xmm0, 2147450896(%r15)
	movups	%xmm0, 2147450912(%r15)
	movups	%xmm0, 2147450924(%r15)
	movq	504(%rcx), %rax
	movb	$0, (%rax)
	jmp	.L712
	.cfi_endproc
.LFE205:
	.size	h_fz_sgrow, .-h_fz_sgrow
	.section	.rodata.str1.8
	.align 8
.LC12:
	.string	"6 32 8 10 _fd0_0:448 64 8 10 _fc0_0:448 96 24 8 _ret:457 160 160 10 _scope:445 384 160 7 _b1:451 608 160 6 _t:452"
	.text
	.globl	h_mkarr
	.type	h_mkarr, @function
h_mkarr:
.LASANPC206:
.LFB206:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$936, %rsp
	.cfi_def_cfa_offset 992
	movq	%rdi, 48(%rsp)
	movq	%rsi, 56(%rsp)
	movq	%rdx, 24(%rsp)
	leaq	96(%rsp), %r13
	movq	%r13, 72(%rsp)
	cmpl	$0, __asan_option_detect_stack_use_after_return(%rip)
	jne	.L764
.L735:
	leaq	832(%r13), %rbx
	movq	$1102416563, 0(%r13)
	leaq	.LC12(%rip), %rax
	movq	%rax, 8(%r13)
	leaq	.LASANPC206(%rip), %rax
	movq	%rax, 16(%r13)
	movq	%r13, %r14
	shrq	$3, %r14
	movl	$-235802127, 2147450880(%r14)
	movl	$-218959360, 2147450884(%r14)
	movl	$-218959360, 2147450888(%r14)
	movl	$-234881024, 2147450892(%r14)
	movl	$-218959118, 2147450896(%r14)
	movl	$-218959118, 2147450920(%r14)
	movl	$-218959118, 2147450924(%r14)
	movl	$-218959118, 2147450948(%r14)
	movl	$-218959118, 2147450952(%r14)
	movl	$-202116109, 2147450976(%r14)
	movl	$-202116109, 2147450980(%r14)
	movq	56(%rsp), %rsi
	testq	%rsi, %rsi
	je	.L739
	leaq	-672(%rbx), %rdi
	call	arena_child
.L740:
	leaq	-800(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L765
	movq	$0, -800(%rbx)
	leaq	-768(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L766
	movq	$0, -768(%rbx)
	cmpq	$0, 24(%rsp)
	jle	.L759
	movq	$0, 8(%rsp)
	movl	$0, %ebp
	leaq	-448(%rbx), %r15
	movq	%r15, %r12
	shrq	$3, %r12
	leaq	-672(%rbx), %rax
	movq	%rax, 16(%rsp)
	leaq	-768(%rbx), %rdx
	movq	%rdx, %rax
	shrq	$3, %rax
	movq	%rax, 32(%rsp)
	movq	%rdx, 64(%rsp)
	movq	%r13, 80(%rsp)
	movq	%r14, 88(%rsp)
	jmp	.L757
.L764:
	movl	$832, %edi
	call	__asan_stack_malloc_4@PLT
	testq	%rax, %rax
	cmovne	%rax, %r13
	jmp	.L735
.L739:
	leaq	-672(%rbx), %rdi
	movl	$0, %esi
	call	arena_new
	jmp	.L740
.L765:
	call	__asan_report_store8@PLT
.L766:
	call	__asan_report_store8@PLT
.L770:
	movq	64(%rsp), %rdx
	movq	%rdx, %rdi
	call	__asan_report_load8@PLT
.L771:
	leaq	-800(%rbx), %rsi
	movq	%rbp, %rcx
	movq	64(%rsp), %rdx
	movq	16(%rsp), %rdi
	call	Arr_int_grow
	jmp	.L745
.L772:
	movq	%rax, %rdi
	call	__asan_report_load8@PLT
.L773:
	movl	$1, %edx
	movq	%rbp, %rsi
	leaq	.Lubsan_data184(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L749:
	cmpq	%r8, %rsi
	jb	.L751
.L750:
	addq	%rbp, %rsi
	movq	8(%rsp), %r13
	addq	$1, %r13
	jo	.L767
	movq	%r13, 8(%rsp)
	testq	%rsi, %rsi
	je	.L754
	testb	$7, %sil
	jne	.L754
	movq	%rsi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L768
	movq	%r13, (%rsi)
	leaq	-224(%rbx), %rbp
	movq	%rbp, %rdi
	call	arena_free
	shrq	$3, %rbp
	movl	$-117901064, 2147450880(%rbp)
	movl	$-117901064, 2147450884(%rbp)
	movl	$-117901064, 2147450888(%rbp)
	movl	$-117901064, 2147450892(%rbp)
	movl	$-117901064, 2147450896(%rbp)
	movq	%r15, %rdi
	call	arena_free
	movl	$-117901064, 2147450880(%r12)
	movl	$-117901064, 2147450884(%r12)
	movl	$-117901064, 2147450888(%r12)
	movl	$-117901064, 2147450892(%r12)
	movl	$-117901064, 2147450896(%r12)
	movq	24(%rsp), %rax
	cmpq	%rax, %r13
	jge	.L769
	movq	%r14, %rbp
.L757:
	movl	$0, 2147450880(%r12)
	movl	$0, 2147450884(%r12)
	movl	$0, 2147450888(%r12)
	movl	$0, 2147450892(%r12)
	movl	$0, 2147450896(%r12)
	movq	16(%rsp), %rsi
	movq	%r15, %rdi
	call	arena_child
	leaq	-224(%rbx), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	movl	$0, 2147450880(%rdx)
	movl	$0, 2147450884(%rdx)
	movl	$0, 2147450888(%rdx)
	movl	$0, 2147450892(%rdx)
	movl	$0, 2147450896(%rdx)
	movl	$0, %esi
	call	arena_new
	movq	32(%rsp), %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L770
	cmpq	%rbp, -768(%rbx)
	je	.L771
.L745:
	leaq	-800(%rbx), %rax
	movq	%rax, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L772
	movq	-800(%rbx), %rsi
	movq	%rbp, %r14
	addq	$1, %r14
	jo	.L773
	movq	%r14, 40(%rsp)
	salq	$3, %rbp
	leaq	(%rsi,%rbp), %r8
	js	.L749
	cmpq	%rsi, %r8
	jnb	.L750
.L751:
	movq	%r8, %rdx
	leaq	.Lubsan_data182(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L767:
	movl	$1, %edx
	movq	8(%rsp), %rsi
	leaq	.Lubsan_data185(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L754:
	leaq	.Lubsan_data183(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L768:
	movq	%rsi, %rdi
	call	__asan_report_store8@PLT
.L769:
	movq	40(%rsp), %rcx
	movq	80(%rsp), %r13
	movq	88(%rsp), %r14
.L743:
	leaq	-800(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L774
	movq	-800(%rbx), %rdx
	leaq	-800(%rbx), %rax
	shrq	$3, %rax
	movb	$-8, 2147450880(%rax)
	leaq	-768(%rbx), %rax
	shrq	$3, %rax
	movb	$-8, 2147450880(%rax)
	leaq	-736(%rbx), %rdi
	movq	56(%rsp), %rsi
	call	Arr_int_from
	leaq	-672(%rbx), %rdi
	call	arena_free
	movq	48(%rsp), %rcx
	movdqa	-736(%rbx), %xmm1
	movups	%xmm1, (%rcx)
	movq	-720(%rbx), %rax
	movq	%rax, 16(%rcx)
	cmpq	%r13, 72(%rsp)
	jne	.L775
	pxor	%xmm0, %xmm0
	movups	%xmm0, 2147450880(%r14)
	movl	$0, 2147450896(%r14)
	movups	%xmm0, 2147450920(%r14)
	movups	%xmm0, 2147450936(%r14)
	movups	%xmm0, 2147450952(%r14)
	movups	%xmm0, 2147450968(%r14)
.L737:
	movq	48(%rsp), %rax
	addq	$936, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L759:
	.cfi_restore_state
	movl	$0, %ecx
	jmp	.L743
.L774:
	call	__asan_report_load8@PLT
.L775:
	movq	$1172321806, 0(%r13)
	movdqa	.LC2(%rip), %xmm0
	movups	%xmm0, 2147450880(%r14)
	movups	%xmm0, 2147450896(%r14)
	movups	%xmm0, 2147450912(%r14)
	movups	%xmm0, 2147450928(%r14)
	movups	%xmm0, 2147450944(%r14)
	movups	%xmm0, 2147450960(%r14)
	movabsq	$-723401728380766731, %rax
	movq	%rax, 2147450976(%r14)
	movq	1016(%r13), %rax
	movb	$0, (%rax)
	jmp	.L737
	.cfi_endproc
.LFE206:
	.size	h_mkarr, .-h_mkarr
	.section	.rodata.str1.8
	.align 8
.LC13:
	.string	"7 32 8 10 _fd0_0:463 64 8 10 _fc0_0:463 96 24 8 _ret:473 160 24 7 h_a:459 224 160 10 _scope:460 448 160 7 _b1:466 672 160 6 _t:468"
	.text
	.globl	h_xform
	.type	h_xform, @function
h_xform:
.LASANPC207:
.LFB207:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$1000, %rsp
	.cfi_def_cfa_offset 1056
	movq	%rdi, 48(%rsp)
	movq	%rsi, 56(%rsp)
	leaq	96(%rsp), %r13
	movq	%r13, 64(%rsp)
	cmpl	$0, __asan_option_detect_stack_use_after_return(%rip)
	jne	.L822
.L776:
	leaq	896(%r13), %rbp
	movq	$1102416563, 0(%r13)
	leaq	.LC13(%rip), %rax
	movq	%rax, 8(%r13)
	leaq	.LASANPC207(%rip), %rax
	movq	%rax, 16(%r13)
	movq	%r13, %r15
	shrq	$3, %r15
	movl	$-235802127, 2147450880(%r15)
	movl	$-218959360, 2147450884(%r15)
	movl	$-218959360, 2147450888(%r15)
	movl	$-234881024, 2147450892(%r15)
	movl	$-218959118, 2147450896(%r15)
	movl	$-234881024, 2147450900(%r15)
	movl	$-218959118, 2147450904(%r15)
	movl	$-218959118, 2147450928(%r15)
	movl	$-218959118, 2147450932(%r15)
	movl	$-218959118, 2147450956(%r15)
	movl	$-218959118, 2147450960(%r15)
	movl	$-202116109, 2147450984(%r15)
	movl	$-202116109, 2147450988(%r15)
	movdqu	1056(%rsp), %xmm1
	movaps	%xmm1, -736(%rbp)
	movq	1072(%rsp), %rax
	movq	%rax, -720(%rbp)
	movq	56(%rsp), %rsi
	testq	%rsi, %rsi
	je	.L780
	leaq	-672(%rbp), %rdi
	call	arena_child
.L781:
	leaq	-864(%rbp), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L823
	movq	$0, -864(%rbp)
	leaq	-832(%rbp), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L824
	movq	$0, -832(%rbp)
	leaq	-728(%rbp), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L825
	movq	-728(%rbp), %r8
	testq	%r8, %r8
	jle	.L814
	movl	$0, %ecx
	movl	$0, %ebx
	leaq	-448(%rbp), %r11
	movq	%r11, %r14
	shrq	$3, %r14
	leaq	-736(%rbp), %r9
	movq	%r9, %rax
	shrq	$3, %rax
	movq	%rax, 24(%rsp)
	movq	%r8, 32(%rsp)
	movq	%r9, 88(%rsp)
	movq	%r13, 72(%rsp)
	movq	%rcx, %r13
	movq	%r15, 80(%rsp)
	movq	%r11, 8(%rsp)
	jmp	.L812
.L822:
	movl	$896, %edi
	call	__asan_stack_malloc_4@PLT
	testq	%rax, %rax
	cmovne	%rax, %r13
	jmp	.L776
.L780:
	leaq	-672(%rbp), %rdi
	movl	$0, %esi
	call	arena_new
	jmp	.L781
.L823:
	call	__asan_report_store8@PLT
.L824:
	call	__asan_report_store8@PLT
.L825:
	call	__asan_report_load8@PLT
.L837:
	movq	88(%rsp), %r9
	movq	%r9, %rdi
	call	__asan_report_load8@PLT
.L787:
	cmpq	%rdx, %rax
	jb	.L789
.L788:
	addq	%rcx, %rax
	je	.L790
	testb	$7, %al
	jne	.L790
	movq	%rax, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L826
	movq	(%rax), %r12
	addq	$1, %r12
	jo	.L827
	movq	%r12, 16(%rsp)
	leaq	-224(%rbp), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	movl	$0, 2147450880(%rdx)
	movl	$0, 2147450884(%rdx)
	movl	$0, 2147450888(%rdx)
	movl	$0, 2147450892(%rdx)
	movl	$0, 2147450896(%rdx)
	movl	$0, %esi
	call	arena_new
	leaq	-832(%rbp), %rax
	movq	%rax, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L828
	cmpq	%rbx, -832(%rbp)
	je	.L829
.L796:
	movabsq	$-6640827866535438581, %rax
	imulq	%r12
	addq	%r12, %rdx
	sarq	$6, %rdx
	movq	%r12, %rax
	sarq	$63, %rax
	subq	%rax, %rdx
	imulq	$100, %rdx, %rdx
	jo	.L830
	movq	%rdx, %rdi
	leaq	-864(%rbp), %rcx
	movq	%rcx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L831
	movq	-864(%rbp), %rsi
	movq	%rbx, %r15
	addq	$1, %r15
	jo	.L832
	movq	%r15, 40(%rsp)
	salq	$3, %rbx
	leaq	(%rsi,%rbx), %r10
	js	.L802
	cmpq	%rsi, %r10
	jnb	.L803
.L804:
	movq	%r10, %rdx
	leaq	.Lubsan_data188(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L790:
	movq	%rax, %rsi
	leaq	.Lubsan_data187(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L826:
	movq	%rax, %rdi
	call	__asan_report_load8@PLT
.L827:
	movq	(%rax), %rsi
	movl	$1, %edx
	leaq	.Lubsan_data190(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L828:
	movq	%rax, %rdi
	call	__asan_report_load8@PLT
.L829:
	leaq	-832(%rbp), %rdx
	leaq	-864(%rbp), %rsi
	leaq	-672(%rbp), %rdi
	movq	%rbx, %rcx
	call	Arr_int_grow
	jmp	.L796
.L830:
	movabsq	$-6640827866535438581, %rdx
	movq	%r12, %rax
	imulq	%rdx
	leaq	(%rdx,%r12), %rsi
	sarq	$6, %rsi
	sarq	$63, %r12
	subq	%r12, %rsi
	movl	$100, %edx
	leaq	.Lubsan_data191(%rip), %rdi
	call	__ubsan_handle_mul_overflow_abort@PLT
.L831:
	movq	%rcx, %rdi
	call	__asan_report_load8@PLT
.L832:
	movl	$1, %edx
	movq	%rbx, %rsi
	leaq	.Lubsan_data192(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L802:
	cmpq	%r10, %rsi
	jb	.L804
.L803:
	addq	%rbx, %rsi
	movq	16(%rsp), %rax
	subq	%rdi, %rax
	jo	.L833
	testq	%rsi, %rsi
	je	.L807
	testb	$7, %sil
	jne	.L807
	movq	%rsi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L834
	movq	%rax, (%rsi)
	leaq	-224(%rbp), %rbx
	movq	%rbx, %rdi
	call	arena_free
	shrq	$3, %rbx
	movl	$-117901064, 2147450880(%rbx)
	movl	$-117901064, 2147450884(%rbx)
	movl	$-117901064, 2147450888(%rbx)
	movl	$-117901064, 2147450892(%rbx)
	movl	$-117901064, 2147450896(%rbx)
	movq	8(%rsp), %rdi
	call	arena_free
	movl	$-117901064, 2147450880(%r14)
	movl	$-117901064, 2147450884(%r14)
	movl	$-117901064, 2147450888(%r14)
	movl	$-117901064, 2147450892(%r14)
	movl	$-117901064, 2147450896(%r14)
	movq	%r13, %rax
	addq	$1, %rax
	jo	.L835
	movq	%rax, %r13
	cmpq	%rax, 32(%rsp)
	jle	.L836
	movq	%r15, %rbx
.L812:
	movl	$0, 2147450880(%r14)
	movl	$0, 2147450884(%r14)
	movl	$0, 2147450888(%r14)
	movl	$0, 2147450892(%r14)
	movl	$0, 2147450896(%r14)
	leaq	-672(%rbp), %rsi
	movq	8(%rsp), %rdi
	call	arena_child
	movq	24(%rsp), %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L837
	movq	-736(%rbp), %rax
	leaq	0(,%r13,8), %rcx
	leaq	(%rax,%rcx), %rdx
	testq	%rcx, %rcx
	js	.L787
	cmpq	%rax, %rdx
	jnb	.L788
.L789:
	movq	%rax, %rsi
	leaq	.Lubsan_data186(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L833:
	movq	%r12, %rsi
	leaq	.Lubsan_data193(%rip), %rdi
	call	__ubsan_handle_sub_overflow_abort@PLT
.L807:
	leaq	.Lubsan_data189(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L834:
	movq	%rsi, %rdi
	call	__asan_report_store8@PLT
.L835:
	movq	%r13, %rsi
	movl	$1, %edx
	leaq	.Lubsan_data194(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L836:
	movq	40(%rsp), %rcx
	movq	72(%rsp), %r13
	movq	80(%rsp), %r15
.L785:
	leaq	-864(%rbp), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L838
	movq	-864(%rbp), %rdx
	leaq	-864(%rbp), %rax
	shrq	$3, %rax
	movb	$-8, 2147450880(%rax)
	leaq	-832(%rbp), %rax
	shrq	$3, %rax
	movb	$-8, 2147450880(%rax)
	leaq	-800(%rbp), %rdi
	movq	56(%rsp), %rsi
	call	Arr_int_from
	leaq	-672(%rbp), %rdi
	call	arena_free
	movq	48(%rsp), %rcx
	movdqa	-800(%rbp), %xmm2
	movups	%xmm2, (%rcx)
	movq	-784(%rbp), %rax
	movq	%rax, 16(%rcx)
	cmpq	%r13, 64(%rsp)
	jne	.L839
	pxor	%xmm0, %xmm0
	movups	%xmm0, 2147450880(%r15)
	movups	%xmm0, 2147450892(%r15)
	movups	%xmm0, 2147450928(%r15)
	movups	%xmm0, 2147450944(%r15)
	movups	%xmm0, 2147450960(%r15)
	movups	%xmm0, 2147450976(%r15)
.L778:
	movq	48(%rsp), %rax
	addq	$1000, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L814:
	.cfi_restore_state
	movl	$0, %ecx
	jmp	.L785
.L838:
	call	__asan_report_load8@PLT
.L839:
	movq	$1172321806, 0(%r13)
	movdqa	.LC2(%rip), %xmm0
	movups	%xmm0, 2147450880(%r15)
	movups	%xmm0, 2147450896(%r15)
	movups	%xmm0, 2147450912(%r15)
	movups	%xmm0, 2147450928(%r15)
	movups	%xmm0, 2147450944(%r15)
	movups	%xmm0, 2147450960(%r15)
	movups	%xmm0, 2147450976(%r15)
	movq	1016(%r13), %rax
	movb	$0, (%rax)
	jmp	.L778
	.cfi_endproc
.LFE207:
	.size	h_xform, .-h_xform
	.section	.rodata.str1.8
	.align 8
.LC14:
	.string	"8 32 8 9 <unknown> 64 8 10 _fd0_0:484 96 8 10 _fc0_0:484 128 24 9 <unknown> 192 160 10 _scope:476 416 160 7 _b1:478 640 160 7 _b1:487 864 160 6 _t:488"
	.section	.rodata
	.align 32
.LC15:
	.string	"neg"
	.zero	60
	.text
	.globl	h_mkRes
	.type	h_mkRes, @function
h_mkRes:
.LASANPC208:
.LFB208:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$1192, %rsp
	.cfi_def_cfa_offset 1248
	movq	%rdi, %r13
	movq	%rsi, 48(%rsp)
	movq	%rdx, 16(%rsp)
	leaq	96(%rsp), %r15
	movq	%r15, 64(%rsp)
	cmpl	$0, __asan_option_detect_stack_use_after_return(%rip)
	jne	.L879
.L840:
	leaq	1088(%r15), %rbx
	movq	$1102416563, (%r15)
	leaq	.LC14(%rip), %rax
	movq	%rax, 8(%r15)
	leaq	.LASANPC208(%rip), %rax
	movq	%rax, 16(%r15)
	movq	%r15, %r14
	shrq	$3, %r14
	movl	$-235802127, 2147450880(%r14)
	movl	$-218959360, 2147450884(%r14)
	movl	$-218959360, 2147450888(%r14)
	movl	$-218959360, 2147450892(%r14)
	movl	$-234881024, 2147450896(%r14)
	movl	$-218959118, 2147450900(%r14)
	movl	$-218959118, 2147450924(%r14)
	movl	$-218959118, 2147450928(%r14)
	movl	$-218959118, 2147450952(%r14)
	movl	$-218959118, 2147450956(%r14)
	movl	$-218959118, 2147450980(%r14)
	movl	$-218959118, 2147450984(%r14)
	movl	$-202116109, 2147451008(%r14)
	movl	$-202116109, 2147451012(%r14)
	movq	48(%rsp), %rsi
	testq	%rsi, %rsi
	je	.L844
	leaq	-896(%rbx), %rdi
	call	arena_child
.L845:
	cmpq	$0, 16(%rsp)
	js	.L880
	leaq	-1024(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L881
	movq	$0, -1024(%rbx)
	leaq	-992(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L882
	movq	$0, -992(%rbx)
	cmpq	$0, 16(%rsp)
	jle	.L874
	movq	$0, 8(%rsp)
	movl	$0, %ebp
	leaq	-448(%rbx), %rdx
	movq	%rdx, %r12
	shrq	$3, %r12
	leaq	-896(%rbx), %rax
	movq	%rax, 24(%rsp)
	leaq	-992(%rbx), %r9
	movq	%r9, %rax
	shrq	$3, %rax
	movq	%rax, 32(%rsp)
	movq	%r9, 56(%rsp)
	movq	%r15, 72(%rsp)
	movq	%r14, 80(%rsp)
	movq	%r13, 88(%rsp)
	movq	%rdx, %r15
	jmp	.L869
.L879:
	movl	$1088, %edi
	call	__asan_stack_malloc_5@PLT
	testq	%rax, %rax
	cmovne	%rax, %r15
	jmp	.L840
.L844:
	leaq	-896(%rbx), %rdi
	movl	$0, %esi
	call	arena_new
	jmp	.L845
.L880:
	leaq	-672(%rbx), %rdi
	leaq	-896(%rbx), %rsi
	call	arena_child
	cmpq	$0, _l.7(%rip)
	je	.L883
.L847:
	movq	_l.7(%rip), %rax
	leaq	-1056(%rbx), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L884
	movq	%rax, -1056(%rbx)
	leaq	-1056(%rbx), %rdx
	movl	$8, %esi
	movq	48(%rsp), %rdi
	call	hbox
	movq	%rax, %rbp
	leaq	-672(%rbx), %rdi
	call	arena_free
	leaq	-896(%rbx), %rdi
	call	arena_free
	movq	%r13, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L849
	cmpb	$3, %al
	jle	.L885
.L849:
	movl	$0, 0(%r13)
	leaq	8(%r13), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L886
	movq	$0, 8(%r13)
	leaq	16(%r13), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L887
	movq	%rbp, 16(%r13)
	jmp	.L843
.L883:
	leaq	.LC15(%rip), %rdi
	call	hi_intern
	movq	%rax, _l.7(%rip)
	jmp	.L847
.L884:
	call	__asan_report_store8@PLT
.L885:
	movq	%r13, %rdi
	call	__asan_report_store4@PLT
.L886:
	call	__asan_report_store8@PLT
.L887:
	call	__asan_report_store8@PLT
.L881:
	call	__asan_report_store8@PLT
.L882:
	call	__asan_report_store8@PLT
.L891:
	movq	56(%rsp), %r9
	movq	%r9, %rdi
	call	__asan_report_load8@PLT
.L892:
	leaq	-1024(%rbx), %rsi
	movq	%rbp, %rcx
	movq	56(%rsp), %rdx
	movq	24(%rsp), %rdi
	call	Arr_int_grow
	jmp	.L857
.L893:
	movq	%rax, %rdi
	call	__asan_report_load8@PLT
.L894:
	movl	$1, %edx
	movq	%rbp, %rsi
	leaq	.Lubsan_data197(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L861:
	cmpq	%r8, %rsi
	jb	.L863
.L862:
	addq	%rbp, %rsi
	movq	8(%rsp), %r13
	addq	$1, %r13
	jo	.L888
	movq	%r13, 8(%rsp)
	testq	%rsi, %rsi
	je	.L866
	testb	$7, %sil
	jne	.L866
	movq	%rsi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L889
	movq	%r13, (%rsi)
	leaq	-224(%rbx), %rbp
	movq	%rbp, %rdi
	call	arena_free
	shrq	$3, %rbp
	movl	$-117901064, 2147450880(%rbp)
	movl	$-117901064, 2147450884(%rbp)
	movl	$-117901064, 2147450888(%rbp)
	movl	$-117901064, 2147450892(%rbp)
	movl	$-117901064, 2147450896(%rbp)
	movq	%r15, %rdi
	call	arena_free
	movl	$-117901064, 2147450880(%r12)
	movl	$-117901064, 2147450884(%r12)
	movl	$-117901064, 2147450888(%r12)
	movl	$-117901064, 2147450892(%r12)
	movl	$-117901064, 2147450896(%r12)
	movq	16(%rsp), %rax
	cmpq	%rax, %r13
	jge	.L890
	movq	%r14, %rbp
.L869:
	movl	$0, 2147450880(%r12)
	movl	$0, 2147450884(%r12)
	movl	$0, 2147450888(%r12)
	movl	$0, 2147450892(%r12)
	movl	$0, 2147450896(%r12)
	movq	24(%rsp), %rsi
	movq	%r15, %rdi
	call	arena_child
	leaq	-224(%rbx), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	movl	$0, 2147450880(%rdx)
	movl	$0, 2147450884(%rdx)
	movl	$0, 2147450888(%rdx)
	movl	$0, 2147450892(%rdx)
	movl	$0, 2147450896(%rdx)
	movl	$0, %esi
	call	arena_new
	movq	32(%rsp), %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L891
	cmpq	%rbp, -992(%rbx)
	je	.L892
.L857:
	leaq	-1024(%rbx), %rax
	movq	%rax, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L893
	movq	-1024(%rbx), %rsi
	movq	%rbp, %r14
	addq	$1, %r14
	jo	.L894
	movq	%r14, 40(%rsp)
	salq	$3, %rbp
	leaq	(%rsi,%rbp), %r8
	js	.L861
	cmpq	%rsi, %r8
	jnb	.L862
.L863:
	movq	%r8, %rdx
	leaq	.Lubsan_data195(%rip), %rdi
	call	__ubsan_handle_pointer_overflow_abort@PLT
.L888:
	movl	$1, %edx
	movq	8(%rsp), %rsi
	leaq	.Lubsan_data198(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L866:
	leaq	.Lubsan_data196(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L889:
	movq	%rsi, %rdi
	call	__asan_report_store8@PLT
.L890:
	movq	40(%rsp), %rcx
	movq	72(%rsp), %r15
	movq	80(%rsp), %r14
	movq	88(%rsp), %r13
.L855:
	leaq	-1024(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L895
	movq	-1024(%rbx), %rdx
	leaq	-1024(%rbx), %rax
	shrq	$3, %rax
	movb	$-8, 2147450880(%rax)
	leaq	-992(%rbx), %rax
	shrq	$3, %rax
	movb	$-8, 2147450880(%rax)
	leaq	-960(%rbx), %rbp
	movq	%rbp, %rax
	shrq	$3, %rax
	movw	$0, 2147450880(%rax)
	movb	$0, 2147450882(%rax)
	movq	48(%rsp), %r12
	movq	%r12, %rsi
	movq	%rbp, %rdi
	call	Arr_int_from
	movq	%rbp, %rdx
	movl	$24, %esi
	movq	%r12, %rdi
	call	hbox
	movq	%rax, %rbp
	leaq	-896(%rbx), %rdi
	call	arena_free
	movq	%r13, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L871
	cmpb	$3, %al
	jle	.L896
.L871:
	movl	$1, 0(%r13)
	leaq	8(%r13), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L897
	movq	%rbp, 8(%r13)
	leaq	16(%r13), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L898
	movq	$0, 16(%r13)
.L843:
	cmpq	%r15, 64(%rsp)
	jne	.L899
	pxor	%xmm0, %xmm0
	movups	%xmm0, 2147450880(%r14)
	movq	$0, 2147450896(%r14)
	movq	$0, 2147450924(%r14)
	movups	%xmm0, 2147450952(%r14)
	movups	%xmm0, 2147450968(%r14)
	movups	%xmm0, 2147450984(%r14)
	movups	%xmm0, 2147451000(%r14)
.L842:
	movq	%r13, %rax
	addq	$1192, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L874:
	.cfi_restore_state
	movl	$0, %ecx
	jmp	.L855
.L895:
	call	__asan_report_load8@PLT
.L896:
	movq	%r13, %rdi
	call	__asan_report_store4@PLT
.L897:
	call	__asan_report_store8@PLT
.L898:
	call	__asan_report_store8@PLT
.L899:
	movq	$1172321806, (%r15)
	movq	64(%rsp), %rdx
	movl	$1088, %esi
	movq	%r15, %rdi
	call	__asan_stack_free_5@PLT
	jmp	.L842
	.cfi_endproc
.LFE208:
	.size	h_mkRes, .-h_mkRes
	.section	.rodata.str1.8
	.align 8
.LC16:
	.string	"4 32 8 9 <unknown> 64 8 9 <unknown> 96 160 10 _scope:496 320 160 7 _b1:498"
	.text
	.globl	h_orret_mk
	.type	h_orret_mk, @function
h_orret_mk:
.LASANPC209:
.LFB209:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$568, %rsp
	.cfi_def_cfa_offset 624
	movq	%rdi, %rbp
	movq	%rsi, %r14
	movq	%rdx, %r15
	leaq	16(%rsp), %r13
	movq	%r13, 8(%rsp)
	cmpl	$0, __asan_option_detect_stack_use_after_return(%rip)
	jne	.L918
.L900:
	leaq	544(%r13), %r12
	movq	$1102416563, 0(%r13)
	leaq	.LC16(%rip), %rax
	movq	%rax, 8(%r13)
	leaq	.LASANPC209(%rip), %rax
	movq	%rax, 16(%r13)
	movq	%r13, %rbx
	shrq	$3, %rbx
	movl	$-235802127, 2147450880(%rbx)
	movl	$-218959360, 2147450884(%rbx)
	movl	$-218959360, 2147450888(%rbx)
	movl	$-218959118, 2147450912(%rbx)
	movl	$-218959118, 2147450916(%rbx)
	movl	$-202116109, 2147450940(%rbx)
	movl	$-202116109, 2147450944(%rbx)
	testq	%r14, %r14
	je	.L904
	leaq	-448(%r12), %rdi
	movq	%r14, %rsi
	call	arena_child
.L905:
	testq	%r15, %r15
	js	.L919
	leaq	-480(%r12), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L920
	movq	%r15, -480(%r12)
	leaq	-480(%r12), %rdx
	movl	$8, %esi
	movq	%r14, %rdi
	call	hbox
	movq	%rax, %r14
	leaq	-448(%r12), %rdi
	call	arena_free
	movq	%rbp, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L914
	cmpb	$3, %al
	jle	.L921
.L914:
	movl	$1, 0(%rbp)
	leaq	8(%rbp), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L922
	movq	%r14, 8(%rbp)
	leaq	16(%rbp), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L923
	movq	$0, 16(%rbp)
.L903:
	cmpq	%r13, 8(%rsp)
	jne	.L924
	movq	$0, 2147450880(%rbx)
	movl	$0, 2147450888(%rbx)
	movq	$0, 2147450912(%rbx)
	movq	$0, 2147450940(%rbx)
.L902:
	movq	%rbp, %rax
	addq	$568, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L918:
	.cfi_restore_state
	movl	$544, %edi
	call	__asan_stack_malloc_4@PLT
	testq	%rax, %rax
	cmovne	%rax, %r13
	jmp	.L900
.L904:
	leaq	-448(%r12), %rdi
	movl	$0, %esi
	call	arena_new
	jmp	.L905
.L919:
	leaq	-224(%r12), %rdi
	leaq	-448(%r12), %rsi
	call	arena_child
	cmpq	$0, _l.6(%rip)
	je	.L925
.L907:
	movq	_l.6(%rip), %rax
	leaq	-512(%r12), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L926
	movq	%rax, -512(%r12)
	leaq	-512(%r12), %rdx
	movl	$8, %esi
	movq	%r14, %rdi
	call	hbox
	movq	%rax, %r14
	leaq	-224(%r12), %rdi
	call	arena_free
	leaq	-448(%r12), %rdi
	call	arena_free
	movq	%rbp, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L909
	cmpb	$3, %al
	jle	.L927
.L909:
	movl	$0, 0(%rbp)
	leaq	8(%rbp), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L928
	movq	$0, 8(%rbp)
	leaq	16(%rbp), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L929
	movq	%r14, 16(%rbp)
	jmp	.L903
.L925:
	leaq	.LC15(%rip), %rdi
	call	hi_intern
	movq	%rax, _l.6(%rip)
	jmp	.L907
.L926:
	call	__asan_report_store8@PLT
.L927:
	movq	%rbp, %rdi
	call	__asan_report_store4@PLT
.L928:
	call	__asan_report_store8@PLT
.L929:
	call	__asan_report_store8@PLT
.L920:
	call	__asan_report_store8@PLT
.L921:
	movq	%rbp, %rdi
	call	__asan_report_store4@PLT
.L922:
	call	__asan_report_store8@PLT
.L923:
	call	__asan_report_store8@PLT
.L924:
	movq	$1172321806, 0(%r13)
	movdqa	.LC2(%rip), %xmm0
	movups	%xmm0, 2147450880(%rbx)
	movups	%xmm0, 2147450896(%rbx)
	movups	%xmm0, 2147450912(%rbx)
	movups	%xmm0, 2147450928(%rbx)
	movl	$-168430091, 2147450944(%rbx)
	movq	1016(%r13), %rax
	movb	$0, (%rax)
	jmp	.L902
	.cfi_endproc
.LFE209:
	.size	h_orret_mk, .-h_orret_mk
	.section	.rodata.str1.8
	.align 8
.LC17:
	.string	"6 32 8 9 <unknown> 64 8 9 <unknown> 96 8 9 <unknown> 128 24 9 _or_x:506 192 24 9 _or_y:510 256 160 10 _scope:505"
	.text
	.globl	h_orret_chain
	.type	h_orret_chain, @function
h_orret_chain:
.LASANPC210:
.LFB210:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$504, %rsp
	.cfi_def_cfa_offset 560
	movq	%rdi, %r12
	movq	%rsi, %r14
	movq	%rdx, %r15
	leaq	16(%rsp), %r13
	movq	%r13, (%rsp)
	cmpl	$0, __asan_option_detect_stack_use_after_return(%rip)
	jne	.L970
.L930:
	leaq	480(%r13), %rbx
	movq	$1102416563, 0(%r13)
	leaq	.LC17(%rip), %rax
	movq	%rax, 8(%r13)
	leaq	.LASANPC210(%rip), %rax
	movq	%rax, 16(%r13)
	movq	%r13, %rbp
	shrq	$3, %rbp
	movl	$-235802127, 2147450880(%rbp)
	movl	$-218959360, 2147450884(%rbp)
	movl	$-218959360, 2147450888(%rbp)
	movl	$-218959360, 2147450892(%rbp)
	movl	$-234881024, 2147450896(%rbp)
	movl	$-218959118, 2147450900(%rbp)
	movl	$-234881024, 2147450904(%rbp)
	movl	$-218959118, 2147450908(%rbp)
	movl	$-202116109, 2147450932(%rbp)
	movl	$-202116109, 2147450936(%rbp)
	testq	%r14, %r14
	je	.L934
	leaq	-224(%rbx), %rdi
	movq	%r14, %rsi
	call	arena_child
.L935:
	leaq	-352(%rbx), %rdi
	leaq	-224(%rbx), %rsi
	movq	%r15, %rdx
	call	h_orret_mk
	cmpl	$0, -352(%rbx)
	je	.L971
	movq	-344(%rbx), %rsi
	testq	%rsi, %rsi
	je	.L945
	testb	$7, %sil
	jne	.L945
	movq	%rsi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L972
	movq	(%rsi), %rax
	movq	%rax, 8(%rsp)
	movq	%r15, %rdx
	addq	$1, %rdx
	jo	.L973
	leaq	-288(%rbx), %rdi
	leaq	-224(%rbx), %rsi
	call	h_orret_mk
	cmpl	$0, -288(%rbx)
	je	.L974
	movq	-280(%rbx), %rsi
	testq	%rsi, %rsi
	je	.L958
	testb	$7, %sil
	jne	.L958
	movq	%rsi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L975
	movq	8(%rsp), %rax
	addq	(%rsi), %rax
	jo	.L976
	leaq	-384(%rbx), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L977
	movq	%rax, -384(%rbx)
	leaq	-384(%rbx), %rdx
	movl	$8, %esi
	movq	%r14, %rdi
	call	hbox
	movq	%rax, %r14
	leaq	-224(%rbx), %rdi
	call	arena_free
	movq	%r12, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L964
	cmpb	$3, %al
	jle	.L978
.L964:
	movl	$1, (%r12)
	leaq	8(%r12), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L979
	movq	%r14, 8(%r12)
	leaq	16(%r12), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L980
	movq	$0, 16(%r12)
.L933:
	cmpq	%r13, (%rsp)
	jne	.L981
	pxor	%xmm0, %xmm0
	movups	%xmm0, 2147450880(%rbp)
	movups	%xmm0, 2147450896(%rbp)
	movq	$0, 2147450932(%rbp)
.L932:
	movq	%r12, %rax
	addq	$504, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L970:
	.cfi_restore_state
	movl	$480, %edi
	call	__asan_stack_malloc_3@PLT
	testq	%rax, %rax
	cmovne	%rax, %r13
	jmp	.L930
.L934:
	leaq	-224(%rbx), %rdi
	movl	$0, %esi
	call	arena_new
	jmp	.L935
.L971:
	movq	-336(%rbx), %rsi
	testq	%rsi, %rsi
	je	.L937
	testb	$7, %sil
	jne	.L937
	movq	%rsi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L982
	movq	(%rsi), %rsi
	movq	%r14, %rdi
	call	scopy
	leaq	-448(%rbx), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L983
	movq	%rax, -448(%rbx)
	leaq	-448(%rbx), %rdx
	movl	$8, %esi
	movq	%r14, %rdi
	call	hbox
	movq	%rax, %r14
	leaq	-224(%rbx), %rdi
	call	arena_free
	movq	%r12, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L941
	cmpb	$3, %al
	jle	.L984
.L941:
	movl	$0, (%r12)
	leaq	8(%r12), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L985
	movq	$0, 8(%r12)
	leaq	16(%r12), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L986
	movq	%r14, 16(%r12)
	jmp	.L933
.L937:
	leaq	.Lubsan_data199(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L982:
	movq	%rsi, %rdi
	call	__asan_report_load8@PLT
.L983:
	call	__asan_report_store8@PLT
.L984:
	movq	%r12, %rdi
	call	__asan_report_store4@PLT
.L985:
	call	__asan_report_store8@PLT
.L986:
	call	__asan_report_store8@PLT
.L945:
	leaq	.Lubsan_data200(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L972:
	movq	%rsi, %rdi
	call	__asan_report_load8@PLT
.L973:
	movl	$1, %edx
	movq	%r15, %rsi
	leaq	.Lubsan_data203(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L974:
	movq	-272(%rbx), %rsi
	testq	%rsi, %rsi
	je	.L951
	testb	$7, %sil
	jne	.L951
	movq	%rsi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L987
	movq	(%rsi), %rsi
	movq	%r14, %rdi
	call	scopy
	leaq	-416(%rbx), %rdi
	movq	%rdi, %rdx
	shrq	$3, %rdx
	cmpb	$0, 2147450880(%rdx)
	jne	.L988
	movq	%rax, -416(%rbx)
	leaq	-416(%rbx), %rdx
	movl	$8, %esi
	movq	%r14, %rdi
	call	hbox
	movq	%rax, %r14
	leaq	-224(%rbx), %rdi
	call	arena_free
	movq	%r12, %rax
	shrq	$3, %rax
	movzbl	2147450880(%rax), %eax
	testb	%al, %al
	je	.L955
	cmpb	$3, %al
	jle	.L989
.L955:
	movl	$0, (%r12)
	leaq	8(%r12), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L990
	movq	$0, 8(%r12)
	leaq	16(%r12), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L991
	movq	%r14, 16(%r12)
	jmp	.L933
.L951:
	leaq	.Lubsan_data201(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L987:
	movq	%rsi, %rdi
	call	__asan_report_load8@PLT
.L988:
	call	__asan_report_store8@PLT
.L989:
	movq	%r12, %rdi
	call	__asan_report_store4@PLT
.L990:
	call	__asan_report_store8@PLT
.L991:
	call	__asan_report_store8@PLT
.L958:
	leaq	.Lubsan_data202(%rip), %rdi
	call	__ubsan_handle_type_mismatch_v1_abort@PLT
.L975:
	movq	%rsi, %rdi
	call	__asan_report_load8@PLT
.L976:
	movq	(%rsi), %rdx
	movq	8(%rsp), %rsi
	leaq	.Lubsan_data204(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L977:
	call	__asan_report_store8@PLT
.L978:
	movq	%r12, %rdi
	call	__asan_report_store4@PLT
.L979:
	call	__asan_report_store8@PLT
.L980:
	call	__asan_report_store8@PLT
.L981:
	movq	$1172321806, 0(%r13)
	movdqa	.LC2(%rip), %xmm0
	movups	%xmm0, 2147450880(%rbp)
	movups	%xmm0, 2147450896(%rbp)
	movups	%xmm0, 2147450912(%rbp)
	movups	%xmm0, 2147450924(%rbp)
	movq	504(%r13), %rax
	movb	$0, (%rax)
	jmp	.L932
	.cfi_endproc
.LFE210:
	.size	h_orret_chain, .-h_orret_chain
	.section	.rodata.str1.1
.LC18:
	.string	"1 32 160 10 _scope:517"
	.text
	.globl	h_pair2
	.type	h_pair2, @function
h_pair2:
.LASANPC211:
.LFB211:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$280, %rsp
	.cfi_def_cfa_offset 336
	movq	%rdi, %r14
	movq	%rsi, %rbx
	leaq	16(%rsp), %rbp
	movq	%rbp, %r15
	cmpl	$0, __asan_option_detect_stack_use_after_return(%rip)
	jne	.L1005
.L992:
	leaq	256(%rbp), %r13
	movq	$1102416563, 0(%rbp)
	leaq	.LC18(%rip), %rax
	movq	%rax, 8(%rbp)
	leaq	.LASANPC211(%rip), %rax
	movq	%rax, 16(%rbp)
	movq	%rbp, %r12
	shrq	$3, %r12
	movl	$-235802127, 2147450880(%r12)
	movl	$-202116109, 2147450904(%r12)
	movl	$-202116109, 2147450908(%r12)
	testq	%r14, %r14
	je	.L996
	leaq	-224(%r13), %rdi
	movq	%r14, %rsi
	call	arena_child
.L997:
	movq	%rbx, %rax
	addq	$1, %rax
	movq	%rax, 8(%rsp)
	jo	.L1006
	imulq	$2, %rbx, %r14
	jo	.L1007
	leaq	-224(%r13), %rdi
	call	arena_free
	movq	8(%rsp), %rax
	movq	%r14, %rdx
	cmpq	%rbp, %r15
	jne	.L1008
	movl	$0, 2147450880(%r12)
	movq	$0, 2147450904(%r12)
.L994:
	addq	$280, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L1005:
	.cfi_restore_state
	movl	$256, %edi
	call	__asan_stack_malloc_2@PLT
	testq	%rax, %rax
	cmovne	%rax, %rbp
	jmp	.L992
.L996:
	leaq	-224(%r13), %rdi
	movl	$0, %esi
	call	arena_new
	jmp	.L997
.L1006:
	movl	$1, %edx
	movq	%rbx, %rsi
	leaq	.Lubsan_data205(%rip), %rdi
	call	__ubsan_handle_add_overflow_abort@PLT
.L1007:
	movl	$2, %edx
	movq	%rbx, %rsi
	leaq	.Lubsan_data206(%rip), %rdi
	call	__ubsan_handle_mul_overflow_abort@PLT
.L1008:
	movq	$1172321806, 0(%rbp)
	movdqa	.LC2(%rip), %xmm0
	movups	%xmm0, 2147450880(%r12)
	movups	%xmm0, 2147450896(%r12)
	movq	248(%rbp), %rcx
	movb	$0, (%rcx)
	jmp	.L994
	.cfi_endproc
.LFE211:
	.size	h_pair2, .-h_pair2
	.section	.rodata.str1.8
	.align 8
.LC19:
	.string	"2 32 8 9 <unknown> 64 160 10 _scope:521"
	.text
	.globl	h_mkadder
	.type	h_mkadder, @function
h_mkadder:
.LASANPC212:
.LFB212:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$312, %rsp
	.cfi_def_cfa_offset 368
	movq	%rdi, %rbx
	movq	%rsi, %r15
	movq	%rdx, %r13
	leaq	16(%rsp), %r12
	movq	%r12, 8(%rsp)
	cmpl	$0, __asan_option_detect_stack_use_after_return(%rip)
	jne	.L1021
.L1009:
	leaq	288(%r12), %rbp
	movq	$1102416563, (%r12)
	leaq	.LC19(%rip), %rax
	movq	%rax, 8(%r12)
	leaq	.LASANPC212(%rip), %rax
	movq	%rax, 16(%r12)
	movq	%r12, %r14
	shrq	$3, %r14
	movl	$-235802127, 2147450880(%r14)
	movl	$-218959360, 2147450884(%r14)
	movl	$-202116109, 2147450908(%r14)
	movl	$-202116109, 2147450912(%r14)
	testq	%r15, %r15
	je	.L1013
	leaq	-224(%rbp), %rdi
	movq	%r15, %rsi
	call	arena_child
.L1014:
	leaq	-256(%rbp), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L1022
	movq	%r13, -256(%rbp)
	leaq	-256(%rbp), %rdx
	leaq	-224(%rbp), %rdi
	movl	$8, %esi
	call	hbox
	movq	%rax, %r13
	testq	%rax, %rax
	je	.L1016
	movq	%rax, %rsi
	movq	%r15, %rdi
	call	Env_0_copy
	movq	%rax, %r13
.L1016:
	leaq	-224(%rbp), %rdi
	call	arena_free
	movq	%rbx, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L1023
	movq	%r13, (%rbx)
	leaq	8(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L1024
	leaq	h___lam0__clo(%rip), %rax
	movq	%rax, 8(%rbx)
	leaq	16(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L1025
	leaq	Env_0_copy(%rip), %rax
	movq	%rax, 16(%rbx)
	cmpq	%r12, 8(%rsp)
	jne	.L1026
	movq	$0, 2147450880(%r14)
	movq	$0, 2147450908(%r14)
.L1011:
	movq	%rbx, %rax
	addq	$312, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 56
	popq	%rbx
	.cfi_def_cfa_offset 48
	popq	%rbp
	.cfi_def_cfa_offset 40
	popq	%r12
	.cfi_def_cfa_offset 32
	popq	%r13
	.cfi_def_cfa_offset 24
	popq	%r14
	.cfi_def_cfa_offset 16
	popq	%r15
	.cfi_def_cfa_offset 8
	ret
.L1021:
	.cfi_restore_state
	movl	$288, %edi
	call	__asan_stack_malloc_3@PLT
	testq	%rax, %rax
	cmovne	%rax, %r12
	jmp	.L1009
.L1013:
	leaq	-224(%rbp), %rdi
	movl	$0, %esi
	call	arena_new
	jmp	.L1014
.L1022:
	call	__asan_report_store8@PLT
.L1023:
	movq	%rbx, %rdi
	call	__asan_report_store8@PLT
.L1024:
	call	__asan_report_store8@PLT
.L1025:
	call	__asan_report_store8@PLT
.L1026:
	movq	$1172321806, (%r12)
	movdqa	.LC2(%rip), %xmm0
	movups	%xmm0, 2147450880(%r14)
	movups	%xmm0, 2147450896(%r14)
	movl	$-168430091, 2147450912(%r14)
	movq	504(%r12), %rax
	movb	$0, (%rax)
	jmp	.L1011
	.cfi_endproc
.LFE212:
	.size	h_mkadder, .-h_mkadder
	.section	.rodata.str1.8
	.align 8
.LC20:
	.string	"3 32 24 9 <unknown> 96 24 7 h_a:524 160 160 10 _scope:525"
	.text
	.globl	h_mksum
	.type	h_mksum, @function
h_mksum:
.LASANPC213:
.LFB213:
	.cfi_startproc
	pushq	%r15
	.cfi_def_cfa_offset 16
	.cfi_offset 15, -16
	pushq	%r14
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -24
	pushq	%r13
	.cfi_def_cfa_offset 32
	.cfi_offset 13, -32
	pushq	%r12
	.cfi_def_cfa_offset 40
	.cfi_offset 12, -40
	pushq	%rbp
	.cfi_def_cfa_offset 48
	.cfi_offset 6, -48
	pushq	%rbx
	.cfi_def_cfa_offset 56
	.cfi_offset 3, -56
	subq	$408, %rsp
	.cfi_def_cfa_offset 464
	movq	%rdi, %r12
	movq	%rsi, (%rsp)
	leaq	16(%rsp), %r13
	movq	%r13, 8(%rsp)
	cmpl	$0, __asan_option_detect_stack_use_after_return(%rip)
	jne	.L1040
.L1027:
	leaq	384(%r13), %rbx
	movq	$1102416563, 0(%r13)
	leaq	.LC20(%rip), %rax
	movq	%rax, 8(%r13)
	leaq	.LASANPC213(%rip), %rax
	movq	%rax, 16(%r13)
	movq	%r13, %rbp
	shrq	$3, %rbp
	movl	$-235802127, 2147450880(%rbp)
	movl	$-234881024, 2147450884(%rbp)
	movl	$-218959118, 2147450888(%rbp)
	movl	$-234881024, 2147450892(%rbp)
	movl	$-218959118, 2147450896(%rbp)
	movl	$-202116109, 2147450920(%rbp)
	movl	$-202116109, 2147450924(%rbp)
	movdqu	464(%rsp), %xmm1
	movaps	%xmm1, -288(%rbx)
	movq	480(%rsp), %rax
	movq	%rax, -272(%rbx)
	movq	(%rsp), %rsi
	testq	%rsi, %rsi
	je	.L1031
	leaq	-224(%rbx), %rdi
	call	arena_child
.L1032:
	leaq	-288(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L1041
	movq	-288(%rbx), %rdx
	leaq	-280(%rbx), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L1042
	leaq	-352(%rbx), %r15
	movq	-280(%rbx), %rcx
	leaq	-224(%rbx), %r14
	movq	%r14, %rsi
	movq	%r15, %rdi
	call	Arr_int_from
	movq	%r15, %rdx
	movl	$24, %esi
	movq	%r14, %rdi
	call	hbox
	movq	%rax, %r14
	testq	%rax, %rax
	je	.L1035
	movq	%rax, %rsi
	movq	(%rsp), %rdi
	call	Env_1_copy
	movq	%rax, %r14
.L1035:
	leaq	-224(%rbx), %rdi
	call	arena_free
	movq	%r12, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.L1043
	movq	%r14, (%r12)
	leaq	8(%r12), %rdi
	movq	%rdi, %rax
	shrq	$3, %rax
	cmpb	$0, 2147450880(%rax)
	jne	.