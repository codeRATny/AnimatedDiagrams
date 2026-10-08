#!/usr/bin/env bash
# Prints AD_VERSION_ARG=-DAD_VERSION=X.Y.Z for a vX.Y.Z[-suffix] tag (for $GITHUB_ENV).
# Prints nothing outside a tag: the version from CMakeLists.txt is used.
set -euo pipefail

if [[ "${GITHUB_REF_TYPE:-}" == "tag" && "${GITHUB_REF_NAME:-}" == v* ]]; then
    version="${GITHUB_REF_NAME#v}"
    version="${version%%-*}"  # v2.1.0-rc1 -> 2.1.0
    if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        echo "::error::Tag '${GITHUB_REF_NAME}' must look like vX.Y.Z" >&2
        exit 1
    fi
    echo "AD_VERSION_ARG=-DAD_VERSION=${version}"
fi
