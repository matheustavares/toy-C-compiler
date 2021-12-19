# A [subset-of-]C compiler

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

There are two types of tests:

- General end-to-end tests, which are available at the submodule
  `compiler-tests`. This is [a
  fork](https://github.com/matheustavares/c-compiler-tests) of the test repo
  [provided by nlsandler](https://github.com/nlsandler/write_a_c_compiler).

- Tests of specific lib routines and APIs, available at `lib-tests`.

To run all tests, execute:

```shell
$ make tests
```

Or select only one type:

```shell
$ make compiler-tests [STAGES="X Y ..."]
$ make lib-tests
```

Use `STAGES=...` to limit the compiler-tests to a desired set of stages. (See
available stage names at the compiler-tests directory.) This can also be used
with the "tests" make rule.
