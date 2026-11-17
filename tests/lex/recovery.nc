// After a lexical error the lexer skips the bad input and continues.
a $ b      // ERROR E1002
c ` d      // ERROR E1002
x # y      // ERROR E1002
é z        // ERROR E1002
1.foo 2    // ERROR E1021
