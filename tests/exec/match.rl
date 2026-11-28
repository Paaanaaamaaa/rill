isbig :: n => n > 100
>>! isbig 150
>>! isbig 7
r <~ #t ?| #t => 1 | #f => 0
>>! r
quiesce 3
>>! "never"
