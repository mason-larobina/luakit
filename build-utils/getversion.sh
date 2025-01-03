#!/bin/sh
# Get the current version using various methods.

# The following will serve as a fallback version string (which is the short
# hash of the latest commit before the application was packaged (if it was
# packaged)). You will find that this file is listed inside the .gitattributes
# file like so:
#
#   ./build-utils/getversion.sh export-subst
#
# This tells git to replace the format string in the following line with the
# current hash upon the calling of the `git archive <hash/tag>` command.
VERSION_FROM_ARCHIVE=$Format:%H$

# The preferred method is to use the git describe command but this is only
# possible if the .git directory is present.
if [ -d .git -a -r .git ]
then
    VERSION_FROM_GIT=`git describe --tags --always`
fi

# In case of a worktree checkout, there is not .git directory, but a
# .git file, which contains the path to the branch information within
# the git repository. In this case, "git describe" works too.
if [ -f .git -a -r .git ] && grep -q ^gitdir .git
then
    VERSION_FROM_GIT=`git describe --tags --always`
fi

if [ x"$VERSION_FROM_GIT" != x ]; then
    echo $VERSION_FROM_GIT; exit 0;
fi

if [ "$VERSION_FROM_ARCHIVE" != ':%H$' ]; then
    echo $VERSION_FROM_ARCHIVE | cut -b1-8; exit 0;
fi

echo "ERROR: Git commit hash detection failed: Please check why "
     "build-utils/getversion.sh is failing in your setup and get in touch with us." >&2

exit 2

# vim: ft=sh:et:sw=4:ts=8:sts=4:tw=80
