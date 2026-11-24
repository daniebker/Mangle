#!/bin/sh
# Mangle launcher, run from KUAL.
#   mangle.sh library   open the book list
#   mangle.sh resume    reopen the last book
#
# Pauses the Kindle UI (cvm) while the reader runs and always resumes it,
# whether the reader exits normally, crashes, or this script is killed.

EXT_DIR=$(cd "$(dirname "$0")/.." && pwd)

# KUAL runs inside the Kindle UI process we're about to pause, so detach
# first; otherwise we'd be tied to a stopped parent.
if [ "${MANGLE_DETACHED:-}" != 1 ]; then
	MANGLE_DETACHED=1 nohup "$0" "$@" >/dev/null 2>&1 &
	exit 0
fi

MANGLE_LIBRARY=/mnt/us/manga
FLASH_EVERY=8
PAGE_OVERVIEW=1
[ -f "$EXT_DIR/mangle.conf" ] && . "$EXT_DIR/mangle.conf"

STATE=$EXT_DIR/state
LOG=$EXT_DIR/mangle.log
mkdir -p "$MANGLE_LIBRARY" "$STATE"
# Keep the log small.
[ -f "$LOG" ] && [ "$(wc -c <"$LOG")" -gt 262144 ] && mv -f "$LOG" "$LOG.old"

restored=0
restore_ui() {
	[ "$restored" = 1 ] && return
	restored=1
	killall -CONT cvm 2>/dev/null
	lipc-set-prop com.lab126.powerd preventScreenSaver 0 2>/dev/null
	# Going Home makes the Kindle UI repaint over our last frame.
	lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home 2>/dev/null
	echo "$(date) ui restored" >>"$LOG"
}
trap restore_ui EXIT
trap 'exit 1' INT TERM HUP QUIT    # replaced once the reader starts

echo "$(date) start $*" >>"$LOG"
# Let KUAL's menu close before we take over the screen.
sleep 1
killall -STOP cvm 2>/dev/null
# The screensaver can't draw while cvm is paused; don't let it trigger.
lipc-set-prop com.lab126.powerd preventScreenSaver 1 2>/dev/null

case "${1:-library}" in
	resume) RESUME=-r ;;
	*) RESUME= ;;
esac
[ "$PAGE_OVERVIEW" = 0 ] && NO_OVERVIEW=-O || NO_OVERVIEW=

# Not exec: the EXIT trap must run after the reader exits (or crashes).
# Run it in the background so a signal to this script reaches it too.
"$EXT_DIR/bin/mangle" -l "$MANGLE_LIBRARY" -s "$STATE" -f "$FLASH_EVERY" $NO_OVERVIEW $RESUME >>"$LOG" 2>&1 &
READER=$!
trap 'kill -TERM "$READER" 2>/dev/null; wait "$READER"; exit 1' INT TERM HUP QUIT
wait "$READER"
echo "$(date) reader exited with $?" >>"$LOG"
