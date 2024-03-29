; RUN: llc < %s -mtriple=i686-pc-linux-gnu -relocation-model=pic -asm-verbose=false -post-RA-scheduler=false | FileCheck %s -check-prefix=LINUX

@ptr = external global i32* 
@dst = external global i32 
@src = external global i32 

define void @test1() nounwind {
entry:
    store i32* @dst, i32** @ptr
    %tmp.s = load i32* @src
    store i32 %tmp.s, i32* @dst
    ret void
    
; LINUX:    test1:
; LINUX:	call	.L1$pb
; LINUX-NEXT: .L1$pb:
; LINUX-NEXT:	popl
; LINUX:	addl	$_GLOBAL_OFFSET_TABLE_+(.L{{.*}}-.L1$pb),
; LINUX:	movl	dst@GOT(%eax),
; LINUX:	movl	ptr@GOT(%eax),
; LINUX:	movl	src@GOT(%eax),
; LINUX:	ret
}

@ptr2 = global i32* null
@dst2 = global i32 0
@src2 = global i32 0

define void @test2() nounwind {
entry:
    store i32* @dst2, i32** @ptr2
    %tmp.s = load i32* @src2
    store i32 %tmp.s, i32* @dst2
    ret void
    
; LINUX: test2:
; LINUX:	call	.L2$pb
; LINUX-NEXT: .L2$pb:
; LINUX-NEXT:	popl
; LINUX:	addl	$_GLOBAL_OFFSET_TABLE_+(.L{{.*}}-.L2$pb), %eax
; LINUX:	movl	dst2@GOT(%eax),
; LINUX:	movl	ptr2@GOT(%eax),
; LINUX:	movl	src2@GOT(%eax),
; LINUX:	ret

}

declare i8* @malloc(i32)

define void @test3() nounwind {
entry:
    %ptr = call i8* @malloc(i32 40)
    ret void
; LINUX: test3:
; LINUX: 	pushl	%ebx
; LINUX-NEXT: 	subl	$8, %esp
; LINUX-NEXT: 	call	.L3$pb
; LINUX-NEXT: .L3$pb:
; LINUX-NEXT: 	popl	%ebx
; LINUX: 	addl	$_GLOBAL_OFFSET_TABLE_+(.L{{.*}}-.L3$pb), %ebx
; LINUX: 	movl	$40, (%esp)
; LINUX: 	call	malloc@PLT
; LINUX: 	addl	$8, %esp
; LINUX: 	popl	%ebx
; LINUX: 	ret
}

@pfoo = external global void(...)* 

define void @test4() nounwind {
entry:
    %tmp = call void(...)*(...)* @afoo()
    store void(...)* %tmp, void(...)** @pfoo
    %tmp1 = load void(...)** @pfoo
    call void(...)* %tmp1()
    ret void
; LINUX: test4:
; LINUX: 	call	.L4$pb
; LINUX-NEXT: .L4$pb:
; LINUX: 	popl
; LINUX: 	addl	$_GLOBAL_OFFSET_TABLE_+(.L{{.*}}-.L4$pb),
; LINUX: 	movl	pfoo@GOT(%esi),
; LINUX: 	call	afoo@PLT
; LINUX: 	call	*
}

declare void(...)* @afoo(...)

define void @test5() nounwind {
entry:
    call void(...)* @foo()
    ret void
; LINUX: test5:
; LINUX: call	.L5$pb
; LINUX: popl	%ebx
; LINUX: addl	$_GLOBAL_OFFSET_TABLE_+(.L{{.*}}-.L5$pb), %ebx
; LINUX: call	foo@PLT
}

declare void @foo(...)


@ptr6 = internal global i32* null
@dst6 = internal global i32 0
@src6 = internal global i32 0

define void @test6() nounwind {
entry:
    store i32* @dst6, i32** @ptr6
    %tmp.s = load i32* @src6
    store i32 %tmp.s, i32* @dst6
    ret void
    
; LINUX: test6:
; LINUX: 	call	.L6$pb
; LINUX-NEXT: .L6$pb:
; LINUX-NEXT: 	popl	%eax
; LINUX: 	addl	$_GLOBAL_OFFSET_TABLE_+(.L{{.*}}-.L6$pb), %eax
; LINUX: 	leal	dst6@GOTOFF(%eax), %ecx
; LINUX: 	movl	%ecx, ptr6@GOTOFF(%eax)
; LINUX: 	movl	src6@GOTOFF(%eax), %ecx
; LINUX: 	movl	%ecx, dst6@GOTOFF(%eax)
; LINUX: 	ret
}


;; Test constant pool references.
define double @test7(i32 %a.u) nounwind {
entry:
    %tmp = icmp eq i32 %a.u,0
    %retval = select i1 %tmp, double 4.561230e+02, double 1.234560e+02
    ret double %retval

; LINUX: .LCPI7_0:

; LINUX: test7:
; LINUX:    call .L7$pb
; LINUX: .L7$pb:
; LINUX:    addl	$_GLOBAL_OFFSET_TABLE_+(.L{{.*}}-.L7$pb), 
; LINUX:    fldl	.LCPI7_0@GOTOFF(
}


;; Test jump table references.
define void @test8(i32 %n.u) nounwind {
entry:
    switch i32 %n.u, label %bb12 [i32 1, label %bb i32 2, label %bb6 i32 4, label %bb7 i32 5, label %bb8 i32 6, label %bb10 i32 7, label %bb1 i32 8, label %bb3 i32 9, label %bb4 i32 10, label %bb9 i32 11, label %bb2 i32 12, label %bb5 i32 13, label %bb11 ]
bb:
    tail call void(...)* @foo1()
    ret void
bb1:
    tail call void(...)* @foo2()
    ret void
bb2:
    tail call void(...)* @foo6()
    ret void
bb3:
    tail call void(...)* @foo3()
    ret void
bb4:
    tail call void(...)* @foo4()
    ret void
bb5:
    tail call void(...)* @foo5()
    ret void
bb6:
    tail call void(...)* @foo1()
    ret void
bb7:
    tail call void(...)* @foo2()
    ret void
bb8:
    tail call void(...)* @foo6()
    ret void
bb9:
    tail call void(...)* @foo3()
    ret void
bb10:
    tail call void(...)* @foo4()
    ret void
bb11:
    tail call void(...)* @foo5()
    ret void
bb12:
    tail call void(...)* @foo6()
    ret void
    
; LINUX: test8:
; LINUX:   call	.L8$pb
; LINUX: .L8$pb:
; LINUX:   addl	$_GLOBAL_OFFSET_TABLE_+(.L{{.*}}-.L8$pb),
; LINUX:   addl	.LJTI8_0@GOTOFF(
; LINUX:   jmpl	*

; LINUX: .LJTI8_0:
; LINUX:   .long	 .LBB8_2@GOTOFF
; LINUX:   .long	 .LBB8_2@GOTOFF
; LINUX:   .long	 .LBB8_7@GOTOFF
; LINUX:   .long	 .LBB8_3@GOTOFF
; LINUX:   .long	 .LBB8_7@GOTOFF
}

declare void @foo1(...)
declare void @foo2(...)
declare void @foo6(...)
declare void @foo3(...)
declare void @foo4(...)
declare void @foo5(...)
