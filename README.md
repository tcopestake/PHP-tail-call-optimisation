### Intro

This is a Zend extension to add recursive tail call optimisation to PHP. Developed and tested with PHP 8, but should compile & work with other versions.

Disclaimer: I can't guarantee the completeness or stability of this extension - so if you choose to use it in the wild, exercise discretion.

### Table of contents
* [Example](#example)
* [Installation]
* [Configuration]
* [Under the hood](#internals)
* [Caveats]
* [License](#license)

<a name="example"></a>
### Example

In this example, we'll use the following PHP code:

```
function test($n = 0) {
    if ($n < 100000) {
        return test($n + 1);
    }

    return $n;
}
```

With this module disabled - on my machine - the total running time benchmarks at around 0.006 - 0.0155 seconds - and total memory usage peaks at around 11.37 - 14.52 megabytes. Increasing the loop count from 100,000 to 10,000,000 results in a memory allocation error - hitting approx. 134.21 megabytes.

With the module enabled, the total running time benchmarks around 0.0009 - 0.002 seconds (anywhere from 3x to 17x faster) - and memory usage is fairly constant at around 0.43 megabytes. Increasing the loop count to 10,000,000 completes in ~0.16 seconds - with memory usage still constant at around 0.43 megabytes.

If you're curious to learn what's happening in the background:

If we dump the opcodes with the module disabled, we get something like this:

```
0000 CV0($n) = RECV_INIT 1 int(0)
0001 T1 = IS_SMALLER CV0($n) int(10000000)
0002 JMPZ T1 0008
0003 INIT_FCALL_BY_NAME 1 string("test")
0004 T2 = ADD CV0($n) int(1)
0005 SEND_VAL_EX T2 1
0006 V3 = DO_FCALL_BY_NAME
0007 RETURN V3
0008 RETURN CV0($n)
0009 RETURN null
```

Between ```0003``` and ```0007``` we can see the opcodes for the recursive tail call. The result of ```$n + 1``` is returned in ```T2``` and then passed as an argument for the recursive call to ```test```. The value returned by ```test``` is then stored in ```V3``` and returned on line ```0007```.

With the module enabled, these opcodes are rewritten to something closer to this:

```
0000 CV0($n) = RECV_INIT 1 int(0)
0001 T1 = IS_SMALLER CV0($n) int(10000000)
0002 JMPZ T1 0008
0003 T2 = ADD CV0($n) int(1)
0004 JMP 0010
0005 NOP
0006 NOP
0007 NOP
0008 RETURN CV0($n)
0009 RETURN null
0010 ASSIGN CV0($n) T2
0011 JMP 0001
```



<a name="installation"></a>
### Installation


<a name="configuration"></a>
### Configuration

[todo]

<a name="internals"></a>
### Under the hood

<a name="license"></a>
### License

MIT, I suppose.