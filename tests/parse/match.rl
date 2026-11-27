a <~ n ?| 0 => "zero" | _ => "other"
b <~ (n > 9) ?| 1 => "big" | _ => "small"
sign <~ x ?| 0 => 0
          | _ => 1
c <~ 7
