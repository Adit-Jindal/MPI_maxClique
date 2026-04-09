# Changes made:

- Changed `adj` to hold int rather than bool, to be able to use adj[i].data() in the Bcast command (bool vectors may not be stored contiguously and thus do not allow .data() functionality)
- Without broadcasting P_max in between iterations, pruning became inefficient