# Contributing to this repository
We welcome your contributions!

## Opening issues
For bugs or enhancement requests, please file a GitHub issue unless it's
security related. When filing a bug, remember that the better written the bug is,
the more likely it is to be fixed. If you think you've found a security
vulnerability, do not raise a GitHub issue and follow the instructions in our
[security policy](SECURITY.md).

## Contributing code
Before submitting code via a pull request, you will need to have signed the [Oracle Contributor Agreement](https://oca.opensource.oracle.com/) (OCA) and your commits need to include the following line using the name and e-mail address you used to sign the OCA:

```Signed-off-by: Your Name <you@example.org>```

This can be automatically added to pull requests by committing with `--signoff`
or `-s`, e.g.

```git commit --signoff```

Only pull requests from committers that can be verified as having signed the OCA
can be accepted.

## Pull request process
1. Ensure there is an issue created to track and discuss the fix or enhancement you intend to submit.
2. Fork this repository.
3. Create a branch in your fork to implement the changes. We recommend using the issue number as part of your branch name, e.g. `1234-fixes`.
4. Ensure that any documentation is updated with the changes that are required by your change.
5. Ensure that any samples are updated if the base image has been changed.
6. Submit the pull request. *Do not leave the pull request blank.* Explain exactly what your changes are meant to do and provide simple steps on how to validate your changes. Ensure that you reference the issue you created as well.
7. We will assign the pull request to 2-3 people for review before it is merged.