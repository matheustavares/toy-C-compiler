# A [subset-of-] C compiler

Following the blogpost at:
[https://norasandler.com/2017/11/29/Write-a-Compiler.html](https://norasandler.com/2017/11/29/Write-a-Compiler.html).

## Clone and compile

```shell
$ git clone --recurse-submodules # To clone the tests submodule too
$ make
```

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

To test, we use a fork of the test repo [provided by
nlsandler](https://github.com/nlsandler/write_a_c_compiler). The fork
is available at [here](https://github.com/matheustavares/c-compiler-tests).
After cloning the submodule (and compiling the compiler), tests can be run
with:

```shell
$ make tests [STAGES="X Y ..."]
```

Use `STAGES=...` to run only the tests from a desired set of stages. (Available
stage names are the subdirectories of compiler-tests, without the "stage-"
prefix).

Note: the `compiler-tests/test_compiler.sh` script expects its caller to be
inside `compiler-tests`. So either cd to the dir before running it or use the
Makefile rule, which already takes care of that.
