### Intro

This is a Zend extension to add recursive tail call optimisation to PHP. Developed and tested with PHP 8, but should compile & work with other versions.

Disclaimer: I can't guarantee the completeness or stability of this extension - so if you choose to use it in the wild, exercise discretion.

### Table of contents
* [Example](#example)
* [Installation](#install)
* [Configuration](#config)
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

(Read on if you're curious to learn what's happening in the background.)

If we dump the opcodes with the module disabled, we get something like this:

```
0000 CV0($n) = RECV_INIT 1 int(0)
0001 T1 = IS_SMALLER CV0($n) int(100000)
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
0001 T1 = IS_SMALLER CV0($n) int(100000)
0002 JMPZ T1 0008
0003 T2 = ADD CV0($n) int(1)
0004 ASSIGN CV0($n) T2
0005 JMP 0001
0006 NOP
0007 NOP
0008 RETURN CV0($n)
0009 RETURN null
```

Here, all function call opcodes are removed/rewritten. Instead of ```T2``` being pushed as an argument, it's assigned directly to ```$n``` - and instead of ```test``` being called again, there's a ```JMP``` to ```0001``` - back to the beginning of the function.

(The ```NOP``` on ```0006``` and ```0007``` are just overwritten leftover opcodes; they could/should get optimised out in a later pass by something like OPcache.)

Here is a slightly more complex example:

```
class Test
{
    public function test($x = 10, $n = 0) {
        // (Do something important with $x here.)

        if ($n < 100000) {
            $x = 9000;

            // Now $x has been changed.

            return $this->test(n: $n + 1);
        }

        return test($n);
    }
}
```

With the module enabled, the resulting opcodes for the ```test``` method (after optimisation) are as such:

```
0000 CV0($x) = RECV_INIT 1 int(10)
0001 CV1($n) = RECV_INIT 2 int(0)
0002 T2 = IS_SMALLER CV1($n) int(100000)
0003 JMPZ T2 0011
0004 ASSIGN CV0($x) int(9000)
0005 T4 = ADD CV1($n) int(1)
0006 ASSIGN CV0($x) int(10)
0007 ASSIGN CV1($n) T4
0008 JMP 0002
0009 NOP
0010 NOP
0011 INIT_FCALL 1 112 string("test")
0012 SEND_VAR CV1($n) 1
0013 V6 = DO_UCALL
0014 RETURN V6
0015 RETURN null
```

This example demonstrates a number of things:

1. The module works with named arguments.
2. At ```0006``` we can see that ```$x``` is reset to its default value of ```10```.
3. Between ```0011``` and ```0014```, the call to another ```test``` function in a different scope, is correctly identified as not being recursive.

<a name="install"></a>
### Installation

As it stands, you'll need to compile the module yourself. There are ways to compile it in isolation, but it's probably much easier to use the PHP build tools.

First,



<a name="config"></a>
### Configuration

[todo]

<a name="internals"></a>
### Under the hood

<a name="license"></a>
### License

MIT, I suppose.