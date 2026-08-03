#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIRECTORY="${DKR_LINUX_BUILD_DIR:-${PROJECT_ROOT}/build/dkr-runtime-linux}"
BINARY="${BUILD_DIRECTORY}/bin/Release/DKRPort"
APPDIR="${DKR_APPDIR:-${PROJECT_ROOT}/dist/DKRPort-Linux-x86_64.AppDir}"
VERSION="${DKR_RELEASE_VERSION:-1.0.0-rc3}"
OUTPUT="${DKR_APPIMAGE_OUTPUT:-${PROJECT_ROOT}/dist/DKRPort-${VERSION}-Linux-x86_64.AppImage}"
LINUXDEPLOY="${LINUXDEPLOY:-${PROJECT_ROOT}/.deps/tools/linuxdeploy-x86_64.AppImage}"
APPIMAGE_PLUGIN="${LINUXDEPLOY_PLUGIN_APPIMAGE:-${PROJECT_ROOT}/.deps/tools/linuxdeploy-plugin-appimage}"

validate_release_tree() {
  local root="$1"
  local file extension magic inspected=0
  while IFS= read -r -d '' file; do
    inspected=$((inspected + 1))
    extension="${file##*.}"
    extension="${extension,,}"
    case "${extension}" in
      z64|v64|n64|eep|mpk|o2r|otr)
        echo "Release staging contains prohibited game data: ${file}" >&2
        return 1
        ;;
    esac
    magic="$(od -An -tx1 -N4 "${file}" 2>/dev/null | tr -d '[:space:]')"
    case "${magic}" in
      80371240|37804012|40123780)
        echo "Release staging contains an N64 ROM header: ${file}" >&2
        return 1
        ;;
    esac
  done < <(find "${root}" -type f -print0)
  [[ "${inspected}" -gt 0 ]] || {
    echo "Release staging is empty: ${root}" >&2
    return 1
  }
  echo "Release staging scan passed: ${inspected} files inspected"
}

[[ -x "${BINARY}" ]] || { echo "Missing Linux release binary: ${BINARY}" >&2; exit 1; }
[[ -x "${LINUXDEPLOY}" ]] || { echo "Missing linuxdeploy: ${LINUXDEPLOY}" >&2; exit 1; }
[[ -x "${APPIMAGE_PLUGIN}" ]] || { echo "Missing linuxdeploy AppImage plugin: ${APPIMAGE_PLUGIN}" >&2; exit 1; }
[[ ! -e "${APPDIR}" ]] || { echo "AppDir already exists; choose a fresh DKR_APPDIR: ${APPDIR}" >&2; exit 1; }
[[ ! -e "${OUTPUT}" ]] || { echo "Output already exists; choose a fresh DKR_APPIMAGE_OUTPUT: ${OUTPUT}" >&2; exit 1; }

mkdir -p "${APPDIR}/usr/share/doc/dkr-port" "$(dirname "${OUTPUT}")"
mkdir -p "${APPDIR}/usr/share/metainfo"
install -m 0644 "${PROJECT_ROOT}/LICENSE.md" "${APPDIR}/usr/share/doc/dkr-port/LICENSE.md"
install -m 0644 "${PROJECT_ROOT}/THIRD_PARTY.md" "${APPDIR}/usr/share/doc/dkr-port/THIRD_PARTY.md"
install -m 0644 "${PROJECT_ROOT}/runtime-recomp/COPYING-NOTICE.md" "${APPDIR}/usr/share/doc/dkr-port/COPYING-NOTICE.md"
install -m 0644 "${PROJECT_ROOT}/packaging/linux/dkr-port.appdata.xml" "${APPDIR}/usr/share/metainfo/dkr-port.appdata.xml"

export PATH="$(dirname "${APPIMAGE_PLUGIN}"):${PATH}"
export OUTPUT
export VERSION
export LDAI_OUTPUT="${OUTPUT}"
export LINUXDEPLOY_OUTPUT_VERSION="${VERSION}"
export APPIMAGE_EXTRACT_AND_RUN=1
# This local release has no public project homepage yet. appimagetool treats
# that optional AppStream field as a fatal warning, so package the supplied
# metadata without the network-facing catalogue validation step.
export LDAI_NO_APPSTREAM=1

"${LINUXDEPLOY}" --appimage-extract-and-run \
  --appdir "${APPDIR}" \
  --executable "${BINARY}" \
  --desktop-file "${PROJECT_ROOT}/packaging/linux/dkr-port.desktop" \
  --icon-file "${PROJECT_ROOT}/packaging/linux/dkr-port.svg" \
  --output appimage

validate_release_tree "${APPDIR}"
[[ -s "${OUTPUT}" ]] || { echo "AppImage output is missing or empty: ${OUTPUT}" >&2; exit 1; }
echo "Created ${OUTPUT}"
sha256sum "${OUTPUT}"
