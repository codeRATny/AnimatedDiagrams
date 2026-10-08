#!/usr/bin/env bash
# Печатает AD_VERSION_ARG=-DAD_VERSION=X.Y.Z для тега vX.Y.Z[-suffix] (для $GITHUB_ENV).
# Вне тега ничего не печатает — используется версия из CMakeLists.txt.
set -euo pipefail

if [[ "${GITHUB_REF_TYPE:-}" == "tag" && "${GITHUB_REF_NAME:-}" == v* ]]; then
    version="${GITHUB_REF_NAME#v}"
    version="${version%%-*}"  # v2.1.0-rc1 → 2.1.0
    if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        echo "::error::Тег '${GITHUB_REF_NAME}' должен иметь вид vX.Y.Z" >&2
        exit 1
    fi
    echo "AD_VERSION_ARG=-DAD_VERSION=${version}"
fi
