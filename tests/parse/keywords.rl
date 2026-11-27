orbit fib <~ 0 1 => (a b) => b (a+b)
>>! +/10#fib
quiesce 0
r <~ fixpoint f 1
manifold scratch 65536
    buf <~ 5
    >>! buf
lattice packet
    ver : 3
    len : 64
    id : 67
isomorph flip :: a b => b a
