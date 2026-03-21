define dso_local i32 @f(i32 %a, i32 %b) #0 {
entry:
  %c = call i32 @id(i32 %b)
  %r = mul i32 %a, %c
  ret i32 %r
}

declare i32 @id(i32)

attributes #0 = { noinline nounwind optnone }
