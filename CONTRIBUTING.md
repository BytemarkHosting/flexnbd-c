# Contribution guide

The code is formatted using the K&R style of "indent".

```
indent -kr <files go here>
```

The C unit tests have also been indented in the same way, but manually adjsted
such that the functions follow the normal libcheck layout.

```c
START_TEST( ... ) {


}
END TEST
```

Indent tends to mangle the `END_TEST` macro, so that will need adjusting if
`indent` is run over the test files again.



