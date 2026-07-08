#!/usr/bin/env bash
# Builds, signs, notarizes, and packages Kadabra K-Player for beta distribution.
#
# Prerequisites (one-time, done by hand - never automated here):
#   1. A "Developer ID Application" certificate installed in your keychain
#      (check with: security find-identity -v -p codesigning)
#   2. Notarization credentials stored once via:
#      xcrun notarytool store-credentials "kplayer-notary" \
#          --apple-id "YOUR_APPLE_ID_EMAIL" --team-id "B74YHJSPSY"
#      (prompts securely for an app-specific password from appleid.apple.com)
#
# Usage: scripts/build_release.sh
#
# Uses a separate build-release/ directory so it never touches or reconfigures
# the existing build/ folder used for day-to-day ad-hoc-signed development.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="build-release"
SCHEME="IMI_KPlayer"
CONFIG="Release"
KEYCHAIN_PROFILE="kplayer-notary"
APP_NAME="Kadabra K-Player.app"

echo "==> Configuring ${BUILD_DIR} (KPLAYER_RELEASE_BUILD=ON)"
cmake -S . -B "${BUILD_DIR}" -G Xcode \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    -DKPLAYER_RELEASE_BUILD=ON

echo "==> Building ${SCHEME} (${CONFIG})"
cmake --build "${BUILD_DIR}" --config "${CONFIG}" --target "${SCHEME}"

APP_PATH="${BUILD_DIR}/${SCHEME}_artefacts/${CONFIG}/${APP_NAME}"
if [ ! -d "${APP_PATH}" ]; then
    echo "error: expected app not found at ${APP_PATH}" >&2
    exit 1
fi

VERSION="$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "${APP_PATH}/Contents/Info.plist")"
echo "==> Built ${APP_PATH} (version ${VERSION})"

echo "==> Verifying code signature"
codesign --verify --deep --strict --verbose=2 "${APP_PATH}"
codesign --display --entitlements - "${APP_PATH}"

ZIP_PATH="${BUILD_DIR}/KPlayer-${VERSION}-for-notarization.zip"
echo "==> Zipping for notarization submission: ${ZIP_PATH}"
rm -f "${ZIP_PATH}"
ditto -c -k --keepParent "${APP_PATH}" "${ZIP_PATH}"

echo "==> Submitting to Apple notary service (this can take a few minutes)"
if ! xcrun notarytool submit "${ZIP_PATH}" --keychain-profile "${KEYCHAIN_PROFILE}" --wait; then
    echo "error: notarization submission failed or was rejected - fetching the log of the most recent submission:" >&2
    LAST_ID="$(xcrun notarytool history --keychain-profile "${KEYCHAIN_PROFILE}" | awk '/id:/{print $2; exit}')"
    if [ -n "${LAST_ID:-}" ]; then
        xcrun notarytool log "${LAST_ID}" --keychain-profile "${KEYCHAIN_PROFILE}"
    fi
    exit 1
fi

echo "==> Stapling notarization ticket to the app"
xcrun stapler staple "${APP_PATH}"
xcrun stapler validate "${APP_PATH}"

echo "==> Gatekeeper assessment (should say 'accepted')"
spctl --assess --type execute --verbose "${APP_PATH}"

DMG_NAME="Kadabra-K-Player-${VERSION}"
DMG_PATH="${BUILD_DIR}/${DMG_NAME}.dmg"
STAGING_DIR="${BUILD_DIR}/dmg-staging"

echo "==> Building DMG: ${DMG_PATH}"
rm -rf "${STAGING_DIR}"
mkdir -p "${STAGING_DIR}"
ditto "${APP_PATH}" "${STAGING_DIR}/${APP_NAME}"
ln -s /Applications "${STAGING_DIR}/Applications"

rm -f "${DMG_PATH}"
hdiutil create -volname "Kadabra K-Player" \
    -srcfolder "${STAGING_DIR}" \
    -ov -format UDZO \
    "${DMG_PATH}"

echo "==> Signing the DMG"
codesign --force --sign "Developer ID Application" --timestamp "${DMG_PATH}"

echo ""
echo "Done. Ready to distribute: ${DMG_PATH}"
