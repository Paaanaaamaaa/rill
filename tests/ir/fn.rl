fact : i67 => i67
fact :: 0 => 1
     :: n => n * fact (n - 1)

sq :: x => x * x
isbig :: n => n > 100

>>! fact 20
>>! sq 7
>>! isbig 150
r <~ #t ?| #t => 1 | #f => 0
>>! r
