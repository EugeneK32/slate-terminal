#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

if ! command -v pacman >/dev/null; then
  echo 'This installer targets Arch Linux / pacman. See README for manual build.' >&2
  exit 1
fi

sudo pacman -S --needed base-devel pkgconf gtk4 libadwaita vte4 adwaita-fonts wl-clipboard
make -j"$(nproc)"
make install
mkdir -p "$HOME/.config/slate-terminal"
if [[ ! -e "$HOME/.config/slate-terminal/config.ini" ]]; then
  cp assets/config.ini "$HOME/.config/slate-terminal/config.ini"
fi
cp shell/slate-keybindings.zsh "$HOME/.config/slate-terminal/slate-keybindings.zsh"

# Add ZLE bindings without replacing personal Zsh configuration. Back up first.
# On a fresh installation, create an empty .zshrc instead of skipping bindings.
if [[ ! -e "$HOME/.zshrc" ]]; then
  touch "$HOME/.zshrc"
fi
if ! grep -Fq '.config/slate-terminal/slate-keybindings.zsh' "$HOME/.zshrc"; then
  cp -p "$HOME/.zshrc" "$HOME/.zshrc.slate-backup-$(date +%Y%m%d-%H%M%S)"
  if grep -q 'zsh-syntax-highlighting' "$HOME/.zshrc"; then
    tmp=$(mktemp)
    awk '
      /zsh-syntax-highlighting/ && !inserted {
        print "# Slate Terminal: Home/End/Delete/Ctrl+arrows"
        print "source \"$HOME/.config/slate-terminal/slate-keybindings.zsh\""
        inserted=1
      }
      { print }
    ' "$HOME/.zshrc" > "$tmp"
    cat "$tmp" > "$HOME/.zshrc"
    rm -f "$tmp"
  else
    printf '\n# Slate Terminal: Home/End/Delete/Ctrl+arrows\nsource "$HOME/.config/slate-terminal/slate-keybindings.zsh"\n' >> "$HOME/.zshrc"
  fi
fi

if command -v update-desktop-database >/dev/null; then
  update-desktop-database "$HOME/.local/share/applications" || true
fi

echo
echo 'Slate Terminal installed.'
echo 'Launch: ~/.local/bin/slate-terminal'
echo 'Config: ~/.config/slate-terminal/config.ini'
echo 'If you use Zsh, reopen it to activate the keybindings.'
