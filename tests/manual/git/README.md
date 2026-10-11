# Branch model stress test

Build Qt Creator with `WITH_TESTS=ON`, then run:

```sh
bash tests/manual/git/branchmodel-refresh-stress.sh \
    build/bin/qtcreator /path/to/repository 200
```

The repository must have a commit. The test creates and deletes temporary
branch refs in a unique namespace, without checking out branches or changing
working tree files. The script removes those refs when the test exits,
including when Qt Creator crashes. Run it against a disposable repository.

The manual test is registered only when the script supplies its environment
variables. Neither CTest nor a normal `qtcreator -test Git` run includes it.
The regression tests run separately with `ctest -R tst_gitbranchmodel` or
`qtcreator -clientid <unique-id> -tcs -test Git,BranchModelTest`.

For a visible run, set `QTC_GIT_BRANCH_STRESS_VISIBLE=1` and
`QT_QPA_PLATFORM` to the platform's native plugin, such as `xcb` on Linux.
Use an AddressSanitizer build to investigate model/index lifetime failures.
