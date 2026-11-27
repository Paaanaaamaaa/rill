fact :: 0 => 1
     :: n => n * fact (n-1)

eval :: a "+" b => a + b
     :: a "/" 0 => 0
     :: _ _ _ => 0

repl :: "" => 0
     :: line =>
        calc line
        repl line
sq :: x => x * x
>>! fact 20
