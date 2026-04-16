; ModuleID = 'test_sr.c'
source_filename = "test_sr.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  %9 = alloca i32, align 4
  %10 = alloca i32, align 4
  store i32 0, ptr %1, align 4
  store i32 8, ptr %2, align 4
  %11 = load i32, ptr %2, align 4
  %12 = mul nsw i32 %11, 2
  store i32 %12, ptr %3, align 4
  %13 = load i32, ptr %2, align 4
  %14 = mul nsw i32 %13, 4
  store i32 %14, ptr %4, align 4
  %15 = load i32, ptr %2, align 4
  %16 = mul nsw i32 %15, 8
  store i32 %16, ptr %5, align 4
  %17 = load i32, ptr %2, align 4
  %18 = sdiv i32 %17, 2
  store i32 %18, ptr %6, align 4
  %19 = load i32, ptr %2, align 4
  %20 = sdiv i32 %19, 4
  store i32 %20, ptr %7, align 4
  %21 = load i32, ptr %2, align 4
  %22 = mul nsw i32 %21, 3
  store i32 %22, ptr %8, align 4
  %23 = load i32, ptr %2, align 4
  %24 = mul nsw i32 %23, 7
  store i32 %24, ptr %9, align 4
  %25 = load i32, ptr %2, align 4
  %26 = mul nsw i32 %25, 15
  store i32 %26, ptr %10, align 4
  %27 = load i32, ptr %3, align 4
  %28 = load i32, ptr %4, align 4
  %29 = add nsw i32 %27, %28
  %30 = load i32, ptr %5, align 4
  %31 = add nsw i32 %29, %30
  %32 = load i32, ptr %6, align 4
  %33 = add nsw i32 %31, %32
  %34 = load i32, ptr %7, align 4
  %35 = add nsw i32 %33, %34
  %36 = load i32, ptr %8, align 4
  %37 = add nsw i32 %35, %36
  %38 = load i32, ptr %9, align 4
  %39 = add nsw i32 %37, %38
  %40 = load i32, ptr %10, align 4
  %41 = add nsw i32 %39, %40
  ret i32 %41
}

attributes #0 = { noinline nounwind uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 19.1.7 (/home/runner/work/llvm-project/llvm-project/clang cd708029e0b2869e80abe31ddb175f7c35361f90)"}
