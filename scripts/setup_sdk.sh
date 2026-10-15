#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
for tool in curl unzip sha256sum make; do
    command -v "$tool" >/dev/null || { echo "Missing dependency: $tool" >&2; exit 1; }
done
command -v llvm-config-18 >/dev/null || command -v llvm-config >/dev/null || {
    echo 'Install clang-18, lld-18 and llvm-18 first (see README).' >&2
    exit 1
}
mkdir -p .tools
curl -fL --retry 2 -o .tools/sdk-v0.44.zip \
    https://github.com/ps5-payload-dev/sdk/releases/download/v0.44/ps5-payload-sdk.zip
echo '4d06eab777bb135705b5a6816688c9132efb246093fe606ac488ab6cf65ca5b0  .tools/sdk-v0.44.zip' | sha256sum -c -
unzip -q -o .tools/sdk-v0.44.zip -d .tools
chmod +x .tools/ps5-payload-sdk/bin/*
echo 'SDK v0.44 ready. Run make.'
