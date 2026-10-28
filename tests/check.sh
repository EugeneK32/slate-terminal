#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
bash -n install.sh
# Zsh-specific code is syntax-checked only when the interpreter is present.
if command -v zsh >/dev/null; then
  zsh -n shell/slate-keybindings.zsh
fi
cc -std=c11 -Wall -Wextra -Werror tests/keycodes.c -o /tmp/slate-keycodes-test
/tmp/slate-keycodes-test
if pkg-config --exists gtk4 libadwaita-1 vte-2.91-gtk4; then
  make clean all
else
  echo 'GTK4/libadwaita/VTE4 development libraries unavailable: compilation test skipped.'
fi
