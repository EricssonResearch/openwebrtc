#!/bin/bash
echo "This script will add the .local suffix to all hooks already in the .git/hooks directory"
echo "and create symbolic links to the hooks in git_hooks/ directory."

HOOK_DIR="$(git rev-parse --show-toplevel)/.git/hooks"
HOOK_NAMES="pre-commit pre-push"
SCRIPT_DIR="$( cd "$(dirname "$0")"; pwd )"

pushd $HOOK_DIR > /dev/null
for hook in $HOOK_NAMES; do
    if [ ! -h ./$hook -a -x ./$hook ]; then
        mv ./$hook $hook.local
        echo "moved $HOOK_DIR/$hook to $HOOK_DIR/$hook.local"
    fi
    rm $hook
    ln -s $SCRIPT_DIR/chain_hook $hook > /dev/null
done
popd > /dev/null

