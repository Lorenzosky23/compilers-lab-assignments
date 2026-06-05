; ModuleID = '../test/test2.ll'
source_filename = "test2.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@.str = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1

; Function Attrs: noinline nounwind uwtable
define dso_local void @test_licm(i32 noundef %0, i32 noundef %1, i32 noundef %2) #0 {
  %4 = add nsw i32 %1, %2
  %5 = mul nsw i32 %4, 2
  br label %6

6:                                                ; preds = %11, %3
  %.01 = phi i32 [ 0, %3 ], [ %10, %11 ]
  %.0 = phi i32 [ 0, %3 ], [ %12, %11 ]
  %7 = icmp slt i32 %.0, %0
  br i1 %7, label %8, label %13

8:                                                ; preds = %6
  %9 = add nsw i32 %5, %.0
  %10 = add nsw i32 %.01, %9
  br label %11

11:                                               ; preds = %8
  %12 = add nsw i32 %.0, 1
  br label %6, !llvm.loop !6

13:                                               ; preds = %6
  %14 = call i32 (ptr, ...) @printf(ptr noundef @.str, i32 noundef %.01)
  ret void
}

declare i32 @printf(ptr noundef, ...) #1

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @main() #0 {
  call void @test_licm(i32 noundef 10, i32 noundef 5, i32 noundef 3)
  ret i32 0
}

attributes #0 = { noinline nounwind uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

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
