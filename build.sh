#!/usr/bin/env bash

USER_HOME="${HOME:-$(getent passwd "$(id -u)" 5 | cut -d: -f6)}"
DEFAULT_FLAKE="$USER_HOME/Nixos"

if [ -t 0 ]; then
    read -p "Caminho da pasta do NixOS [padrão: $DEFAULT_FLAKE]: " FLAKE_PATH
    FLAKE_PATH=${FLAKE_PATH:-"$DEFAULT_FLAKE"}

    read -p "Caminho do arquivo de log [padrão: hist.log]: " LOG_PATH
    LOG_PATH=${LOG_PATH:-"hist.log"}
else
    FLAKE_PATH="${FLAKE_PATH:-$DEFAULT_FLAKE}"
    LOG_PATH="${LOG_PATH:-hist.log}"
fi

echo "==> Compilando nix-tool..."
echo "    -> Flake Path: $FLAKE_PATH"
echo "    -> Log Path:   $LOG_PATH"

g++ -O3 nix-tool.cpp -o nix-tool \
    -DFLAKE_PATH="\"$FLAKE_PATH\"" \
    -DLOG_PATH="\"$LOG_PATH\""

if [ $? -ne 0 ]; then
    echo "==> Falha na compilação."
    exit 1
fi

echo "==> Compilação concluída com sucesso!"

