# Slate Terminal — Zsh ZLE editing bindings for xterm-compatible sequences.
# Source once from ~/.zshrc, after bindkey -e and before syntax highlighting.
# Other terminals with the same sequences benefit too.

bindkey -e
for seq in '^[[H' '^[OH' '^[[1~' '^[[7~'; do bindkey "$seq" beginning-of-line; done
for seq in '^[[F' '^[OF' '^[[4~' '^[[8~'; do bindkey "$seq" end-of-line; done
bindkey '^[[3~' delete-char
bindkey '^?' backward-delete-char
bindkey '^H' backward-delete-char
bindkey '^[[1;5D' backward-word
bindkey '^[[1;5C' forward-word
bindkey '^[[5D' backward-word
bindkey '^[[5C' forward-word
# With this profile, Shift+arrows are sent as CSI 1;2 C/D.
# Optional: source an installed zsh-shift-select plugin *after* these bindings
# to enable editor-like selected text, including Ctrl+Shift+arrows.

# Slate clipboard bridge for Shift-selected ZLE text. Normal mouse-selected
# terminal text is copied directly by VTE and does not use this widget.
# Requires wl-copy (Wayland) or xclip (X11).
_slate_copy_region_to_clipboard() {
  if (( ! REGION_ACTIVE || CURSOR == MARK )); then
    return 0
  fi
  zle copy-region-as-kill
  if [[ -n ${WAYLAND_DISPLAY:-} ]] && (( $+commands[wl-copy] )); then
    print -rn -- "$CUTBUFFER" | wl-copy
  elif (( $+commands[xclip] )); then
    print -rn -- "$CUTBUFFER" | xclip -selection clipboard
  fi
}
zle -N _slate_copy_region_to_clipboard
bindkey -M emacs '^[[27;6;99~' _slate_copy_region_to_clipboard
bindkey -M viins '^[[27;6;99~' _slate_copy_region_to_clipboard
# This keymap is provided by zsh-shift-select; it may not be installed.
bindkey -M shift-select '^[[27;6;99~' _slate_copy_region_to_clipboard 2>/dev/null || true
