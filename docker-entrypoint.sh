#!/bin/sh

set -eu

# O usuario numerico definido pelo Compose nao possui HOME nem o diretorio
# /run/user/<uid> da sessao do host dentro da imagem. O Qt exige que seu
# diretorio de runtime exista e tenha acesso exclusivo ao usuario atual.
mkdir -p "${HOME}"
mkdir -p "${XDG_RUNTIME_DIR}"
chmod 0700 "${XDG_RUNTIME_DIR}"

exec "$@"
