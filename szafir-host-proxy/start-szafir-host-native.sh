#!/bin/sh

debug_log() {
    [ "${SZAFIR_HOST_DEBUG:-0}" = "1" ] && echo "DEBUG: $*" >&2
}

# Persistent tmp
mkdir -p "$HOME/.java/tmp"

debug_log "start-szafir-host-native.sh PID=$$ UID=$(id -u) HOME=$HOME XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}"

# Verified components store managed by szafir-host-proxy.
XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
COMPONENTS_DIR="${XDG_DATA_HOME}/szafir-host-proxy/components"
INSTALLER_JAR="${COMPONENTS_DIR}/szafirhost-install.jar"

debug_log "COMPONENTS_DIR=$COMPONENTS_DIR INSTALLER_JAR=$INSTALLER_JAR"

if [ ! -f "${INSTALLER_JAR}" ]; then
    echo "ERROR: szafirhost-install.jar not found at ${INSTALLER_JAR}" >&2
    exit 1
fi

INSTALL_DIR="${XDG_DATA_HOME}/szafir_host"
LOCK_FILE="${INSTALL_DIR}/.install.lock"
JAR_PATH="${INSTALL_DIR}/SzafirHost.jar"

# Run IzPack installer on first launch; flock prevents parallel installs.
if [ ! -f "$JAR_PATH" ]; then
    mkdir -p "$INSTALL_DIR"
    (
        flock -x 200
        # Re-check after acquiring the lock (another instance may have just finished).
        if [ ! -f "$JAR_PATH" ]; then
            AUTO_XML="$(mktemp)"
            cat > "$AUTO_XML" << EOF
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<AutomatedInstallation langpack="pol">
    <com.izforge.izpack.panels.htmllicence.HTMLLicencePanel id="SHLicensePanel"/>
    <com.izforge.izpack.panels.target.TargetPanel id="SHTargetPanel">
        <installpath>${INSTALL_DIR}</installpath>
    </com.izforge.izpack.panels.target.TargetPanel>
    <com.izforge.izpack.panels.packs.PacksPanel id="SHPacksPanel">
        <pack index="0" name="Szafir Host" selected="true"/>
    </com.izforge.izpack.panels.packs.PacksPanel>
    <com.izforge.izpack.panels.install.InstallPanel id="SHInstallPanel"/>
    <com.izforge.izpack.panels.finish.FinishPanel id="SHFinishPanel"/>
</AutomatedInstallation>
EOF
            debug_log "running installer ${INSTALLER_JAR} (auto xml ${AUTO_XML})"
            /app/jre/bin/java -jar "${INSTALLER_JAR}" "$AUTO_XML" >&2
            debug_log "installer exit=$?"
            rm -f "$AUTO_XML"
        fi
    ) 200>"$LOCK_FILE"
fi

cd "$INSTALL_DIR" || exit 1

debug_log "launching SzafirHost.jar (path=$JAR_PATH) args=$* cwd=$(pwd)"
exec /app/jre/bin/java \
    -Djava.io.tmpdir="$HOME/.java/tmp" \
    -Dsun.java2d.uiScale.enabled=true \
    -jar SzafirHost.jar "$@"
