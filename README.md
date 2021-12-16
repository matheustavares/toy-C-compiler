# A [subset-of-] C compiler

Following the blogpost at:
[https://norasandler.com/2017/11/29/Write-a-Compiler.html](https://norasandler.com/2017/11/29/Write-a-Compiler.html).

## Running

```shell
$ ./cc file.c
# Creates the binary <file>
$ ./cc -S file.c
# Creates the assembly <file.s>

$ ./cc -l file.c
# Outputs the identified tokens, one per line.
$ ./cc -t file.c | dot -Tpng | display
# Outputs the abstract syntax tree in dot format. Use dot and
# ImageMagick (display) to show it.
```

## Overfiew of the files

- lexer.c: the tokenizer
- parser.c: a recursive descent parser
- x86.c: code generation to x86\_64 assembly (AT&T syntax)
- lib: miscellaneous utilities to handle errors, arrays, etc.

## Testing

See [https://github.com/nlsandler/write_a_c_compiler](https://github.com/nlsandler/write_a_c_compiler).
