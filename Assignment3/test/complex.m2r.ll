; ModuleID = '../test/complex.ll'
source_filename = "../test/complex.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: noinline nounwind uwtable
define dso_local void @complex_structure(i32 noundef %0) #0 {
  br label %2

2:                                                ; preds = %11, %1
  %.01 = phi i32 [ 0, %1 ], [ %12, %11 ]
  %3 = icmp slt i32 %.01, %0
  br i1 %3, label %4, label %13

4:                                                ; preds = %2
  br label %5

5:                                                ; preds = %8, %4
  %.02 = phi i32 [ 0, %4 ], [ %9, %8 ]
  %6 = icmp slt i32 %.02, %0
  br i1 %6, label %7, label %10

7:                                                ; preds = %5
  br label %8

8:                                                ; preds = %7
  %9 = add nsw i32 %.02, 1
  br label %5, !llvm.loop !6

10:                                               ; preds = %5
  br label %11

11:                                               ; preds = %10
  %12 = add nsw i32 %.01, 1
  br label %2, !llvm.loop !8

13:                                               ; preds = %2
  br label %14

14:                                               ; preds = %17, %13
  %.0 = phi i32 [ 0, %13 ], [ %18, %17 ]
  %15 = icmp slt i32 %.0, %0
  br i1 %15, label %16, label %19

16:                                               ; preds = %14
  br label %17

17:                                               ; preds = %16
  %18 = add nsw i32 %.0, 1
  br label %14, !llvm.loop !9

19:                                               ; preds = %14
  ret void
}

attributes #0 = { noinline nounwind uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"Ubuntu clang version 21.1.8 (6ubuntu1)"}
!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}
!8 = distinct !{!8, !7}
!9 = distinct !{!9, !7}
