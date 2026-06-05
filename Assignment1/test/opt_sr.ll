; ModuleID = 'test_sr.ll'
source_filename = "test_sr.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @main(i32 noundef %0, ptr noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca ptr, align 8
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  %9 = alloca i32, align 4
  %10 = alloca i32, align 4
  %11 = alloca i32, align 4
  %12 = alloca i32, align 4
  %13 = alloca i32, align 4
  %14 = alloca i32, align 4
  store i32 0, ptr %3, align 4
  store i32 %0, ptr %4, align 4
  store ptr %1, ptr %5, align 8
  %15 = load i32, ptr %4, align 4
  store i32 %15, ptr %6, align 4
  %16 = load i32, ptr %6, align 4
  %17 = shl i32 %16, 1
  store i32 %17, ptr %7, align 4
  %18 = load i32, ptr %6, align 4
  %19 = shl i32 %18, 2
  store i32 %19, ptr %8, align 4
  %20 = load i32, ptr %6, align 4
  %21 = shl i32 %20, 3
  store i32 %21, ptr %9, align 4
  %22 = load i32, ptr %6, align 4
  %23 = ashr i32 %22, 1
  store i32 %23, ptr %10, align 4
  %24 = load i32, ptr %6, align 4
  %25 = ashr i32 %24, 2
  store i32 %25, ptr %11, align 4
  %26 = load i32, ptr %6, align 4
  %27 = shl i32 %26, 2
  %28 = sub i32 %27, %26
  store i32 %28, ptr %12, align 4
  %29 = load i32, ptr %6, align 4
  %30 = shl i32 %29, 3
  %31 = sub i32 %30, %29
  store i32 %31, ptr %13, align 4
  %32 = load i32, ptr %6, align 4
  %33 = shl i32 %32, 4
  %34 = sub i32 %33, %32
  store i32 %34, ptr %14, align 4
  %35 = load i32, ptr %7, align 4
  %36 = load i32, ptr %8, align 4
  %37 = add nsw i32 %35, %36
  %38 = load i32, ptr %9, align 4
  %39 = add nsw i32 %37, %38
  %40 = load i32, ptr %10, align 4
  %41 = add nsw i32 %39, %40
  %42 = load i32, ptr %11, align 4
  %43 = add nsw i32 %41, %42
  %44 = load i32, ptr %12, align 4
  %45 = add nsw i32 %43, %44
  %46 = load i32, ptr %13, align 4
  %47 = add nsw i32 %45, %46
  %48 = load i32, ptr %14, align 4
  %49 = add nsw i32 %47, %48
  ret i32 %49
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
