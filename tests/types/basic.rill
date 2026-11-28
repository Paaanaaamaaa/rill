x : i67 <~ 5
y <~ 5
z <~ x + 1
c <~ y < 9
msg <~ "hi"
big : u8 <~ 255

fact : i67 => i67
fact :: 0 => 1
     :: n => n * fact (n - 1)

twice :: a => a + a
add : i32 i32 => i32
add :: a b => a + b

>>! fact 20
>>! add 1 2
