#!/bin/bash

##
 # @file build/update_issue_template.sh
 # @brief Adds a version to the version dropdown of the GitHub bug report
 #        template, as the newest entry. Takes the version as first argument,
 #        or reads it from CMakeLists.txt when called with no arguments.
 #        Run from the build/ directory before a release.
 #
##
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BASEPATH=$SCRIPT_DIR/..
template="$BASEPATH/.github/ISSUE_TEMPLATE/bug_report.yml"

megacmd_VERSION=${1:-}
if [ -z "$megacmd_VERSION" ]; then
    megacmd_VERSION=$(grep -Po "MEGACMD_.*_VERSION [0-9]*" "$BASEPATH/CMakeLists.txt" | awk '{print $2}' | paste -sd '.')
fi

if grep -qE "^[[:space:]]*- ${megacmd_VERSION//./\\.}[[:space:]]*$" "$template"; then
    echo "Version $megacmd_VERSION already listed in $template. Nothing to do."
    exit 0
fi

trap 'rm -f "$template.new"' EXIT

awk -v version="$megacmd_VERSION" '
    { print }
    /^[[:space:]]*id: version[[:space:]]*$/ { in_version = 1 }
    in_version && !added && /^[[:space:]]*options:[[:space:]]*$/ {
        match($0, /^[[:space:]]*/)
        print substr($0, 1, RLENGTH) "  - " version
        added = 1
    }
    END {
        if (!added) {
            print "Could not find the version dropdown options" > "/dev/stderr"
            exit 1
        }
    }
' "$template" > "$template.new"

mv "$template.new" "$template"
echo "Added $megacmd_VERSION to $template"
