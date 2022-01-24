# A [subset-of-]C compiler

A toy compiler I'm writing following the excellent tutorial from Nora Sandler:
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

Check `./cc -h` for all available options.

## Overview of the source files 

Main source files:

- cc.c: the CLI option parser and main entry point
- lexer.c: the tokenizer
- parser.c: a recursive descent parser
- x86.c: code generation to x86\_64 assembly (AT&T syntax)

Auxiliary source files:

- lib: miscellaneous utilities to handle errors, strings, arrays, etc.
- dot-printer.[ch]: prints an AST (abstract syntax tree) in dot format.
- symtable.[ch]: table of "currently known symbols" (vars and funcs) during
		 assembly generation.
- labelset.[ch]: set of user defined labels (i.e. those used in `goto`'s) to
		 assist the assembly generation.

## Testing

Tests are divided in three parts:

- Tests of internal lib routines and APIs, available at `lib-tests`.

- General end-to-end tests, which are available at the submodule
  `compiler-tests`. This is [a
  fork](https://github.com/matheustavares/c-compiler-tests) of the test repo
  [provided by Nora](https://github.com/nlsandler/write_a_c_compiler). These
  tests are input based and they check that the compiler fails on invalid
  input and succeeds (i.e. produces the same result as our reference compiler,
  gcc) on valid input.

- `extra-tests`: these are additional end-to-end tests that would not fit
  in the framework of the previous item. They check CLI options, default
  filenames, x86 conventions, etc.

To run all tests, execute:

```shell
$ make tests
```

Or select only one type:

```shell
$ make compiler-tests [STAGES="X Y ..."]
$ make lib-tests
$ make extra-tests
```

Use `STAGES=...` to limit the compiler-tests to a desired set of stages. (See
available stage names at the compiler-tests directory.) This can also be used
with the "tests" make rule.
