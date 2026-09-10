#!/bin/bash

EC_IF=${EC_IF:-eth1}
if [ -n "${BASH_SOURCE:-}" ]; then
    SCRIPT_PATH=$BASH_SOURCE
else
    SCRIPT_PATH=$0
fi
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$SCRIPT_PATH")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
EC_ASSETS_DIR=${EC_ASSETS_DIR:-$PROJECT_DIR/modules}
MODULES_DIR=/lib/modules/$(uname -r)/extra
APP_NAME=ethercat_csv_test

need_root() {
    if [ "$(id -u)" -ne 0 ]; then
        echo "This command must be run as root." >&2
        exit 1
    fi
}

need_file() {
    if [ ! -f "$1" ]; then
        echo "Missing required asset: $1" >&2
        echo "Set EC_ASSETS_DIR to the directory containing the IgH deployment files." >&2
        exit 1
    fi
}

module_loaded() {
    lsmod | awk '{print $1}' | grep -qx "$1"
}

deploy() {
    need_root
    need_file "$EC_ASSETS_DIR/ec_master.ko"
    need_file "$EC_ASSETS_DIR/ec_generic.ko"
    need_file "$EC_ASSETS_DIR/libethercat.so.1.2.0"
    need_file "$EC_ASSETS_DIR/ethercat"

    install -d "$MODULES_DIR"
    install -m 0644 "$EC_ASSETS_DIR/ec_master.ko" "$MODULES_DIR/ec_master.ko"
    install -m 0644 "$EC_ASSETS_DIR/ec_generic.ko" "$MODULES_DIR/ec_generic.ko"
    depmod -a

    install -m 0755 "$EC_ASSETS_DIR/libethercat.so.1.2.0" /usr/lib/libethercat.so.1.2.0
    ln -sfn libethercat.so.1.2.0 /usr/lib/libethercat.so.1
    ln -sfn libethercat.so.1.2.0 /usr/lib/libethercat.so
    ldconfig

    install -m 0755 "$EC_ASSETS_DIR/ethercat" /usr/sbin/ethercat
    if [ -f "$EC_ASSETS_DIR/ecrt.h" ]; then
        install -m 0644 "$EC_ASSETS_DIR/ecrt.h" /usr/include/ecrt.h
    fi

    if [ -x "$PROJECT_DIR/bin/$APP_NAME" ]; then
        install -m 0755 "$PROJECT_DIR/bin/$APP_NAME" "/usr/local/bin/$APP_NAME"
    fi

    echo "IgH EtherCAT deployment completed."
}

stop() {
    need_root
    killall "$APP_NAME" 2>/dev/null || true
    module_loaded ec_generic && rmmod ec_generic
    module_loaded ec_master && rmmod ec_master
}

start() {
    need_root
    if ! ip link show "$EC_IF" >/dev/null 2>&1; then
        echo "Network interface does not exist: $EC_IF" >&2
        exit 1
    fi

    ip addr flush dev "$EC_IF"
    ip link set "$EC_IF" up

    tries=0
    while [ "$tries" -lt 20 ]; do
        carrier=$(cat "/sys/class/net/$EC_IF/carrier" 2>/dev/null || echo 0)
        [ "$carrier" = "1" ] && break
        tries=$((tries + 1))
        sleep 0.2
    done

    mac=$(cat "/sys/class/net/$EC_IF/address")
    master_ko="$MODULES_DIR/ec_master.ko"
    generic_ko="$MODULES_DIR/ec_generic.ko"
    need_file "$master_ko"
    need_file "$generic_ko"

    module_loaded ec_master || insmod "$master_ko" main_devices="$mac"
    module_loaded ec_generic || insmod "$generic_ko"

    tries=0
    while [ "$tries" -lt 20 ]; do
        [ -e /dev/EtherCAT0 ] && break
        tries=$((tries + 1))
        sleep 0.1
    done

    if [ ! -e /dev/EtherCAT0 ]; then
        echo "/dev/EtherCAT0 was not created. Check dmesg and module compatibility." >&2
        exit 1
    fi

    echo "EtherCAT master started on $EC_IF ($mac)."
    command -v ethercat >/dev/null 2>&1 && ethercat slaves || true
}

status() {
    echo "Interface: $EC_IF"
    ip -brief link show "$EC_IF" 2>/dev/null || true
    echo "Modules:"
    lsmod | awk 'NR == 1 || $1 ~ /^ec_(master|generic)$/'
    echo "Device:"
    ls -l /dev/EtherCAT0 2>/dev/null || echo "/dev/EtherCAT0 not present"
    if command -v ethercat >/dev/null 2>&1; then
        echo "Master:"
        ethercat master 2>/dev/null || true
        echo "Slaves:"
        ethercat slaves 2>/dev/null || true
    else
        echo "The ethercat command is not installed."
    fi
}

usage() {
    echo "Usage: $0 {deploy|start|stop|reload|status}"
    echo "Environment: EC_IF=eth1 EC_ASSETS_DIR=/path/to/igh-assets"
}

case ${1:-} in
    deploy) deploy ;;
    start) start ;;
    stop) stop ;;
    reload) stop; start ;;
    status) status ;;
    *) usage; exit 1 ;;
esac
