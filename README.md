
The general idea here is:

We go through each opcode in the block, looking for a return opcode.

If one is found, we then check whether the opcode before the return, was a call opcode.

If not, we just keep looking for another return opcode & repeat.

If so, we search for the respective init opcode for the call opcode we found. Once found, we check whether the call is recursive i.e. the function is calling itself.

If not, we ignore this return & function all altogether.

If so, we separate out the opcodes from the init to the return into their own separate "sub-block."

(The idea behind these individual blocks is that any irrelevant opcodes - i.e. those which are not related to recursive )