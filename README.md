(I will flesh this out properly - eventually.)

The general idea here is:

We go through each opcode in the op array, looking for a return opcode.

If one is found, we then check whether the opcode before the return, was a call opcode.

If not, we just keep looking for another return opcode & repeat.

[ ... ]