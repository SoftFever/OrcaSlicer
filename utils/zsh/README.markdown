# ZSH Completions for Slic3r

To enable zsh(1) completions for Slic3r, add the following to your
``~/.zshrc`` file, replacing ``/path/to/Slic3r/`` with the actual path
to your Slic3r directory:

    typeset -U fpath

    if [[ -d /path/to/Slic3r/utils/zsh/functions ]]; then
        fpath=(/path/to/Slic3r/utils/zsh/functions $fpath)
    fi

    autoload -Uz compinit
    compinit
    zstyle ':completion:*' verbose true
    zstyle ':completion:*:descriptions' format '%B%d%b'
    zstyle ':completion:*:messages' format '%d'
    zstyle ':completion:*:warnings' format 'No matches for %d'
    zstyle ':completion:*' group-name '%d'

See the zshcompsys(1) man page for further details.
