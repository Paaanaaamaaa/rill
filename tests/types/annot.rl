a : [i64] <~ 1 2 3
b : [u8] <~ 1 2 3
s : [i64..] <~ [1,3..]
c : [bool] <~ 1 2 3 < 2

sum : [i64] => i64
sum :: x => +/ x

head : [i64..] => i64
head :: z => z @ 0

firstn : i64 [i64..] => [i64]
firstn :: n s => n # s

>>! sum a
>>! sum 1 2 3
>>! firstn 3 s
t <~ sum (10 # s)
