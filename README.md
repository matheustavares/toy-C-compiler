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

## Testing

See [https://github.com/nlsandler/write_a_c_compiler](https://github.com/nlsandler/write_a_c_compiler).

