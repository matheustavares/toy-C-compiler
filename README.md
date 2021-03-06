# A toy C compiler

A toy compiler for a (small) subset of the C11 standard. It generates 64-bit
x86 assembly (AT&T syntax), following the System V AMD64 ABI. `gcc` is called
for assembling and linking (so functions from the standard C library are
available). This is intended for fun and learning purposes, see
limitations [here](#current-features-and-limitations) and an overview of the
source files [here](#overview-of-the-source-files).

Developed following the excellent "Writing a C Compiler"
series by [Nora Sandler](https://github.com/nlsandler), available at:
[https://norasandler.com/2017/11/29/Write-a-Compiler.html](https://norasandler.com/2017/11/29/Write-a-Compiler.html).

## Example

The following C program is an example of what can be currently compiled. The
program receives an integer N on stdin and prints the Nth first Fibonacci
numbers on stdout.

```c
/* We don't have includes, so we must declare the prototypes. */
int getchar(void);
int putchar(int c);

int fibbonacci(int n)
{
	if (n == 1)
		return 0;
	if (n <= 3)
		return 1;
	/* Very inefficient. Only to demonstrate recursion. */
	return fibbonacci(n-1) + fibbonacci(n-2);
}

/*
 * Get an unsigned int from stdin. Return -1 on failure. The integer must
 * be followed by a newline and cannot be surrounded by spaces.
 *
 * Note that we only support `int` for now, so we obviously cannot use scanf.
 */
int getint(void)
{
	int val = 0, got_input = 0;
	/* We don't have 'EOF' define, so stop on any negative number. */
	for (int c = getchar(); c >= 0; c = getchar()) {
		if (c == 10) // newline
			break;
		if (c < 48 || c > 57) // Error: not a digit
			return -1;
		c -= 48;
		val *= 10;
		val += c;
		got_input = 1;
	}
	return got_input ? val : -1;
}

/* Prints an unsigned integer followed by a newline. */
void putint(int val)
{
	int divisor = 1;
	for (int val_cpy = val; val_cpy / 10; val_cpy /= 10)
		divisor *= 10;

	while (divisor) {
		int digit = val / divisor;
		putchar(digit + 48);
		val -= digit * divisor;
		divisor /= 10;
	}
	putchar(10);
}

/*
 * Receives a positive integer N from stdin and prints the first Nth
 * Fibonacci numbers to stdout.
 */
int main()
{
	int val = getint();
	if (val <= 0)
		return 1;
	for (int i = 1; i <= val; i++) {
		/* Again: inefficient, only for demonstration */
		putint(fibbonacci(i));
	}
	return 0;
}
```

## Clone and build the compiler

```shell
$ git clone --recurse-submodules # To clone the tests submodule too
$ make
# Produces the binary `./cc`.
# Note: it requires `gcc` for assembling and linking.
```

## Running

```shell
$ ./cc file.c
# Creates the binary file "a.out"
$ ./cc -S file.c
# Creates the assembly file "file.s"

$ ./cc -l file.c
# Outputs the identified tokens, one per line.
$ ./cc -t file.c | dot -Tpng | display
# Outputs the abstract syntax tree in dot format. Use dot and
# ImageMagick (display) to show it.
```

Check `./cc -h` for all available options.

## Current features and limitations

### Implemented:

- Types: signed integer only (`int`)
- Operators (for `int`):
	- [x] Arithmetic (`+`, `*`, etc.)
	- [x] Relational (`>=`, `<`, `==`, etc.)
	- [x] Bitwise (`<<`, `&`, etc.)
	- [x] Logical (`&&`, `||`, etc.)
	- [x] Compound assignment (`+=`, `*=`, etc.)
	- [x] Suffix/postfix increment and decrement (`++`, `--`, etc.)
	- [x] Ternary operator
- Statements:
	- [x] `for`, `while`, and `do-while` loops
	- [x] `if` and `if-else` statements
	- [x] `int` and `void` types
	- [x] `goto` and labels
	- [x] Function calls
- Other features:
	- [x] Inline and block comments
	- [x] Local variables (with scoping rules)
	- [x] Global variables
	- [x] Multiple source files support.

### Missing:

- [ ] Storage classifiers and qualifiers (`static`, `extern`, `volatile`, `const`, etc.)
- [ ] Pointers and arrays (and the `*` and `&` operators)
- [ ] Structs and unions (and the `.` and `->` operators)
- [ ] Preprocessing (macros, includes, ifdef, pragmas, etc.)
- [ ] Strings
- [ ] Many other types: char, float, unsigned, etc.
- [ ] Type casting
- [ ] Switch-case
- [ ] CRLF line ending support
- [ ] Variadic functions and parameter list without names (e.g. `int func(int)`)
- [ ] Code optimization
- [ ] Certainly a lot more :)

## Overview of the source files 

Main source files:

- **cc.c**: the CLI option parser and main entry point
- **lexer.c**: the tokenizer
- **parser.c**: a recursive descent parser. Syntactic errors are detected and
  printed out at this step, but semantic errors (like function redefinition),
  are only detected during code generation.
- **x86.c**: code generation to x86\_64 assembly (AT&T syntax). Also implements
  some semantic validations.

Auxiliary source files:

- **lib**: miscellaneous utilities to handle errors, strings, arrays,
  hashtables, temporary files, etc.
- **dot-printer.[ch]**: prints an AST (abstract syntax tree) in dot format.
- **symtable.[ch]**: table of "currently known symbols" (variables and
  functions) during assembly generation. This is where we check for errors like
  symbol redefinition and use-before-declaration. This table is duplicated when
  entering an inner scope (e.g. a code block) to allow for symbol shadowing
  without affecting the code generation on the upper scope.
- **labelset.[ch]**: set of user defined labels (i.e. those used in `goto`
  statements) to assist the assembly generation. Like `symtable.c`, `labelset.c`
  checks for redefinition and use-before-declaration errors regarding labels.

## Testing

Tests are divided in three parts:

- Tests of internal lib routines and APIs, available at `lib-tests`.

- General end-to-end tests, which are available at the submodule
  `compiler-tests`. This is [a
  fork](https://github.com/matheustavares/c-compiler-tests) of the test repo
  [provided by Nora](https://github.com/nlsandler/write_a_c_compiler). These
  tests are input based. They check that the compiler fails on invalid
  input and succeeds on valid input (i.e produces the same exit code and output
  as gcc).

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

## Resources

- Nora's tutorial posts on writing a C compiler:
  [https://norasandler.com/2017/11/29/Write-a-Compiler.html](https://norasandler.com/2017/11/29/Write-a-Compiler.html)
- C11 Standard: [latest draft](http://www.open-std.org/jtc1/sc22/WG14/www/docs/n1570.pdf).
  (In particular, [Annex A](http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf#page=476)'s "Language syntax summary".)
- Eli Bendersky's ["Parsing expressions by precedence climbing"](https://eli.thegreenplace.net/2012/08/02/parsing-expressions-by-precedence-climbing)
- Brown University's CS033 ["x64 Cheat Sheet"](https://cs.brown.edu/courses/cs033/docs/guides/x64_cheatsheet.pdf)
- Yale University's CS421 ["x86 Assembly Guide"](https://flint.cs.yale.edu/cs421/papers/x86-asm/asm.html)
- cppreference's ["operator precedence" page](https://en.cppreference.com/w/c/language/operator_precedence)
- Wikipedia's ["x86-64 calling conventions"](https://en.wikipedia.org/wiki/X86_calling_conventions#x86-64_calling_conventions)
