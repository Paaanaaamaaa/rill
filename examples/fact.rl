fact : u67 => u67
fact :: 0 => 1
     :: n => n * fact (n - 1)

>>! fact 20
>>! fact 25
